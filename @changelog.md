# Changelog - Photo Mode Ray Reconstruction Fix

## [1.0.0] - 2026-09-14

### Changed
- Version 1.0.0 in the `Main.cpp` header and `RED4EXT_V1_SEMVER`. First Nexus release. No behaviour change
  from 0.3.0.
- Renamed from Photo Mode Ray Reconstruction: plugin name, CMake target and DLL are
  `PhotoModeRayReconstructionFix`, installed at `red4ext/plugins/PhotoModeRayReconstructionFix/`, logging to
  `red4ext/logs/photomoderayreconstructionfix-*.log`. GitHub repo `cp2077-photo-mode-ray-reconstruction-fix`.

### Added
- `nexus_description.bbc`, `nexus_changelog.md`, `@features.md`, `release-manifest.json`, `RELEASING.md`,
  `.github/workflows/release.yml` (byte-identical to `MyMods/_shared/release/release.yml`).
- `docs/`: technical notes on the Photo Mode RR switch, the capture pipeline, banding and the probe.
- `probe/`: `PhotoModeRRProbe` merged in with its history. Never shipped.

## [0.3.0] - 2026-09-13

### Removed
- The per-capture-frame RR history reset. It softened banding at `SampleNumber` 12 but made captures
  boil; with history kept and `SampleNumber` 5 photos are clean.

## [0.2.0] - 2026-09-13

### Added
- An RR history reset on every capture frame, to test whether history carried across the capture sweep
  caused banding.

## [0.1.0] - 2026-09-13

### Added
- The plugin. On load it resolves `OnPhotoModeOpened` (hash `2954628956`) and
  `OnPhotoModeUIVisibilityChanged` (hash `3553368820`), verifies their bytes, and replaces the four
  RR-off instructions: `+0x4e` and `+0x58` in the first, `+0x3d` and `+0x4c` in the second.
  Runtime-independent; the byte check is the only gate.
