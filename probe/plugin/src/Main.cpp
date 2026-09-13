// Photo Mode RR Probe - logs why a frame does or does not get Ray Reconstruction.
//
// A measuring instrument, never released. Every offset is game 2.31 and is read, never written.
//
// The renderer builds a 128-bit feature mask for each view it draws. Bit 0x46 of that mask is Ray
// Reconstruction: the DLSS wrapper tags RR's inputs and evaluates the RR feature only when it is
// set. The mask builder sets the bit only when six conditions hold. This hooks the builder, lets
// it run, then logs each condition's inputs and the bit it produced, one line per view whenever
// that line changes.

#include <Windows.h>
#include <RED4ext/RED4ext.hpp>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace
{
// The frame feature mask builder (0x1d49540 on 2.31), by RED4ext hash.
constexpr uint32_t kHashBuildFeatureMask = 137310724;

// Its first ten bytes: mov [rsp+18h], r8 / mov [rsp+8], rcx. Anything else is not this build.
constexpr uint8_t kPrologue[] = {0x4c, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x4c, 0x24, 0x08};

// Bit indices in the mask.
constexpr uint32_t kBitRayReconstruction = 0x46;
constexpr uint32_t kBitDlssWrapperSecond = 0x55;

// Renderer object (first argument) -> Streamline manager.
constexpr size_t kRendererStreamline = 0x4658;
// Streamline manager bytes the conditions read.
constexpr size_t kSlDlssA = 0x3e8;
constexpr size_t kSlDlssB = 0x3e9;
constexpr size_t kSlRrAvailable = 0x3ea;
constexpr size_t kSlLoadedGate = 0x43b;
constexpr size_t kSlLoadedRr1 = 0x437;
constexpr size_t kSlLoadedRr2 = 0x432;

// View object (third argument).
constexpr size_t kViewRect = 0x14;        // four int32: min x, min y, max x, max y
constexpr size_t kViewType = 0x16e0;      // byte compared against 1 and 2 throughout the builder
constexpr size_t kViewSomething = 0x17e0; // pointer; null or +0x10 == 3 returns an empty mask

// Frame object (fourth argument).
constexpr size_t kFrameTargetA = 0x90;
constexpr size_t kFrameTargetB = 0xa0;
constexpr size_t kFrameTargetC = 0x110;
constexpr size_t kFrameKind334 = 0x334;   // RR requires != 0x37
constexpr size_t kFrameMode = 0xf90;      // 3 skips most of the builder
constexpr size_t kFrameKind = 0xf94;      // RR requires < 2, or 4/5 with targets bound

// Settings values, as image RVAs: Developer/FeatureToggles/Antialiasing and AntialiasingSuppressed.
constexpr uintptr_t kRvaAntialiasing = 0x32f7a38;
constexpr uintptr_t kRvaAntialiasingSuppressed = 0x32f7a70;

using BuildFeatureMaskFn = uint64_t* (*)(void* aRenderer, uint64_t* aOutMask, void* aView, void* aFrame);

const RED4ext::v1::Sdk* g_sdk = nullptr;
RED4ext::v1::PluginHandle g_handle = nullptr;
BuildFeatureMaskFn g_original = nullptr;
uintptr_t g_imageBase = 0;

std::mutex g_mutex;
std::unordered_map<uintptr_t, std::string> g_lastLine;
uint64_t g_calls = 0;

void Log(const std::string& aText)
{
    if (g_sdk && g_sdk->logger)
    {
        g_sdk->logger->Info(g_handle, aText.c_str());
    }
}

uintptr_t ResolveByHash(uint32_t aHash)
{
    using ResolveFn = uintptr_t (*)(uint32_t);
    const HMODULE red4ext = GetModuleHandleW(L"RED4ext.dll");
    const auto resolve =
        red4ext ? reinterpret_cast<ResolveFn>(GetProcAddress(red4ext, "RED4ext_ResolveAddress")) : nullptr;
    return resolve ? resolve(aHash) : 0;
}

struct Snapshot
{
    uint64_t mask0 = 0;
    uint64_t mask1 = 0;
    uint8_t slDlssA = 0, slDlssB = 0, slRr = 0, slGate = 0, slRr1 = 0, slRr2 = 0;
    int32_t rect[4] = {};
    uint8_t viewType = 0;
    uintptr_t viewSomething = 0;
    int32_t viewSomething10 = -1;
    uintptr_t targetA = 0, targetB = 0, targetC = 0;
    int32_t kind334 = 0, mode = 0, kind = 0;
    uint8_t aa = 0, aaSuppressed = 0;
};

// Plain reads only, so the whole thing can sit inside one SEH frame.
bool Capture(void* aRenderer, uint64_t* aMask, void* aView, void* aFrame, Snapshot& aOut)
{
    __try
    {
        const auto view = reinterpret_cast<uintptr_t>(aView);
        const auto frame = reinterpret_cast<uintptr_t>(aFrame);
        aOut.mask0 = aMask[0];
        aOut.mask1 = aMask[1];

        const auto sl = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(aRenderer) + kRendererStreamline);
        if (sl)
        {
            aOut.slDlssA = *reinterpret_cast<uint8_t*>(sl + kSlDlssA);
            aOut.slDlssB = *reinterpret_cast<uint8_t*>(sl + kSlDlssB);
            aOut.slRr = *reinterpret_cast<uint8_t*>(sl + kSlRrAvailable);
            aOut.slGate = *reinterpret_cast<uint8_t*>(sl + kSlLoadedGate);
            aOut.slRr1 = *reinterpret_cast<uint8_t*>(sl + kSlLoadedRr1);
            aOut.slRr2 = *reinterpret_cast<uint8_t*>(sl + kSlLoadedRr2);
        }
        if (view)
        {
            for (int i = 0; i < 4; ++i)
            {
                aOut.rect[i] = *reinterpret_cast<int32_t*>(view + kViewRect + 4 * i);
            }
            aOut.viewType = *reinterpret_cast<uint8_t*>(view + kViewType);
            aOut.viewSomething = *reinterpret_cast<uintptr_t*>(view + kViewSomething);
            if (aOut.viewSomething)
            {
                aOut.viewSomething10 = *reinterpret_cast<int32_t*>(aOut.viewSomething + 0x10);
            }
        }
        if (frame)
        {
            aOut.targetA = *reinterpret_cast<uintptr_t*>(frame + kFrameTargetA);
            aOut.targetB = *reinterpret_cast<uintptr_t*>(frame + kFrameTargetB);
            aOut.targetC = *reinterpret_cast<uintptr_t*>(frame + kFrameTargetC);
            aOut.kind334 = *reinterpret_cast<int32_t*>(frame + kFrameKind334);
            aOut.mode = *reinterpret_cast<int32_t*>(frame + kFrameMode);
            aOut.kind = *reinterpret_cast<int32_t*>(frame + kFrameKind);
        }
        aOut.aa = *reinterpret_cast<uint8_t*>(g_imageBase + kRvaAntialiasing);
        aOut.aaSuppressed = *reinterpret_cast<uint8_t*>(g_imageBase + kRvaAntialiasingSuppressed);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool Bit(const Snapshot& aSnap, uint32_t aIndex)
{
    const uint64_t word = (aIndex >> 6) == 0 ? aSnap.mask0 : aSnap.mask1;
    return (word >> (aIndex & 0x3f)) & 1;
}

std::string Describe(const Snapshot& s)
{
    const bool rectEmpty = s.rect[0] >= s.rect[2] || s.rect[1] >= s.rect[3];
    // Kinds 4 and 5 qualify when either target is bound; every other kind must be below 2.
    const bool kindSpecial = (s.kind == 4 || s.kind == 5) && (s.targetA || s.targetB);
    const bool kindOk = s.kind < 2 || kindSpecial;

    char buf[768];
    std::snprintf(buf, sizeof(buf),
                  "RR=%d bit55=%d | dlssBytes=%d/%d rrAvail=%d loaded=%d/%d/%d | rect=%d,%d-%d,%d empty=%d | "
                  "kind334=%d(!=0x37:%d) kind=%d(ok:%d) mode=%d targets=%d%d%d | aa=%d suppressed=%d | "
                  "viewType=%d viewPtr=%d/%d | mask=%016llx%016llx",
                  Bit(s, kBitRayReconstruction), Bit(s, kBitDlssWrapperSecond), s.slDlssA, s.slDlssB, s.slRr,
                  s.slGate, s.slRr1, s.slRr2, s.rect[0], s.rect[1], s.rect[2], s.rect[3], rectEmpty, s.kind334,
                  s.kind334 != 0x37, s.kind, kindOk, s.mode, s.targetA != 0, s.targetB != 0, s.targetC != 0, s.aa,
                  s.aaSuppressed, s.viewType, s.viewSomething != 0, s.viewSomething10,
                  static_cast<unsigned long long>(s.mask1), static_cast<unsigned long long>(s.mask0));
    return buf;
}

// ---- Streamline constants, logged frame by frame around a Photo Mode capture ----

// The function that fills and sends sl::Constants (0x78933c on 2.31). Its first argument is the
// Streamline manager; the struct it sends lives at manager+0x200 and follows sl_consts.h exactly.
constexpr uint32_t kHashSetConstants = 2155221092;
constexpr uint8_t kSetConstantsPrologue[] = {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8d, 0x6c, 0x24, 0xc9};

constexpr size_t kConsts = 0x200;             // sl::Constants, 32-byte BaseStructure header first
constexpr size_t kViewToClip = 0x20;          // float4x4, rows
constexpr size_t kClipToPrevClip = 0xe0;      // float4x4
constexpr size_t kJitter = 0x160;             // float2
constexpr size_t kMvecScale = 0x168;          // float2
constexpr size_t kPinhole = 0x170;            // float2
constexpr size_t kCameraPos = 0x178;          // float3
constexpr size_t kFov = 0x1b0;                // float
constexpr size_t kAspect = 0x1b4;             // float
constexpr size_t kFlags = 0x1bc;              // depthInverted, cameraMotionIncluded, mv3D, reset
constexpr size_t kMgrHistoryValid = 0x1f0;    // reset is sent as !this
constexpr size_t kMgrFlag1f2 = 0x1f2;

using SetConstantsFn = uint64_t (*)(void* aManager, uint32_t aFrame);
SetConstantsFn g_originalSetConstants = nullptr;

std::atomic<int> g_constantsBudget{0};    // lines left to log; refilled while a capture frame is seen
std::atomic<int> g_lastKind{-1};
std::atomic<uint64_t> g_constantsCalls{0};

struct ConstantsSnapshot
{
    float viewToClip[16] = {};
    float clipToPrev[16] = {};
    float jitter[2] = {}, mvec[2] = {}, pinhole[2] = {}, pos[3] = {};
    float fov = 0, aspect = 0;
    uint8_t flags[4] = {};
    uint8_t historyValid = 0, flag1f2 = 0, dlssA = 0, dlssB = 0, rrAvail = 0;
};

bool ReadConstants(void* aManager, ConstantsSnapshot& aOut)
{
    __try
    {
        const auto m = reinterpret_cast<uintptr_t>(aManager);
        const auto c = m + kConsts;
        std::memcpy(aOut.viewToClip, reinterpret_cast<void*>(c + kViewToClip), 64);
        std::memcpy(aOut.clipToPrev, reinterpret_cast<void*>(c + kClipToPrevClip), 64);
        std::memcpy(aOut.jitter, reinterpret_cast<void*>(c + kJitter), 8);
        std::memcpy(aOut.mvec, reinterpret_cast<void*>(c + kMvecScale), 8);
        std::memcpy(aOut.pinhole, reinterpret_cast<void*>(c + kPinhole), 8);
        std::memcpy(aOut.pos, reinterpret_cast<void*>(c + kCameraPos), 12);
        aOut.fov = *reinterpret_cast<float*>(c + kFov);
        aOut.aspect = *reinterpret_cast<float*>(c + kAspect);
        std::memcpy(aOut.flags, reinterpret_cast<void*>(c + kFlags), 4);
        aOut.historyValid = *reinterpret_cast<uint8_t*>(m + kMgrHistoryValid);
        aOut.flag1f2 = *reinterpret_cast<uint8_t*>(m + kMgrFlag1f2);
        aOut.dlssA = *reinterpret_cast<uint8_t*>(m + kSlDlssA);
        aOut.dlssB = *reinterpret_cast<uint8_t*>(m + kSlDlssB);
        aOut.rrAvail = *reinterpret_cast<uint8_t*>(m + kSlRrAvailable);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

uint64_t DetourSetConstants(void* aManager, uint32_t aFrame)
{
    const auto result = g_originalSetConstants(aManager, aFrame);
    const auto call = ++g_constantsCalls;

    const bool baseline = call <= 3 || call % 900 == 0;
    if (!baseline && g_constantsBudget.load() <= 0)
    {
        return result;
    }
    --g_constantsBudget;

    ConstantsSnapshot s;
    if (!ReadConstants(aManager, s))
    {
        return result;
    }
    // Rows: [r][c] = m[r*4+c]. The projection's jitter/offset terms sit in row 2, translation in row 3.
    const auto* v = s.viewToClip;
    const auto* p = s.clipToPrev;
    char buf[640];
    std::snprintf(buf, sizeof(buf),
                  "consts call %llu frame %u kind %d | reset=%d historyValid=%d f1f2=%d camMotion=%d mv3D=%d depthInv=%d | "
                  "dlss=%d/%d rr=%d | jitter=%.4f,%.4f mvec=%.1f,%.1f pinhole=%.4f,%.4f | pos=%.4f,%.4f,%.4f fov=%.4f aspect=%.4f | "
                  "v2c diag=%.4f,%.4f r2=%.5f,%.5f | c2p diag=%.5f,%.5f r2=%.5f,%.5f r3=%.5f,%.5f",
                  static_cast<unsigned long long>(call), aFrame, g_lastKind.load(), s.flags[3], s.historyValid, s.flag1f2,
                  s.flags[1], s.flags[2], s.flags[0], s.dlssA, s.dlssB, s.rrAvail, s.jitter[0], s.jitter[1], s.mvec[0],
                  s.mvec[1], s.pinhole[0], s.pinhole[1], s.pos[0], s.pos[1], s.pos[2], s.fov, s.aspect, v[0], v[5], v[8],
                  v[9], p[0], p[5], p[8], p[9], p[12], p[13]);
    Log(buf);
    return result;
}

// ---- The capture frame executor, dumped every frame while a capture runs ----

// 0x1c6bf10 on 2.31: runs a capture frame. It reads stack arguments up to the seventh, so the hook
// forwards eight 8-byte slots untouched. Its first argument is the executor object. Dumping that
// object each capture frame and diffing the dumps offline shows which fields count samples and passes.
constexpr uint32_t kHashCaptureExecutor = 1857241502;
constexpr uint8_t kCaptureExecutorPrologue[] = {0x4c, 0x89, 0x4c, 0x24, 0x20, 0x4c, 0x89, 0x44, 0x24, 0x18};
constexpr size_t kExecutorDumpBytes = 0x300;
constexpr uintptr_t kRvaRendererGlobal = 0x3427c00;

using CaptureExecutorFn = uint64_t (*)(void* aExecutor, void* aContext, void* aArg3, void* aArg4, uint64_t aArg5,
                                       uint64_t aArg6, uint64_t aArg7, uint64_t aArg8);
CaptureExecutorFn g_originalExecutor = nullptr;
std::atomic<uint64_t> g_executorCalls{0};

bool StreamlineInCaptureMode()
{
    __try
    {
        const auto renderer = *reinterpret_cast<uintptr_t*>(g_imageBase + kRvaRendererGlobal);
        if (!renderer)
        {
            return false;
        }
        const auto sl = *reinterpret_cast<uintptr_t*>(renderer + kRendererStreamline);
        return sl && *reinterpret_cast<uint8_t*>(sl + kSlDlssA) == 1 && *reinterpret_cast<uint8_t*>(sl + kSlDlssB) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool DumpBytes(void* aAt, size_t aLen, char* aOut)
{
    static const char kHex[] = "0123456789abcdef";
    __try
    {
        const auto* p = static_cast<uint8_t*>(aAt);
        for (size_t i = 0; i < aLen; ++i)
        {
            aOut[i * 2] = kHex[p[i] >> 4];
            aOut[i * 2 + 1] = kHex[p[i] & 0xf];
        }
        aOut[aLen * 2] = 0;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

uint64_t DetourCaptureExecutor(void* aExecutor, void* aContext, void* aArg3, void* aArg4, uint64_t aArg5,
                               uint64_t aArg6, uint64_t aArg7, uint64_t aArg8)
{
    const auto result = g_originalExecutor(aExecutor, aContext, aArg3, aArg4, aArg5, aArg6, aArg7, aArg8);
    const auto call = ++g_executorCalls;
    const bool capturing = StreamlineInCaptureMode();
    if (capturing)
    {
        g_constantsBudget = 90;
    }
    if (!capturing && g_constantsBudget.load() <= 0 && call % 1800 != 1)
    {
        return result;
    }
    static char hex[kExecutorDumpBytes * 2 + 1];
    if (aExecutor && DumpBytes(aExecutor, kExecutorDumpBytes, hex))
    {
        char head[160];
        std::snprintf(head, sizeof(head), "exec call %llu capturing %d exec %p ctx %p a3 %p a4 %p | ",
                      static_cast<unsigned long long>(call), capturing, aExecutor, aContext, aArg3, aArg4);
        Log(std::string(head) + hex);
    }
    return result;
}

uint64_t* DetourBuildFeatureMask(void* aRenderer, uint64_t* aOutMask, void* aView, void* aFrame)
{
    const auto result = g_original(aRenderer, aOutMask, aView, aFrame);

    Snapshot snap;
    if (Capture(aRenderer, aOutMask, aView, aFrame, snap))
    {
        g_lastKind = snap.kind;
        if (snap.kind == 4)
        {
            g_constantsBudget = 90;  // keeps logging for a stretch after the last capture frame
        }
        std::string line = Describe(snap);
        std::lock_guard lock(g_mutex);
        ++g_calls;
        const auto key = reinterpret_cast<uintptr_t>(aView);
        auto& last = g_lastLine[key];
        if (last != line)
        {
            last = line;
            char head[64];
            std::snprintf(head, sizeof(head), "view %p call %llu: ", aView, static_cast<unsigned long long>(g_calls));
            Log(head + line);
        }
    }
    return result;
}

void Install()
{
    g_imageBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto target = ResolveByHash(kHashBuildFeatureMask);
    if (!target)
    {
        Log("feature mask builder did not resolve - no address database for this build, nothing hooked");
        return;
    }
    if (std::memcmp(reinterpret_cast<void*>(target), kPrologue, sizeof(kPrologue)) != 0)
    {
        Log("feature mask builder does not start with the expected bytes - not 2.31, nothing hooked");
        return;
    }
    if (target - g_imageBase != 0x1d49540)
    {
        Log("feature mask builder resolved to an unexpected address - not 2.31, nothing hooked");
        return;
    }
    if (!g_sdk->hooking->Attach(g_handle, reinterpret_cast<void*>(target), &DetourBuildFeatureMask,
                                reinterpret_cast<void**>(&g_original)))
    {
        Log("hook attach failed - nothing hooked");
        return;
    }
    Log("photo mode RR probe hooked the feature mask builder - one line per view whenever its RR inputs change");

    const auto constants = ResolveByHash(kHashSetConstants);
    const auto* constantsBytes = reinterpret_cast<uint8_t*>(constants);
    const bool constantsHookedAlready = constants && (constantsBytes[0] == 0xe9 || constantsBytes[0] == 0xff);
    if (!constants || constants - g_imageBase != 0x78933c ||
        (!constantsHookedAlready &&
         std::memcmp(reinterpret_cast<void*>(constants), kSetConstantsPrologue, sizeof(kSetConstantsPrologue)) != 0))
    {
        Log("Streamline constants function did not match 2.31 - constants logging off");
        return;
    }
    if (!g_sdk->hooking->Attach(g_handle, reinterpret_cast<void*>(constants), &DetourSetConstants,
                                reinterpret_cast<void**>(&g_originalSetConstants)))
    {
        Log("constants hook attach failed - constants logging off");
        return;
    }
    Log("hooked the Streamline constants function - every call is logged while a capture frame has been seen recently");
}

void InstallExecutorHook()
{
    const auto executor = ResolveByHash(kHashCaptureExecutor);
    if (!executor || executor - g_imageBase != 0x1c6bf10 ||
        std::memcmp(reinterpret_cast<void*>(executor), kCaptureExecutorPrologue, sizeof(kCaptureExecutorPrologue)) != 0)
    {
        Log("capture executor did not match 2.31 - executor dumps off");
        return;
    }
    if (!g_sdk->hooking->Attach(g_handle, reinterpret_cast<void*>(executor), &DetourCaptureExecutor,
                                reinterpret_cast<void**>(&g_originalExecutor)))
    {
        Log("capture executor hook attach failed - executor dumps off");
        return;
    }
    Log("hooked the capture executor - its object is dumped every capture frame");
}
} // namespace

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"PhotoModeRRProbe";
    aInfo->author = L"Spuddeh";
    aInfo->version = RED4EXT_V1_SEMVER(0, 3, 0);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle,
                                        RED4ext::v1::EMainReason aReason, const RED4ext::v1::Sdk* aSdk)
{
    if (aReason == RED4ext::v1::EMainReason::Load)
    {
        g_sdk = aSdk;
        g_handle = aHandle;
        Install();
        InstallExecutorHook();
    }
    else if (aReason == RED4ext::v1::EMainReason::Unload)
    {
        if (g_original)
        {
            aSdk->hooking->Detach(aHandle, reinterpret_cast<void*>(g_imageBase + 0x1d49540));
        }
        if (g_originalExecutor)
        {
            aSdk->hooking->Detach(aHandle, reinterpret_cast<void*>(g_imageBase + 0x1c6bf10));
        }
        if (g_originalSetConstants)
        {
            aSdk->hooking->Detach(aHandle, reinterpret_cast<void*>(g_imageBase + 0x78933c));
        }
    }
    return true;
}
