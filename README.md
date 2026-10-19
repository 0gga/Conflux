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
- Create local pack repositories, pin exact compatible release references, remove pins, save Git commits and inspect history.
- Atomic manifest writes, no overwriting existing packs, no shell interpolation, Git work and discovery off the UI thread.

Create a modpack, browse mods, filter to its Minecraft version and loader, open a project, then pin a release. The pack workspace lets you save that change to Git. Existing Git author configuration is used; missing author configuration is reported through the UI.

## Deliberately unfinished

This is a runnable foundation, not a complete launcher. GitHub sign-in, remote push/pull, semantic conflict resolution, game installation/launch, loader version resolution, dependency resolution/downloads, import/export, fuzzy image duplicate detection and public comment aggregation are not implemented. `loaderVersion` remains null until loader resolution exists. A pin records a file reference; it does not install that mod. Required dependency IDs are preserved and shown.

CurseForge API approval and agreement compatibility must be resolved before distribution. The app makes no CurseForge requests without a configured key. This adapter has not been live-verified without an approved key. No provider metadata is persisted to a catalogue cache. Current galleries render Modrinth images; cross-provider gallery merging is a later step. The domain contains a tested decoded-pixel fingerprint primitive, not a finished perceptual matching service.

The current browser requests at most 40 search hits per provider and 50 CurseForge releases; the details panel displays 30 matching releases. Pagination is future work. Projects with no verified cross-platform mapping remain separate; matching names are insufficient evidence.

## Verification

`scripts/build.ps1` builds and runs CTest. For the optional real Modrinth search/detail check, set `MODPACK_STUDIO_LIVE_TEST=1` before running `build/foundation-tests.exe`.

`modpack-studio.exe --smoke-test <absolute-output-folder>` verifies live browsing, creates a temporary pack, pins a compatible release, commits it to Git, renders five screens to PNG, and exits. Test repositories are isolated in an automatically removed temporary folder; real launcher installations are read only.

See [architecture](docs/ARCHITECTURE.md) and [owner requirements](docs/REQUIREMENTS.md).
