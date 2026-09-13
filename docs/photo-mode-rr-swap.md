# How Photo Mode switches Ray Reconstruction off

Since patch 2.3, Ray Reconstruction (RR) looks broken in Photo Mode. It is not broken: Photo Mode
switches it off and denoises with NRD instead. This page follows that from the engine options down to
the four instructions the plugin removes.

## 1. Engine options that change on entry (measured)

`GameOptions.Dump()` from the CET console in gameplay and again inside Photo Mode, diffed across all
1525 options, with RR on and no plugin:

| Option | Gameplay | Photo Mode |
| --- | --- | --- |
| `Developer/FeatureToggles/DLSSD` | `true` | `false` |
| `RayTracing/EnableNRD` | `false` | `true` |
| `DLSSFrameGen/Enable` | `true` | `false` |
| `SnapToTerrainIk/Enabled` | `true` | `false` |

The settings-menu variable `/graphics/presets/DLSS_D` also reads `false` inside Photo Mode. All of them
return to their gameplay values on exit. `Developer/FeatureToggles/PathTracingForPhotoMode` reads
`false` in both states, so the switch is not tied to Photo Mode's own path tracing option.

A `ReLAX`/`ReBLUR` group also moved in that test. Those values came from Ultra+, which applies its NRD
denoiser preset when Photo Mode opens; they are not the game's.

No script writes these. The decompiled Photo Mode controllers contain no settings writes, and
`gamePhotoModeSystem` exposes only `CanPhotoModeBeEnabled`, `IsExitLocked`, `IsPhotoModeActive`,
`GetCameraLocation` and `UnlockPhotoModeItem`. The switch happens in native code.

## 2. Forcing the options back on does not bring RR back (measured)

Setting `/graphics/presets/DLSS_D` to true and calling `ConfirmChanges()` inside Photo Mode flips
`DLSSD` back to true and `EnableNRD` to false, but the image fills with noise that does not clear on a
still camera. Nsight Graphics captures of the same spot show why:

| | Gameplay, RR on | Photo Mode, options forced back on |
| --- | --- | --- |
| Feature evaluated | `NGX_FeatureID_13030` | `NGX_FeatureID_106333` |
| `NGX_Parameter::Set` calls before evaluate | 140 | 40 |
| Preset hints set | `RayReconstruction.Hint.Render.Preset.*` | `DLSS.Hint.Render.Preset.*` |
| `DiffuseAlbedo`, `SpecularAlbedo`, `GBuffer.Normals`, `SpecularHitDistance` | set | not set |
| `WorldToViewMatrix`, `ViewToClipMatrix` | set | not set |

Photo Mode evaluates plain DLSS Super Resolution. With NRD also off, DLSS upscales undenoised path
tracing. The options are not what decides which feature runs.

## 3. Where the choice is made (static)

The game drives DLSS through Streamline: `Cyberpunk2077.exe` imports `slEvaluateFeature`, `slSetTag`
and `slSetConstants` from `sl.interposer.dll` and nothing from `nvngx`.

| What | RVA | Hash |
| --- | --- | --- |
| The only `slEvaluateFeature` call, in a wrapper that also makes ten `slSetTag` calls | `0x1d4fdc0` (+`0x605`) | `2331188779` |
| Its only direct caller, the renderer's DLSS pass | `0x37d5c4` (+`0xcf1`) | `4233240033` |
| The only `slSetConstants` call | `0x78933c` (+`0x25e`) | `2155221092` |

`0x1d4fdc0` tests bit `0x46` of the frame's 128-bit render feature mask (`0x23af5c(ctx, 0x46)`, hash
`3013809689`). When the bit is set it tags seven more resources, sets RR options and evaluates feature
`0x3e9`. When it is clear it skips those tags, sets plain DLSS options and evaluates feature `0`.
**Bit `0x46` is Ray Reconstruction for the frame.**

The mask is built by `0x1d49540` (hash `137310724`). It sets bit `0x46` at `0x1d49bc5` when all of
these hold:

| Condition | Meaning |
| --- | --- |
| Streamline manager `+0x3e9` or `+0x3e8` set | DLSS enabled |
| `0x1d41170()`: RR feature loaded (`0x7896c8(mgr, 1)`) and manager `+0x3ea` set | RR available |
| View rectangle (`view+0x14`) not empty | |
| `frame+0x334 != 0x37` | a frame kind is excluded |
| `frame+0xf94 < 2`, or kind 4/5 with certain targets bound | frame kind |
| `Developer/FeatureToggles/Antialiasing` on and `AntialiasingSuppressed` off | anti-aliasing active |

The Streamline manager is `renderer+0x4658`, where the renderer is the global at `0x3427c00`.

## 4. The gate that changes (measured)

The probe in `probe/` hooks the mask builder and logs every condition's inputs per view. Entering
Photo Mode changes exactly one input: manager `+0x3ea` goes from 1 to 0, and bit `0x46` with it. The
frame kind fields, view rectangle, DLSS bytes and anti-aliasing settings are identical in gameplay and
Photo Mode. Setting `DLSS_D` with `ConfirmChanges()` inside Photo Mode leaves `+0x3ea` at 0, so the
options alone cannot bring RR back.

## 5. The handlers that write it (static)

| Function | RVA | Hash | Does |
| --- | --- | --- | --- |
| `SetRayReconstructionAvailable(mgr, bool)` | `0x290fc24` | `3650751457` | if `0x7896c8(mgr, 1)` (RR loaded), `mgr+0x3ea = bool` |
| `SetFrameGenerationFlag(mgr, bool)` | `0x290fc48` | `1077743266` | if `0x7896c8(mgr, 2)`, `mgr+0x40c = bool` |
| `SetRayReconstruction(enable, apply)` | `0x7e4150` | `1902975224` | writes the RR settings variable, then `DLSSD` (`0x32f8178`) = `enable` and `EnableNRD` (`0x32f76c0`) = `!enable` |
| `OnPhotoModeOpened` | `0x1d533e0` | `2954628956` | below |
| `OnPhotoModeUIVisibilityChanged` | `0x1d537d0` | `3553368820` | below |

