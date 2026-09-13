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

uint64_t* DetourBuildFeatureMask(void* aRenderer, uint64_t* aOutMask, void* aView, void* aFrame)
{
    const auto result = g_original(aRenderer, aOutMask, aView, aFrame);

    Snapshot snap;
    if (Capture(aRenderer, aOutMask, aView, aFrame, snap))
    {
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
}
} // namespace

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"PhotoModeRRProbe";
    aInfo->author = L"Spuddeh";
    aInfo->version = RED4EXT_V1_SEMVER(0, 1, 0);
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
    }
    else if (aReason == RED4ext::v1::EMainReason::Unload && g_original)
    {
        aSdk->hooking->Detach(aHandle, reinterpret_cast<void*>(g_imageBase + 0x1d49540));
    }
    return true;
}
