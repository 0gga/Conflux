# Conflux

A C++20 / Qt Quick desktop foundation for making Git-backed Minecraft modpacks.
The owner selected Conflux to suggest technologies and sources coming together.

## Try it

On this Windows workstation, run `scripts/build.ps1 -Deploy`, then `scripts/run.ps1`.
The standalone build is `dist/modpack-studio.exe`. Qt 6.10.1, MinGW 13.1 and Ninja are already installed in `C:/Qt`. The build script supplies the missing WebP decoder from Qt's official ImageFormats archive, verifies its pinned SHA-256, and keeps it inside the project.

For another Qt 6.8+ toolchain (including Qt Svg):

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Working in this foundation

- OLED Qt Quick interface with a unified Library, Discover, pack content workspace and Settings.
- Every pack is tagged Conflux, Modrinth or CurseForge. Persistent hiding, a Hidden tab, source/search filters and onboarding only when no visible packs remain.
- Rounded controls and theme-aware icons, including embedded mod artwork and related-mod config icons.
- Click a modpack row to open Content, Configs, Files, Logs and Worlds. Explore mods preserves pack context; the redundant Configure button is removed.
- Back/forward toolbar, mouse buttons and Alt+Left/Right navigation with unsaved-edit protection.
- Visual config forms for identified JSON/TOML/properties files with explicit backed-up saves and external-change detection.
- Settings uses a vertical category index for Appearance, Warnings and Connections; warning visibility persists by category.
- Review banners for identified loader mismatches, duplicate mod IDs, missing managed pins/dependencies and possible orphan config names. Ownership suggestions are uncertain; nothing is deleted.
- Simple JSON themes with four semantic color groups, validation, reload and theme selection in Settings.
- Read-only discovery of Modrinth and CurseForge installation folders; configurable paths.
- Live Modrinth mod search and native project showcases opened from installed mods or catalogue results: Description, Gallery and grouped Versions, with Modrinth priority and retained file variants.
- Compact content table with author artwork, selection, sorting, Author/Environment/State/Update available/Open source/External filters, enable/disable, removal, Show file, Copy link and persistent Freeze version.
- Compatible stable update checks, version switching, multi-mod dependency planning and Update all. Local changes refresh through debounced filesystem notifications; Refresh also rechecks provider data.
- Explicit file operations back up originals and reject stale hashes. Modrinth content installs into managed packs and existing launcher packs through an isolated staging transaction; launcher metadata stays untouched.
- Modrinth Play and direct Play world handoff for supported installations, using the launcher's existing account session.
- CurseForge search, descriptions and files adapter, enabled only with a session API key (or `CURSEFORGE_API_KEY`). No key is included or saved.
- Merged/source-only views. Only explicit project identity mappings collapse cross-platform projects. Modrinth metadata wins; exact file hashes and compatibility establish release equivalence.
- Create local pack repositories, add compatible Modrinth releases, resolve required dependencies, download and verify their files, save Git commits and inspect history.
- Preserve pinned dependency versions, reject conflicts/incompatible mods, support cancellation, and reuse verified installed files.
- Consistently aligned pack/installation rows, including long names and wide windows.
- Atomic manifest writes, no overwriting existing packs, no shell interpolation, Git work and discovery off the UI thread.

Create a modpack, browse mods, open a project, then choose **Add +** on a compatible release. Required dependencies are resolved automatically and all files are checked against published SHA-512/SHA-1 hashes before the manifest changes. The pack workspace lets you save that change to Git. Existing Git author configuration is used; missing author configuration is reported through the UI.

