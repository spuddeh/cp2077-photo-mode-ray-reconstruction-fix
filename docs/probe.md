# PhotoModeRRProbe

A read-only RED4ext plugin, in `probe/`, that logs what the renderer does around Ray Reconstruction and
Photo Mode captures. Most measured facts in these notes come from its log. It is a development tool: it
is not part of any release, it logs heavily during a photo, and it works on game 2.31 only.

## Hooks

Each hook resolves its function by RED4ext hash, then refuses to attach unless the address and the first
bytes match 2.31. The constants hook also accepts a function that already starts with a jump, so it can
chain onto another plugin's hook there. Every hook calls the original first and changes nothing it
returns; reads sit behind structured exception handling.

| Function | RVA | Hash | Logs |
| --- | --- | --- | --- |
| Frame feature mask builder | `0x1d49540` | `137310724` | One line per view whenever it changes: the resulting RR bit (`0x46`) and every input of the conditions that gate it |
| Streamline constants (`slSetConstants` caller) | `0x78933c` | `2155221092` | Every call for 90 calls after any capture frame, plus the first three calls and every 900th |
| Capture finaliser | `0x1c6bf10` | `1857241502` | A hex dump of its object on each call, once per photo (the log line says "every capture frame") |

The finaliser reads stack arguments up to the seventh, so its hook forwards all of them. A hook with only
the four register arguments crashes the game on the first photo.

## Log

RED4ext writes it to `red4ext/logs/photomoderrprobe-<date>-<time>.log` and keeps only the most recent
few, so copy a log out before the next few game launches if it matters.

**View line** (from the mask builder):

```text
view <ptr> call <n>: RR=<bit 0x46> bit55=<bit 0x55> | dlssBytes=<mgr+0x3e8>/<mgr+0x3e9> rrAvail=<mgr+0x3ea>
  loaded=<feature loaded flags> | rect=<min x>,<min y>-<max x>,<max y> empty=<0|1>
  | kind334=<frame+0x334>(!=0x37:<ok>) kind=<frame+0xf94>(ok:<ok>) mode=<frame+0xf90> targets=<three flags>
  | aa=<Antialiasing> suppressed=<AntialiasingSuppressed> | viewType=<view+0x16e0> viewPtr=... | mask=<128-bit mask>
```

Each entry is one line in the log; the formats are wrapped here to fit.

**Constants line** (from the constants function; fields from `sl::Constants` at manager `+0x200`):

```text
consts call <n> frame <index> kind <last frame kind> | reset=<0|1> historyValid=<mgr+0x1f0> f1f2=<mgr+0x1f2>
  camMotion=<0|1> mv3D=<0|1> depthInv=<0|1> | dlss=<mgr+0x3e8>/<mgr+0x3e9> rr=<mgr+0x3ea>
  | jitter=<x>,<y> mvec=<scale x>,<scale y> pinhole=... | pos=<camera> fov=<deg> aspect=<ratio>
  | v2c diag=... | c2p diag=<clipToPrevClip diagonal> r2=... r3=...
```

`kind 4` lines are capture frames. A photo is the run of consecutive `kind 4` constants lines: 8 settle
frames at jitter (0, 0), then two NxN sweeps (see [capture-pipeline.md](capture-pipeline.md)).

## Offsets it reads

| Object | Offset | Meaning |
| --- | --- | --- |
| Renderer (global `0x3427c00`) | `+0x4658` | Streamline manager |
| Streamline manager | `+0x3e8`, `+0x3e9` | DLSS mode bytes (`0/1` normal frames, `1/0` capture frames) |
| | `+0x3ea` | RR available |
| | `+0x1f0` | history valid; `reset` is sent as its inverse |
| | `+0x200` | `sl::Constants`: view-to-clip `+0x20`, clip-to-prev-clip `+0xe0`, jitter `+0x160`, motion vector scale `+0x168`, camera position `+0x178`, FOV `+0x1b0`, flags `+0x1bc` |
| View | `+0x14` | rectangle, four `int32` |
| Frame | `+0x334` | kind; `0x37` never gets RR |
| | `+0xf90` | mode; 3 skips most of the builder |
| | `+0xf94` | kind; RR needs `< 2`, or 4/5 with targets bound |
| Settings | `0x32f7a38`, `0x32f7a70` | values of `Antialiasing` and `AntialiasingSuppressed` |

## Building

```powershell
cmake -S probe/plugin -B probe/plugin/build -G "Visual Studio 17 2022" -A x64
cmake --build probe/plugin/build --config Release
```

`CMakeLists.txt` points its include path at a RED4ext.SDK checkout; change it to wherever yours is. Copy
`PhotoModeRRProbe.dll` to `red4ext/plugins/PhotoModeRRProbe/` in the game folder. It runs alongside the
main plugin.
