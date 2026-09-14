# Features - Photo Mode Ray Reconstruction Fix

## Implemented
- Ray Reconstruction stays as the player set it in Photo Mode, while framing and while capturing. The
  two native handlers that switch it off run with their four RR-off instructions removed.
- Frame generation is still switched off in Photo Mode and restored on exit, as in the unmodified game.
- Both handlers are verified byte for byte before anything is written. On any other game build the
  plugin logs a line and patches nothing; an already-patched game is detected and left alone.

## Verified in game
- 2026-09-13, Gameplay, 0.3.0: RR live in Photo Mode (`DLSSD` true, `EnableNRD` false inside Photo Mode),
  Nsight captures evaluate the RR feature with all guide buffers; photos clean at `SampleNumber` 5 and 6,
  including straight after fast camera movement; RR off in the settings gives a clean NRD photo;
  `patched:` in the plugin log.

## Not tested
- Hardware without Ray Reconstruction.

## Known issue
- RR preset F bands photos at a capture grid of 7 or more; preset E is clean up to 16. See `docs/banding.md`.
  - Photo Mode's own photos: grid is `RayTracing/ReferenceScreenshot/SampleNumber` (default 5). Happens
    without the plugin too.
  - IGPT photos: grid is `Editor/Recording/HighResolutionScreenshot_MS_Count` (default 8). Without the
    plugin IGPT captures with NRD and is clean, so for IGPT on preset F the plugin brings the banding in.
    8x8 bands (score 47.44), 5x5 is clean (0.35), RR on in both.
