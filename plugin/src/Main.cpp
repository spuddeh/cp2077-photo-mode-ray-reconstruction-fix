// ======================================================================================
// Mod Name: Photo Mode Ray Reconstruction
// Author: Spuddeh
// Description: Keeps Ray Reconstruction on in Photo Mode.
// File Version: 0.1.0
// Credits: RED4ext by WopsS.
// ======================================================================================
//
// Two native Photo Mode handlers switch Ray Reconstruction off: OnPhotoModeOpened, and one branch of
// OnPhotoModeUIVisibilityChanged. Each writes the RR setting off and clears the Streamline manager's
// RR-available byte, which gates the frame's RR feature bit. Removing those two calls from each
// leaves RR as the player had it; frame generation is still switched off and every restore stays.
// Both functions are verified in full before either is written.

#include <Windows.h>
#include <RED4ext/RED4ext.hpp>

#include <cstdint>
#include <cstring>
#include <string>

namespace
{
struct Edit
{
    size_t offset;
    size_t length;
    uint8_t replacement[5];
};

struct Site
{
    const char* name;
    uint32_t hash;
    const uint8_t* expected;  // the function's bytes from its start, as shipped
    size_t expectedLength;
    const Edit* edits;
    size_t editCount;
};

// OnPhotoModeOpened, 0x1d533e0 on 2.31. Bytes 0x00..0x5d: prologue through the open branch.
//   +0x4e  E8 ..  call SetRayReconstruction(0, apply)
//   +0x58  E8 ..  call SetRayReconstructionAvailable(manager, 0)
constexpr uint32_t kHashOpened = 2954628956;
constexpr uint8_t kOpenedBytes[] = {
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x80, 0x39, 0x00, 0x48, 0x8b, 0x15,
    0x0c, 0x48, 0x6d, 0x01, 0x48, 0x8b, 0x9a, 0x58, 0x46, 0x00, 0x00, 0x48, 0x8b, 0xba, 0x68, 0x46,
    0x00, 0x00, 0x74, 0x48, 0x0f, 0xb6, 0x83, 0x0c, 0x04, 0x00, 0x00, 0x33, 0xd2, 0x48, 0x8b, 0xcb,
    0x88, 0x83, 0x0d, 0x04, 0x00, 0x00, 0xe8, 0x2d, 0xc8, 0xbb, 0x00, 0x44, 0x0f, 0xb6, 0x93, 0xea,
    0x03, 0x00, 0x00, 0xb2, 0x01, 0x33, 0xc9, 0x44, 0x88, 0x93, 0xeb, 0x03, 0x00, 0x00, 0xe8, 0x1d,
    0x0d, 0xa9, 0xfe, 0x33, 0xd2, 0x48, 0x8b, 0xcb, 0xe8, 0xe7, 0xc7, 0xbb, 0x00};
constexpr Edit kOpenedEdits[] = {
    {0x4e, 5, {0x90, 0x90, 0x90, 0x90, 0x90}},
    {0x58, 5, {0x90, 0x90, 0x90, 0x90, 0x90}},
};

// OnPhotoModeUIVisibilityChanged, 0x1d537d0 on 2.31. The whole function, 0x51 bytes.
//   +0x3d  E8 ..  call SetRayReconstruction(0, apply)
//   +0x4c  E9 ..  jmp SetRayReconstructionAvailable(manager, 0), a tail call after the epilogue
constexpr uint32_t kHashVisibility = 3553368820;
constexpr uint8_t kVisibilityBytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x80, 0x79, 0x20, 0x00, 0xb2, 0x01, 0x48, 0x8b, 0x05, 0x1d,
    0x44, 0x6d, 0x01, 0x48, 0x8b, 0x98, 0x58, 0x46, 0x00, 0x00, 0x74, 0x1f, 0x0f, 0xb6, 0x8b, 0xeb,
    0x03, 0x00, 0x00, 0xe8, 0x58, 0x09, 0xa9, 0xfe, 0x0f, 0xb6, 0x83, 0xeb, 0x03, 0x00, 0x00, 0x88,
    0x83, 0xea, 0x03, 0x00, 0x00, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3, 0x33, 0xc9, 0xe8, 0x3e, 0x09,
    0xa9, 0xfe, 0x33, 0xd2, 0x48, 0x8b, 0xcb, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xe9, 0x03, 0xc4, 0xbb,
    0x00};