All five function names are descriptive, not symbols from the game. The two handlers are native
functions the engine runs through job tables (`0x31333d8` and `0x3133328`). No script calls them, and
the plugin reaches them through the address database, not through RTTI. That `+0x40c`
is the frame generation flag is inferred from the feature index and the `DLSSFrameGen/Enable` change.

`OnPhotoModeOpened` (event byte `[rcx]` set = opened):

```text
opened:
  +0x24  mgr+0x40d = mgr+0x40c            save frame generation flag
  +0x36  call 0x290fc48(mgr, 0)           frame generation off
  +0x3b  mgr+0x3eb = mgr+0x3ea            save RR-available
  +0x4e  call 0x7e4150(0, 1)              RR setting off, DLSSD off, EnableNRD on
  +0x58  call 0x290fc24(mgr, 0)           RR-available off
  +0x5d  renderer[+0x4668]+0x62 = 1
closed:
         renderer[+0x4668]+0x62 = 0
         mgr+0x40c = mgr+0x40d            restore frame generation flag
         call 0x7e4150(mgr+0x3eb, 1)      restore RR setting
         mgr+0x3ea = mgr+0x3eb            restore RR-available
```

`OnPhotoModeUIVisibilityChanged` (event byte `+0x20`), the whole function is `0x51` bytes:

```text
flag set:
  +0x23  call 0x7e4150(mgr+0x3eb, 1)      restore RR setting
  +0x2f  mgr+0x3ea = mgr+0x3eb            restore RR-available
flag clear:
  +0x3d  call 0x7e4150(0, 1)              RR off
  +0x4c  jmp  0x290fc24(mgr, 0)           RR-available off, as a tail call
```

Across the whole executable, the other callers of these setters are `0x219730` and `0x21bf9c` (renderer
setup, passing a stored setting) for `0x290fc24`, and `0x7e4704` and `0x20ece64` for `0x7e4150`.
`0x20ece64` switches RR off alongside two other features; what calls it is not identified. The only
other direct write to `+0x3ea` is `0xdc8920`, which sets it to 1. None of these are in the Photo Mode
code.

**Which visibility state is "flag set" is inferred.** Vanilla Photo Mode frames with RR off and captures
with RR on (see [capture-pipeline.md](capture-pipeline.md)). Apart from closing Photo Mode, this handler is the only
Photo Mode code that restores RR, which fits the restore firing as the UI hides for a photo. The event's value was not read.

## 6. What the plugin patches (static)

**Method:** a RED4ext plugin that patches bytes once, when RED4ext loads it. It does not hook RTTI,
register script functions or install detours, and none of its code runs after load. When the game later
runs the two handlers, it runs them with four instructions replaced:

| Function | Offset | Original | Replacement |
| --- | --- | --- | --- |
| `OnPhotoModeOpened` | `+0x4e` | `call 0x7e4150` | 5 x `nop` |
| `OnPhotoModeOpened` | `+0x58` | `call 0x290fc24` | 5 x `nop` |
| `OnPhotoModeUIVisibilityChanged` | `+0x3d` | `call 0x7e4150` | 5 x `nop` |
| `OnPhotoModeUIVisibilityChanged` | `+0x4c` | `jmp 0x290fc24` | `ret` + 4 x `nop` |

The `jmp` at `+0x4c` comes after `add rsp, 0x20; pop rbx`, so the stack is already unwound and `ret` is
exact. The saves still run, so closing Photo Mode restores the same values as before; with RR never
switched off, they are the values already in place. Frame generation is still switched off in Photo
Mode and restored on exit.

**Safety checks.** Both functions are resolved by hash through `RED4ext_ResolveAddress`, then compared
with their full expected bytes: `0x5d` bytes of `OnPhotoModeOpened` (prologue through the opened
branch) and all `0x51` bytes of `OnPhotoModeUIVisibilityChanged`. Nothing is written unless both match
the original or the already-patched bytes. Otherwise the plugin logs one line and does nothing. The
plugin declares itself runtime-independent, so this byte check is the only version gate.

**Effect (measured).** With the plugin, RR stays on while framing: in Photo Mode `DLSSD` reads true,
`EnableNRD` false, and Nsight captures taken after the plugin was built evaluate the RR feature with RR
preset hints, 140 parameter sets and every guide buffer, as in gameplay. With RR switched off in the graphics settings and the plugin
installed, photos are captured with NRD and come out clean.

**Not tested:** hardware without Ray Reconstruction. Both setters check that their feature is loaded
before writing, so the removed calls should have had no effect there anyway (inferred).

## 7. An approach that was dropped (measured)

A build that also reset RR history on every capture frame softened the banding at a capture grid of
12x12 but made the capture visibly boil. Keeping history is better: with it kept and the default grid of
5x5, a photo taken straight after fast camera movement is clean. The fresh history the unmodified game
gives each capture, by switching RR on for it, is not needed for photo quality.

## Open questions

1. Why the game switches RR off while framing. The banding described in [banding.md](banding.md) does not
   explain it: it depends on the RR preset and the capture grid size, and the default grid is clean.
2. Which UI visibility state sets the handler's flag, and whether hiding the UI while framing in the
   unmodified game brings RR back.
3. What `0x20ece64` is, and when it runs.
