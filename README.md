# Photo Mode Ray Reconstruction Fix

A RED4ext plugin for Cyberpunk 2077 2.31 that keeps DLSS Ray Reconstruction on in Photo Mode.

## What the game does

Opening Photo Mode switches Ray Reconstruction off, so the scene is framed with the NRD denoiser. When
the UI hides to take a photo, the game switches Ray Reconstruction back on for the capture, then off
again. What you frame is not what the photo renders.

## What the plugin does

It removes the Ray Reconstruction switch-off calls from the two native Photo Mode handlers that make
them, so Ray Reconstruction stays as you had it in gameplay, while framing and while capturing.
Everything else those handlers do stays, including switching frame generation off in Photo Mode and
restoring it on exit. With Ray Reconstruction switched off in the graphics settings, photos are taken with
NRD as usual.

It does this as a RED4ext plugin that patches the game's code once at load: no RTTI or script hooks, and
nothing runs per frame. Both functions are resolved by RED4ext hash and checked byte for byte before
anything is written. On a
game build where either one differs, the plugin logs a line and patches nothing.

## Known issue: banded photos at high capture sample counts

A Photo Mode photo is built from a sweep of NxN sub-pixel samples, where N is the engine option
`RayTracing/ReferenceScreenshot/SampleNumber` (game default 5). With **Ray Reconstruction preset F**,
photos show thin horizontal bands every 64 pixels when N is 7 or more. Preset E is clean up to 16. This
happens in the unmodified game too, which also takes its photos with Ray Reconstruction when it is on.

If another mod raises the option above 6, use preset E, or run this in the CET console before taking a
photo. It sets the option to 5 and prints the new value. The game resets it on every launch, so run it
once per session:

```lua
GameOptions.SetInt("RayTracing/ReferenceScreenshot", "SampleNumber", 5) print("Photo Mode capture samples set to " .. GameOptions.GetInt("RayTracing/ReferenceScreenshot", "SampleNumber") .. " (6 or less avoids banding with RR preset F)")
```

**IGPT.** In-Game Photomode Tweaks takes its photos with the same kind of sweep, but its N is
`Editor/Recording/HighResolutionScreenshot_MS_Count`, default 8. Without this plugin IGPT photos use NRD and
do not band. With it they use Ray Reconstruction, so on preset F they band at 7 or more, including the default of 8.
Use preset E, or run this in the CET console, which sets the option to 5 and prints the new value. It also resets on every launch:

```lua
GameOptions.SetInt("Editor/Recording", "HighResolutionScreenshot_MS_Count", 5) print("IGPT capture samples set to " .. GameOptions.GetInt("Editor/Recording", "HighResolutionScreenshot_MS_Count") .. " (6 or less avoids banding with RR preset F)")
```

## Install

Download from [Nexus Mods](https://www.nexusmods.com/cyberpunk2077/mods/33897). Install with a mod manager, or copy
`red4ext\plugins\PhotoModeRayReconstructionFix\` into the game folder. Requires
[RED4ext](https://www.nexusmods.com/cyberpunk2077/mods/2380).

## The probe

`probe/` holds `PhotoModeRRProbe`, a read-only RED4ext plugin for investigating Photo Mode rendering. It
changes nothing in the game. It logs:

- the inputs of the conditions that decide whether a frame gets Ray Reconstruction, once per view
  whenever they change
- the Streamline constants (jitter, reset, motion vector scale, camera) for every frame around a capture
- the capture executor's object on every capture frame

The log is RED4ext's plugin log, `red4ext/logs/photomoderrprobe-*.log`. It is a development tool, not
for players: it logs heavily during captures and works on game 2.31 only.

## Technical notes

[`docs/`](docs/README.md) covers how Photo Mode switches Ray Reconstruction off, how a photo is captured
frame by frame, the banding measurements, and the probe, with addresses and evidence.

## Build

See `plugin/CMakeLists.txt` and `probe/plugin/CMakeLists.txt`. RED4ext.SDK is header-only; point the
include path at a checkout.

## Credits

- [RED4ext](https://www.nexusmods.com/cyberpunk2077/mods/2380) by WopsS.
- [sammilucia](https://www.nexusmods.com/cyberpunk2077/users/49024583) for being a sounding board and
  confirming my crazy theories.

## License

Licensed under the [PolyForm Strict License 1.0.0](LICENSE.md). You may use this mod and read its source
for any **noncommercial** purpose. You may not share or re-upload it, or make changes or new works based on
it, without permission. Commercial use, including paid mods or selling, is not permitted.

Permission requests go through the [Nexus Mods page](https://www.nexusmods.com/cyberpunk2077/mods/33897).

## Disclaimer

This mod was developed with the assistance of an LLM. All in-game testing and code validation was
performed by a human. No rogue AIs were permitted through the Blackwall.