constexpr Edit kVisibilityEdits[] = {
    {0x3d, 5, {0x90, 0x90, 0x90, 0x90, 0x90}},
    {0x4c, 5, {0xc3, 0x90, 0x90, 0x90, 0x90}},  // the stack is already unwound, so ret is exact
};

constexpr Site kSites[] = {
    {"OnPhotoModeOpened", kHashOpened, kOpenedBytes, sizeof(kOpenedBytes), kOpenedEdits, 2},
    {"OnPhotoModeUIVisibilityChanged", kHashVisibility, kVisibilityBytes, sizeof(kVisibilityBytes),
     kVisibilityEdits, 2},
};

const RED4ext::v1::Sdk* g_sdk = nullptr;
RED4ext::v1::PluginHandle g_handle = nullptr;

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
    const auto resolve = red4ext ? reinterpret_cast<ResolveFn>(GetProcAddress(red4ext, "RED4ext_ResolveAddress"))
                                 : nullptr;
    return resolve ? resolve(aHash) : 0;
}

bool WriteBytes(void* aAt, const void* aData, size_t aLen)
{
    DWORD old = 0;
    if (!VirtualProtect(aAt, aLen, PAGE_EXECUTE_READWRITE, &old))
    {
        return false;
    }
    std::memcpy(aAt, aData, aLen);
    VirtualProtect(aAt, aLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), aAt, aLen);
    return true;
}

enum class State
{
    Unpatched,
    Patched,
    Unknown
};

State Inspect(const Site& aSite, const uint8_t* aFn)
{
    if (std::memcmp(aFn, aSite.expected, aSite.expectedLength) == 0)
    {
        return State::Unpatched;
    }
    std::string patched(reinterpret_cast<const char*>(aSite.expected), aSite.expectedLength);
    for (size_t i = 0; i < aSite.editCount; ++i)
    {
        patched.replace(aSite.edits[i].offset, aSite.edits[i].length,
                        reinterpret_cast<const char*>(aSite.edits[i].replacement), aSite.edits[i].length);
    }
    if (std::memcmp(aFn, patched.data(), aSite.expectedLength) == 0)
    {
        return State::Patched;
    }
    return State::Unknown;
}

void Patch()
{
    uint8_t* fns[2] = {};
    State states[2] = {};
    for (size_t s = 0; s < 2; ++s)
    {
        fns[s] = reinterpret_cast<uint8_t*>(ResolveByHash(kSites[s].hash));
        if (!fns[s])
        {
            Log(std::string(kSites[s].name) + " did not resolve - no address database for this build, nothing patched");
            return;
        }
        states[s] = Inspect(kSites[s], fns[s]);
        if (states[s] == State::Unknown)
        {
            Log(std::string(kSites[s].name) + " does not hold the expected bytes - not this game build, nothing patched");
            return;
        }
    }
    if (states[0] == State::Patched && states[1] == State::Patched)
    {
        Log("both handlers are already patched - another copy of this plugin is loaded");
        return;
    }
    for (size_t s = 0; s < 2; ++s)
    {
        if (states[s] == State::Patched)
        {
            continue;
        }
        for (size_t i = 0; i < kSites[s].editCount; ++i)
        {
            const auto& edit = kSites[s].edits[i];
            if (!WriteBytes(fns[s] + edit.offset, edit.replacement, edit.length))
            {
                Log(std::string(kSites[s].name) + " could not be made writable - patch incomplete");
                return;
            }
        }
    }
    Log("patched: Photo Mode no longer switches Ray Reconstruction off");
}
} // namespace

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"PhotoModeRayReconstruction";
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
        Patch();
    }
    return true;
}
