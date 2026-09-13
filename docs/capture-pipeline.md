# The Photo Mode capture pipeline

What happens between pressing Take Photo and the saved PNG. Every temporal effect in a capture frame
(DLSS upscaling, Ray Reconstruction, NRD, TAA) sees the same sequence, so anything that changes Photo
Mode rendering has to respect it.

## 1. The engine's capture system (static)

Photo Mode has no screenshot code of its own. It drives the renderer's general capture system, whose
RTTI types describe it:

| Type | Carries |
| --- | --- |
| `rendScreenshotMode` | `NONE 0`, `NORMAL 1`, `NORMAL_MULTISAMPLE 2`, `LAYERED 4`, `HIGH_RESOLUTION 5`, `HIGH_RESOLUTION_LAYERED 6` |
| `rendSingleScreenShotData` | mode, output path, resolution preset, resolution multiplier, EMM modes, `forceLOD0`, `ESaveFormat` |
| `ESaveFormat` | `SF_PNG 2`, `SF_EXR 32`, `SF_PNG_AND_EXR 34` |
| `rendCaptureParameters` | mode, recording flags, output path, resolution and multiplier, FOV multiplier, save format |

## 2. The request (static)

- The input actions `TakeScreenshot` and `TakeScreenshot_HiRes` are registered beside the Photo Mode
  render handlers. Their jobs run `0x1d53070` and `0x1d53270`.
- Both build a request with **mode `4`, `LAYERED`**, and pass it to the renderer, either through
  `0x290f4ec` (hash `2124950529`) on the renderer global `0x3427c00`, or through `0x290f8f0` (hash
  `1122509968`). `0x1d53070` first builds an output path under `screenshots` when none is set.
- `0x290f4ec` creates one output per bound frame target (`frame+0x70`, `+0xb0`, `+0xc0`, `+0xd0`,
  `+0x100`, `+0x30`), with suffixes such as `_a10`: the layers of a LAYERED capture.
- The save format field of this request is not identified. The PNGs Photo Mode produces are 8-bit RGBA
  with no colour-space chunk, so they are SDR (measured on the saved files).

## 3. The capture finaliser (static and measured)

`0x1c6bf10` (hash `1857241502`) runs once per photo, after the capture frames. It:

- reads `Editor/Recording/HighResolutionScreenshot_MS_Count` (default 8, clamped to 4 to 32). **That
  option does not affect a Photo Mode capture**: 8 and 16 give the same frame count and grid (measured).
- sizes three targets from resolution times multiplier (`0x1c6bc80`)
- saves the frame's `+0xf94` and sets it to 4
- reads stack arguments up to the seventh. A hook that forwards only the four register arguments
  crashes the game on the first photo.

## 4. What a capture renders, frame by frame (measured)

From the probe's Streamline constants log for every frame of a photo:

| | Framing in Photo Mode | Capture frames |
| --- | --- | --- |
| frame `+0xf94` | 0 | 4 |
| Streamline manager `+0x3e8` / `+0x3e9` | 0 / 1 | 1 / 0 |
| Ray Reconstruction, unmodified game | **off** (NRD) | **on** |
| Ray Reconstruction, with the plugin | on | on |
| Frames | | **8 settle frames, then two NxN sweeps**: 8 + 2N² |
| Jitter | the game's normal sequence | settle at (0, 0), then an ordered grid |
| `reset` sent to Streamline | | 0 on every frame |
| Camera position, FOV, `clipToPrevClip` | | unchanged; `clipToPrevClip` is identity |
| Motion vector scale | | 1, 1 |
| Frame index | | consecutive through the capture; live frames continue from where the capture began |

**The grid.** For sweep sample `i` (0 to N²-1), the jitter sent to Streamline is:

```text
x = -(i mod N) / N
y = -(i div N) / N
```

x steps from 0 towards -(N-1)/N along a row, then y moves one step and x starts again. Both sweeps are
identical. Every capture in the retained logs (N = 6, 7 and 16) matches this exactly, and the frame
counts match 8 + 2N² for every N tested:

| N | Capture frames |
| --- | --- |
| 5 | 58 |
| 6 | 80 |
| 7 | 106 |
| 8 | 136 |
| 9 | 170 |
| 10 | 208 |
| 12 | 296 |
| 16 | 520 |

**N is `RayTracing/ReferenceScreenshot/SampleNumber`** (game default 5). Changing it from the CET
console changes the grid of the next photo:

```lua
GameOptions.SetInt("RayTracing/ReferenceScreenshot", "SampleNumber", 5)
```

`RayTracing/ReferenceScreenshot/TileSize` (default 256) is a separate option. Halving it made no
difference to the banding, and what it controls is not known.

**Where the jitter comes from (static).** `0x788a9c` (hash `3992265634`) fills each frame's constants
from the render view: jitter x = `view+0x3e0`, jitter y = `-view+0x3e4`. `0x78933c` then copies them into
`sl::Constants` and calls `slSetConstants`. The grid is written into the view's jitter fields earlier;
the writer is not located.

**The screen flash (reported).** During a photo the screen shows two bright, streaked frames with
green noise between the streaks, one per sweep. On a clean photo there is no visible flash.

## 5. Ray Reconstruction during the capture (measured)

In the unmodified game, with RR on in the settings, capture frames have the RR-available byte set and
frame feature bit `0x46` set: **the game frames with NRD and captures with RR.** How that is switched is
covered in [photo-mode-rr-swap.md](photo-mode-rr-swap.md).

With RR switched off in the graphics settings, capture frames run with RR unavailable and the DLSS mode
bytes at `0/1` instead of `1/0`, and no Streamline constants were logged for them even though the probe's
constants logging was active (it switches on for 90 calls whenever a kind-4 frame is seen). A missing
line is weaker evidence than a logged value, so "not sent" is inferred. Either way, that capture differs
in more than the denoiser.

## 6. IGPT's capture path (measured, without this plugin)

IGPT (In-Game Photomode Tweaks) moves Space from `PhotoModeTakeScreenshot_HiRes` to its own action,
which calls its native `TakeFancyScreenshot(resolution, scale, format)`. Its settings (resolution,
multiplier, force LOD0, PNG or EXR) are the fields of `rendSingleScreenShotData`, so it most likely
issues that request (inferred; the plugin carries no type name to confirm it).

Two IGPT photos (1920x1080 x2 and 2560x1440 x1), with this plugin not installed:

- capture frames have `+0xf94 = 4` and the DLSS mode bytes at `1/0`, like the vanilla capture, at IGPT's
  render resolution, with a different target layout
- **RR stays off for the whole capture**: the UI visibility restore never runs, so the photo is an NRD
  photo
- about 20 s at 1080p x2, 10 s at 1440p x1
- neither photo bands
- IGPT's PNG output is 8-bit RGBA with no colour-space chunk, so SDR

No constants lines were logged during these captures either, so their jitter is not known. Whether IGPT captures with RR while this plugin is installed is not
tested.

## Open questions

1. What writes the capture grid into `view+0x3e0` / `+0x3e4`.
2. How the samples combine: which target accumulates them, with what weights, and whether both sweeps
   count.
3. What the DLSS mode bytes at `1/0` change beyond selecting a mode.
4. What a LAYERED capture writes besides the colour image, and why Photo Mode uses it rather than
   `NORMAL_MULTISAMPLE`.
5. Whether IGPT's EXR output holds values above SDR white.
