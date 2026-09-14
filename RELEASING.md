# Releasing

This repo publishes Photo Mode Ray Reconstruction Fix to **GitHub Releases** and **Nexus Mods** via
[`.github/workflows/release.yml`](.github/workflows/release.yml), driven by
[`release-manifest.json`](release-manifest.json).

The mod is one RED4ext plugin DLL. **CI does not build it**: the workflow stages the committed
`red4ext` folder and zips it, so the DLL in `red4ext/plugins/PhotoModeRayReconstructionFix/` is exactly what
ships. Rebuild and copy it in before every release (commands in the steps below).

| Artifact id | What | File on Nexus |
| --- | --- | --- |
| `photo-mode-ray-reconstruction-fix` | The plugin | main |

## First release is manual (then it automates)

A Nexus **file id does not exist until a file has been uploaded once**, so the very first upload
cannot come from CI. Do this once:

1. **Create the Nexus mod page** and set its requirements (RED4ext) and description (paste
   `nexus_description.bbc`).
2. **Build the first zip locally** and upload it by hand through the Nexus site:
   ```pwsh
   Compress-Archive -Path "red4ext" -DestinationPath "PhotoModeRayReconstructionFix_v1.0.0.zip" -Force
   ```
3. **Read the file id and set it as a repository VARIABLE.** On the mod page open the **Files**
   tab > **API Info** and copy the id - Nexus labels it **"Group ID"** there. Set it as the
   repository variable **`NEXUS_FILE_ID_PHOTO_MODE_RAY_RECONSTRUCTION`** (Settings > Secrets and
   variables > Actions > **Variables**). Put the mod page's number in `nexus_mod_id` in `release-manifest.json`.

   > **It does not go in the repo.** The id does not exist until this first upload.
   >
   > **Do NOT take the id from the public v1 API.** That endpoint has a field also called
   > `file_id`, in a different id space. The wrong value looks plausible and fails only at
   > release time.
4. **Add the API key secret** `NEXUSMODS_API_KEY` (Settings > Secrets and variables > Actions >
   **Secrets**).

After that, every future release publishes automatically.

## Before cutting any release: bump the version and rebuild

The version in the git tag is what CI ships, and it must agree with:

- the `Main.cpp` header (`File Version:` line) and `RED4EXT_V1_SEMVER(...)` in `Query`,
- `@changelog.md` and `nexus_changelog.md`,
- `currentVersion` in `release-manifest.json`.

Then rebuild and commit the DLL:

```pwsh
$cm = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cm --build plugin\build --config Release
Copy-Item plugin\build\Release\PhotoModeRayReconstructionFix.dll red4ext\plugins\PhotoModeRayReconstructionFix\ -Force
```

The DLL's version is visible in `red4ext/logs/red4ext-*.log` (`PhotoModeRayReconstructionFix (version: ...)`):
check it there after a test launch, because a stale DLL passes every check in the repo.

## Cutting a release

1. Commit the version bump, the rebuilt DLL and your changes.
2. Create a GitHub Release whose **tag** is `photo-mode-ray-reconstruction-fix-v<version>`:
   ```pwsh
   gh release create photo-mode-ray-reconstruction-fix-v1.0.0 --title "Photo Mode Ray Reconstruction Fix v1.0.0" --notes "..."
   ```
   The release body feeds two Nexus fields, split by a `<!-- nexus-description-end -->` marker on
   its own line. Everything **before** the marker becomes the **file description** (capped at 255
   chars); everything **after** it is appended to the mod page's **changelog**. With no marker, the
   whole body becomes the changelog and no file description is sent. Plain lines, no markdown.
3. On publish, the workflow zips `red4ext` as `PhotoModeRayReconstructionFix_v<version>.zip`, attaches it to
   the GitHub Release, and uploads to Nexus.
