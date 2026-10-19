# Conflux

A C++20 / Qt Quick desktop foundation for making Git-backed Minecraft modpacks.
The owner selected Conflux to suggest technologies and sources coming together.

## Try it

On this Windows workstation, run `scripts/build.ps1 -Deploy`, then `scripts/run.ps1`.
The standalone build is `dist/modpack-studio.exe`. Qt 6.10.1, MinGW 13.1 and Ninja are already installed in `C:/Qt`. The build script supplies the missing WebP decoder from Qt's official ImageFormats archive, verifies its pinned SHA-256, and keeps it inside the project.

For another Qt 6.8+ toolchain:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Working in this foundation

- Dark Qt Quick interface with Library, Discover, My modpack and Settings.
- Read-only discovery of Modrinth and CurseForge installation folders; configurable paths.
- Live Modrinth mod search, Minecraft/loader filters, descriptions, gallery and release browsing.
- CurseForge search, descriptions and files adapter, enabled only with a session API key (or `CURSEFORGE_API_KEY`). No key is included or saved.
- Merged/source-only views. Only explicit project identity mappings collapse cross-platform projects. Modrinth metadata wins; exact file hashes and compatibility establish release equivalence.
- Create local pack repositories, add compatible Modrinth releases, resolve required dependencies, download and verify their files, save Git commits and inspect history.
- Preserve pinned dependency versions, reject conflicts/incompatible mods, support cancellation, and reuse verified installed files.
- Consistently aligned pack/installation rows, including long names and wide windows.
- Atomic manifest writes, no overwriting existing packs, no shell interpolation, Git work and discovery off the UI thread.

Create a modpack, browse mods, open a project, then choose **Add +** on a compatible release. Required dependencies are resolved automatically and all files are checked against published SHA-512/SHA-1 hashes before the manifest changes. The pack workspace lets you save that change to Git. Existing Git author configuration is used; missing author configuration is reported through the UI.

Public Modrinth browsing and downloading require no application approval or API key. The app identifies itself with `0gga/Conflux/0.1.0` and its public repository URL. Account sign-in and publishing are separate OAuth work. See [Modrinth API authentication](https://docs.modrinth.com/api/) and [OAuth app registration](https://docs.modrinth.com/guide/oauth/).

## Deliberately unfinished

This is a runnable foundation, not a complete launcher. GitHub/Modrinth account sign-in, remote push/pull, semantic conflict resolution, game installation/launch, loader version resolution, import/export, fuzzy image duplicate detection and public comment aggregation are not implemented. `loaderVersion` remains null until loader resolution exists. Modrinth mod files are installed into Conflux-managed pack folders; this does not yet make the pack launchable. Optional dependencies are not automatically installed. Conflicting exact pins are reported rather than silently upgraded. Removing a mod removes its unedited managed file; unused dependencies remain until removed explicitly.

CurseForge API approval and agreement compatibility must be resolved before distribution. The owner is awaiting approval. The app makes no CurseForge requests without a configured key. The browsing adapter has not been live-verified without an approved key; CurseForge downloads/dependency resolution are not enabled yet. No provider metadata is persisted to a catalogue cache. Current galleries render Modrinth images; cross-provider gallery merging is a later step. The domain contains a tested decoded-pixel fingerprint primitive, not a finished perceptual matching service.

The current browser requests at most 40 search hits per provider and 50 CurseForge releases; the details panel displays 30 matching releases. Pagination is future work. Projects with no verified cross-platform mapping remain separate; matching names are insufficient evidence.

## Verification

`scripts/build.ps1` builds and runs CTest, including loopback HTTP installer tests and a library-row alignment regression check at three widths. Set `MODPACK_STUDIO_LIVE_TEST=1` to also verify real Modrinth search/details and an Iris installation with required dependencies in temporary storage. Set `CONFLUX_UI_ARTIFACTS` to a folder to capture the row comparison renders.

`modpack-studio.exe --smoke-test <absolute-output-folder>` verifies live browsing, creates a temporary pack, pins a compatible release, commits it to Git, renders five screens to PNG, and exits. Test repositories are isolated in an automatically removed temporary folder; real launcher installations are read only.

See [architecture](docs/ARCHITECTURE.md) and [owner requirements](docs/REQUIREMENTS.md).