Public Modrinth browsing and downloading require no application approval or API key. The app identifies itself with `0gga/Conflux/0.1.0` and its public repository URL. Account sign-in and publishing are separate OAuth work. See [Modrinth API authentication](https://docs.modrinth.com/api/) and [OAuth app registration](https://docs.modrinth.com/guide/oauth/).

## Deliberately unfinished

This is a runnable foundation, not a complete launcher. GitHub/Modrinth account sign-in, remote push/pull, semantic conflict resolution, native Conflux/CurseForge game installation/launch, loader version resolution, import/export, fuzzy image duplicate detection and public comment aggregation are not implemented. `loaderVersion` remains null until loader resolution exists. Conflux-managed packs still need game/runtime installation and account authentication before they can launch independently. Existing Modrinth installations can hand launch requests to Modrinth App; direct worlds require a verified instance ID, a readable saved world and Minecraft 1.20+. Optional dependencies are not automatically installed. Conflicting exact pins are reported rather than silently upgraded. Content removal retains backups; unused dependencies remain until removed explicitly. File-based JAR/ZIP content can be toggled or removed; folder-based resource/shader packs remain inspectable. Freeze preferences are local to this Conflux installation. Automatic update checks request stable releases; the version picker exposes compatible alternatives.

CurseForge API approval and agreement compatibility must be resolved before distribution. The owner is awaiting approval. The app makes no CurseForge requests without a configured key. The browsing adapter has not been live-verified without an approved key; CurseForge downloads/dependency resolution are not enabled yet. No provider metadata is persisted to a catalogue cache. Native showcases combine available galleries in Modrinth-first order and remove equal URLs and decoded-pixel duplicates. Differently resized, recompressed or cropped images remain distinct. CurseForge gallery/API behavior remains unverified live until approval.

The current browser requests at most 40 search hits per provider and 50 CurseForge releases; the details panel displays 30 matching releases. Pagination is future work. Projects with no verified cross-platform mapping remain separate; matching names are insufficient evidence.

## Verification

`scripts/build.ps1` builds and runs CTest, including loopback HTTP installer tests, library-row alignment at three widths, and config/navigation/preferences integration tests. Set `MODPACK_STUDIO_LIVE_TEST=1` to also verify real Modrinth search/details and an Iris installation with required dependencies in temporary storage. Set `CONFLUX_UI_ARTIFACTS` to a folder to capture the row comparison renders.

`modpack-studio.exe --smoke-test <absolute-output-folder>` verifies live browsing, creates a temporary pack, pins a compatible release, commits it to Git, checks temporary visual TOML editing and renders the catalogue, library, all pack tabs, visual editor and warning settings to PNG, and exits. Test repositories are isolated in an automatically removed temporary folder; real launcher installations are only inspected during verification. Actual content mutations in tests use temporary fixtures. The smoke also renders native showcases, grouped version selection, content filters and vertical Settings; dedicated Qt interaction tests exercise real pointer dispatch.

See [architecture](docs/ARCHITECTURE.md), [backlog](docs/BACKLOG.md) and [owner requirements](docs/REQUIREMENTS.md).

## Themes and pack inspection

OLED is the default: a true black canvas, subtle dark surfaces, grey inputs/selections and mint primary actions. Shared controls distinguish ordinary actions from selected filters, with hover, pressed, disabled and keyboard-focus states. The original dark grey palette remains available.

Open **Settings / Appearance / Open themes folder**, copy `oled.json` or `dark-grey.json` under a new filename, change `name` and colors, then select it. Use **Reload theme** after editing. Four groups and fourteen tokens style the app: `surfaces` controls backgrounds, inputs, selections and borders; `text`, `actions` and `status` cover the remaining colors. Invalid files keep the last good palette. Built-in themes always remain available. The old built-in default migrates to OLED once; custom selections/files and subsequent choices are preserved. Theme selection and hidden packs persist locally.

Discovery, launcher metadata, worlds and logs stay read-only. Explicit Content actions can alter selected JAR/ZIP files with backups and hash checks. External installs resolve/download in an isolated staging workspace, then recheck the installed mod inventory before applying; another launcher may subsequently report local changes because its metadata is deliberately not rewritten. The visual editor can explicitly save supported identified configs with backups and optimistic conflict checks. Mod archive metadata is read in a worker with a 1 MiB per-entry limit; config listing skips linked directories, stops at depth five and limits the combined file list to 5,000 entries. Fabric/Quilt JSON and basic Forge-family TOML identity are recognized. Forge-family metadata alone cannot prove Forge vs NeoForge compatibility; full dependency ranges, bundled mod conflicts and runtime behavior are not validated. An enabled [Sinytra Connector](https://connector.sinytra.org/) identity or its JarJar component metadata changes Fabric-on-Forge-family warnings into an unverified bridge notice. Missing game/loader metadata stays unknown. Possible stale config ownership uses filename matching and always requires review. Config contents are read only when opening the editor. Unidentified owners and unsupported formats have no Edit action; Files opens their containing folder. TOML/properties preserve comments and unsupported values; JSON is reserialized. Runtime schemas are not inferred. Config changes are not yet included in the existing Git manifest commit flow; see the backlog.

Archive metadata uses vendored [miniz 3.1.0](https://github.com/richgel999/miniz/tree/174573d60290f447c13a2b1b3405de2b96e27d6c), pinned at commit `174573d60290f447c13a2b1b3405de2b96e27d6c` under its retained MIT license in `third_party/LICENSE.miniz`. `miniz_export.h` is a local static-build export definition. Theme registration uses Qt's [QML module and singleton support](https://doc.qt.io/qt-6/qt-add-qml-module.html).

The visual TOML adapter uses [toml++ v3.4.0](https://github.com/marzer/tomlplusplus/tree/30172438cee64926dc41fdd9c11fb3ba5b2ba9de), pinned at `30172438cee64926dc41fdd9c11fb3ba5b2ba9de`, with the retained MIT license in `third_party/LICENSE.tomlplusplus`. The architecture separates storage ports, content identity/rules, codecs/repository, feature presentation models and QML panels. No feature should add parsing or filesystem writes to a view or composition root.
