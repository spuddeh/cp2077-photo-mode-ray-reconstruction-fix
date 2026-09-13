# Technical notes

These pages record what is known about how Cyberpunk 2077 handles Ray Reconstruction in Photo Mode,
how a Photo Mode photo is captured, and why some photos band. They back the plugin in this repo and are
written for modders and reverse engineers. Players only need the main [README](../README.md).

| Page | Covers |
| --- | --- |
| [photo-mode-rr-swap.md](photo-mode-rr-swap.md) | How Photo Mode switches Ray Reconstruction off, where in the executable, and what the plugin patches |
| [capture-pipeline.md](capture-pipeline.md) | What happens between pressing Take Photo and the saved PNG: the request, the frame-by-frame sweep, IGPT's path |
| [banding.md](banding.md) | The horizontal bands in photos: measurements, what does and does not change them, and what is still unknown |
| [probe.md](probe.md) | `PhotoModeRRProbe`, the read-only plugin in `probe/` that produced most of the runtime measurements |

## Scope

- **Game version 2.31.** Every address is a 2.31 RVA (relative to the image base). Every function is
  also given as its RED4ext address-database hash, which is how the plugin and probe find it.
- **NVIDIA hardware with DLSS Ray Reconstruction and path tracing.** Nothing here was tested on
  hardware without Ray Reconstruction.
- Offsets into objects (`manager+0x3ea`, `frame+0xf94`) are 2.31 layouts and move between builds.

## How sure each statement is

Each section is marked with how its facts were established:

| Mark | Meaning |
| --- | --- |
| **measured** | Observed at runtime: probe logs, engine option reads, GPU captures, or analysis of saved photos |
| **static** | Read from the 2.31 executable's disassembly |
| **reported** | Observed by the tester in game and not recorded in a file, such as which DLSS DLL and preset were active |
| **inferred** | Follows from measured or static facts but was not observed directly |

A statement with no mark in a measured or static section carries that section's mark. Anything
inferred is marked where it appears.

## What is not known

Each page ends with its open questions. The main ones:

- What inside DLSS Ray Reconstruction preset F turns a capture sweep of 7x7 or more into bands, and why
  on a 64-pixel row grid.
- Why the bands stop halfway down a 2560x1440 photo but run the full height of a 1920x1080 one.
- Why the game switches Ray Reconstruction off while framing in Photo Mode.
- Which UI visibility state makes the game switch Ray Reconstruction back on.
