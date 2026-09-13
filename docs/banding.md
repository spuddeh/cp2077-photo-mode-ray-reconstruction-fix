# Banded Photo Mode photos

Some Photo Mode photos show thin horizontal bands with repeated highlights along them, and the screen
flashes streaked, green-noised frames while the photo is taken. This page records what causes it, what
does not, and how it was measured.

**Short version:** it happens when DLSS Ray Reconstruction **preset F** is used for a capture whose
grid (`RayTracing/ReferenceScreenshot/SampleNumber`) is **7 or more**. Preset E does not band at any grid
size tested, up to 16. The capture grid is described in [capture-pipeline.md](capture-pipeline.md).

It is not caused by this plugin. The unmodified game also captures with Ray Reconstruction when it is on,
and banded at a grid of 12 with no plugin installed (reported).

## How photos were scored (measured)

Photos in each comparison were taken from the same camera position. Each saved PNG was converted to
greyscale and scored:

1. For every pair of neighbouring rows, take the median absolute brightness step across the width.
   This gives one value per row boundary and ignores isolated edges in the scene.
2. At every multiple of 64 (rows 64, 128, 192 ...), take the largest of the three values up to that
   row, and subtract the average of the values 10 rows above and 8 rows below.
3. The score is the mean of those differences over the image.

A clean photo scores close to zero. The same measurement on columns scores close to zero for every
photo, banded or not.

Across all the photos in this investigation the two groups do not overlap:

| | Score range |
| --- | --- |
| Every photo with visible bands | 1.33 to 6.60 |
| Every photo without | -0.31 to 0.36 |

## Results

Game 2.31, path tracing, DLSS Super Resolution Balanced unless stated. The capture grid N of every RR
photo was confirmed from the probe log, except the 1920x1080 photo on 310.9.1, where N is the value set
in game. The DLSS DLL version and preset were set through the NVIDIA
driver override and are **reported**, not read from files.

| RR DLLs | Preset | N | Other change | Score | Bands |
| --- | --- | --- | --- | --- | --- |
| 310.9.1 | F | 5 (three photos) | | -0.19 to 0.07 | no |
| 310.9.1 | F | 6 | | 0.00 | no |
| 310.9.1 | F | 7 | | 2.81 | **yes** |
| 310.9.1 | F | 8 | | 2.10 | **yes** |
| 310.9.1 | F | 7 | `TileSize` 128 | 2.81 | **yes** |
| 310.9.1 | F | 7 | Super Resolution DLAA | 3.26 | **yes** |
| 310.9.1 | F | 7 | Super Resolution Performance | 2.83 | **yes** |
| 310.9.1 | F | 7 | 1920x1080 output | 6.60 | **yes** |
| 310.9.1 | F | 7 | RR off in the graphics settings | -0.05 | no |
| 310.9.1 | E | 16 | | 0.36 | no |
| 310.9.0 | F | 6 | | -0.31 | no |
| 310.9.0 | F | 7 | | 2.86 | **yes** |
| 310.9.0 | F | 16 | | 1.33 | **yes** |
| 310.9.0 | E | 16 | | -0.10 | no |
| 310.7.129 | E | 7, 8, 9, 10, 16 | 1920x1080 output | 0.20 to 0.30 | no |
| 310.7.129 | E | 16 | | 0.07 | no |

Output is 2560x1440 unless stated. `TileSize` was changed from the CET console, and reading it back
returned 256 before the change and 128 after.

An earlier test with no plugin installed, the same spot and a grid of 12 banded, and the same setup at 5
did not (reported; those photos were not scored). Frame generation switched back on in the graphics
settings still banded at 7 (reported).

## What the bands look like (measured)

- **Rows only, every 64 output pixels.** Band centres sit on rows 64, 128, 192 and so on. The spacing is
  64 at N = 7, 8 and 16, in DLAA, Balanced and Performance, and at both 2560x1440 and 1920x1080, so it is
  fixed in output pixels rather than tied to the grid, the render scale or the image height. At 1080p,
  rows on multiples of 48 that are not also multiples of 64 score 0.09.
- **3 to 6 rows wide** at half strength.
- **Detail, not blur.** In the N=7 photos, band rows carry 16 to 50% more horizontal detail than the rows
  around them, which fits the repeated highlights seen by eye. In the N=16 photo the amount is unchanged.
  In neither case are they blurred copies of the clean rows.
- **Only the top half at 2560x1440.** In every banded 1440p photo the bands run from row 64 to row 704
  and there is no signal from row 768 down. In the 1920x1080 photo they run the full height, rows 64 to
  960. Why is not known.
- **The damage builds across the capture.** A single capture frame's RR output, inspected in an Nsight
  Graphics capture, is clean.

## What this rules out

| Suspect | Result |
| --- | --- |
| The capture sweep itself | The same 16x16 sweep, 520 frames, is clean with preset E |
| `RayTracing/ReferenceScreenshot/TileSize` | Halving it leaves the bands on the same rows at the same strength |
| DLSS Super Resolution mode | DLAA and Performance band on the same rows |
| Output resolution | 1080p bands on the same 64-pixel spacing |
| Frame generation | Photo Mode switches it off for the capture either way; bands with the setting on (reported) |
| DLL build | 310.9.0 and 310.9.1 both band on preset F and are clean on preset E |
| This plugin | The unmodified game bands too (reported) |

Switching RR off removes the bands, but that capture also runs a different DLSS mode and sends no
Streamline constants, so it changes more than the denoiser.

## Workarounds

- Use Ray Reconstruction preset E.
- Or keep `SampleNumber` at 6 or less. The game default is 5. Some mods set it higher; resetting it
  before a photo works:

  ```lua
  GameOptions.SetInt("RayTracing/ReferenceScreenshot", "SampleNumber", 5)
  ```

- Resetting RR history on every capture frame softens the bands but makes the capture boil, and is not
  used.

## Open questions

1. What inside preset F reacts to a capture grid of 7 or more. From outside, the sweep at 6 and at 7
   differs only in step size (1/6 against 1/7 pixel) and length (36 against 49 samples per sweep).
2. Why the bands sit on a 64-pixel row grid.
3. Why they stop halfway down a 1440p photo but not a 1080p one.
4. Whether other preset F builds behave the same, and whether presets other than E and F band.
