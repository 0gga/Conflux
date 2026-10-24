# Conflux backlog

Requests are prioritized here; listing an idea does not mean it is implemented. Keep acceptance criteria and limitations explicit. The SOLID contracts in ARCHITECTURE.md apply to every item.

## Delivered in this workspace update

- OLED default with a black canvas, distinct action/selection states, compact filters, shared keyboard-accessible sidebar buttons and consistent input focus/hover feedback. JSON customization retains four groups and fourteen tokens; dark grey remains available.
- Rounded shared controls and theme-aware vector icons, embedded mod/resource icons and related-mod icons for unambiguous config matches.
- Back/forward buttons, mouse buttons, Alt+Left/Right and guarded navigation for unsaved config edits.
- Independent Content, Configs, Files, Logs and Worlds sections.
- Left-sidebar Settings with Appearance, Warnings and Connections tabs. Warning visibility persists by category.
- Visual JSON/TOML/properties forms, typed controls, lists, TOML documented ranges/choices, explicit saves, backups and external-change detection.
- Separate storage, content, config and desktop libraries with injected interfaces and feature-specific presentation models.

- Vertical Settings categories; native Description/Gallery/Versions showcases; verified Modrinth Play/world handoff.
- Modrinth-style content search, sorting, category/advanced filters, selection, version switching, stable updates, enable/disable, file imports/removal and freeze preferences.
- Warning badge beside the pack name; content file events refresh locally, with explicit provider Refresh.

## Next additions

| Priority | Addition | Acceptance criteria |
| --- | --- | --- |
| P1 | JSON5, YAML and additional CFG formats | New codec adapters preserve comments/unsupported syntax, validate round trips, and never pass unknown formats through another parser. |
| P2 | Ownership mapping review/reset UI | Local explicit per-file mod associations are implemented. Add a central mapping list and reset action; retain ambiguity and missing-mod checks. |
| P1 | Mod-provided config schemas and optional game-side bridge | Export actual loaded runtime specs/defaults/enums/validators/restart metadata through a versioned optional game-side contract. Catalogue invokes registered Java screens; a launcher cannot execute them offline. Offline forms must not invent constraints. |
| P1 | Config changes in Git | Explicitly include reviewed config files in managed commits; preserve secret/unrelated-file staging guards. Current Git save remains for the pack manifest. |
| P1 | Config change preview and restore | Show a diff before save, list timestamped backups, and restore only after an explicit action with conflict checks. |
| P1 | GitHub sign-in and sync | OAuth, repository creation, push/pull and conflicts through UI; credentials stay outside repositories. |
| P2 | Broader verified cross-platform identity | Extend reviewed mappings for different binaries without title matching. Exact-SHA1-confirmed CurseForge fingerprints now identify untracked files and known launcher projects lacking exact pins. |
| P2 | Dependency and incompatibility graph | Separate rules evaluate version constraints, duplicate/nested IDs, optional dependencies and explicit incompatibilities with evidence. Static results do not promise a successful game launch. |
| P1 | Independent Conflux and CurseForge world launch | Add supported runtime/account/provider adapters, loader/Java resolution and real launch-state/error verification. Do not treat content API approval as a game-launch API. Current direct handoff supports verified Modrinth instances only. |
| P2 | Richer mod identity and icons | Resolve nested-JAR identity/icons and verified provider artwork; retain a clear generic fallback. Cache with size limits and invalidation. |
| P2 | Log tools | Search/filter/copy, live tail with cancellation, gzip log archives and explicit privacy controls before sharing. |
| P2 | World metadata and backups | Read level.dat through a bounded NBT adapter, show actual world names/version/last played, and provide explicit backup/restore flows. |
| P2 | Config editing history | Undo/redo across search/filter changes, schema-aware list controls and richer nested objects/tables and comment-preserving collection edits. |
| P2 | Pack Overview / Changes tabs | Summarize pack metadata, health and reviewed changes in independent panels; avoid duplicating Content or Git state. |
| P2 | Pack import/export and broader identity matching | Standard Modrinth/CurseForge pack formats, reviewed identity mappings, distribution rules, pagination and conservative resized/recompressed image matching. Current native galleries remove exact decoded-pixel duplicates. |
| P3 | Accessibility and performance | Keyboard/screen-reader review, scaling, contrast, large-library benchmarks, paged config inventory and cancellable scans. |
| P3 | Distribution | Reproducible Windows packaging, dependency/license notices and releases; expand platform support only with real build/runtime verification. |

## Content follow-ups

- Extend broader independently verified cross-platform links. CurseForge-only files now resolve through bounded fingerprints with published SHA1 confirmation and a persistent metadata index. Known pins support approved-key installs, compatible update checks and version selection; exact shared file hashes merge catalogue entries. Different binaries require another verified mapping. Actual Minecraft execution remains separate from content API validation.
- Add safe folder-pack identity/mutations and cross-type required asset dependency planning and crash-durable multi-file recovery. Mixed known stable ZIP/mod commands now stage together and roll back handled commit failures; individual verified Modrinth ZIP install/version selection is implemented; this does not imply a universal shader-settings adapter.
- Add shared Git freeze-policy files, richer update channels and backup restore/retention UI. Current freeze preferences are local.
- Add full NBT world names/metadata and game-process observation; handing a URL to a launcher is not proof of a successful game launch.

## Current editing limits

Ownership is an unambiguous filename/directory match, not a declaration from a mod. Unknown owners and unsupported formats remain inspectable without an Edit action. JSON is reserialized with its values preserved; TOML/properties patch supported values and preserve unrelated text. Complex values remain unchanged and are omitted from forms. No runtime schema, enum or range is inferred for JSON. Logs/worlds/files are inspection tools. Scans do not guarantee runtime compatibility. Backups are local application data and have no automatic retention policy yet.

- Delivered correction: readable descriptions, page-only navigation history, dependency-safe Forge identity and grouped duplicate paths, editable-first config forms with explicit owner association, Lucide glyphs and rounded edge-to-edge artwork.

## Proposed convenience work for owner review

These are ideas, not delivered features or additional owner requirements.

| Priority | Idea | Acceptance criteria |
| --- | --- | --- |
| P1 | Install/update plan with rollback | Preview dependencies, replaced files, sizes and conflicts; create a reversible snapshot before applying; restore with hash checks. |
| P1 | Cross-type asset dependencies | Resolve provider-declared content types through independent verified dependency transactions. Desktop selected files now commit independently; unsupported cross-type dependency metadata must become a separate warning, never undo the root. |
| P1 | Per-instance update channels | Choose stable/beta/alpha with per-project exceptions/freezes; show a changelog and compatibility diff before Update all. |
| P1 | Shader-engine and side checks | Detect Iris/Oculus/OptiFine and client/server requirements from verified metadata; explain prerequisites and offer reviewed alternatives. |
| P2 | Batch install queue and saved searches | Queue projects, review the combined dependency plan and preserve named searches beyond the implemented per-instance/content-type compatibility preferences. |
| P2 | Indexed merged page jumps and precise totals | Editable page entry and single-provider offset jumps are delivered. Build resumable provider cursor indexes to accelerate large merged/hidden-view jumps, which currently materialize intervening pages asynchronously. Expose exact deduplicated totals when known; provider totals currently bound merged/client-filtered page counts until exhaustion. |
| P2 | Instance cloning and profiles | Duplicate a pack with a new Git identity; support client/server/export profiles preserving exact shared pins and explicit overrides. |
| P2 | Reusable collections | Curate optional groups of mods with compatibility/dependency previews before adding a collection. |
| P2 | Git-friendly change review | Show added/removed/updated content and config diffs; compare commits and restore a pack version through UI. |
| P2 | Shared verified content cache | Deduplicate immutable downloads by hash, show disk usage and safely reclaim unreferenced files; never mutate shared bytes. |
| P2 | Global ZIP data-pack loader integration | Detect or offer a compatible global pack loader; validate pack formats and future-world activation without silently rewriting saves. Instance storage and mod-form variants are implemented. |
| P3 | Searchable command palette | Jump to instances, worlds, content, configs and common actions with clear context and keyboard shortcuts. |

- Delivered browsing: page-size/sort controls, cached page/scroll restoration, right multi-value compatibility/tag filters, persistent hide/restore menus, instance-scoped Data Packs and the Connector search preference. Editable page entry with nearby/first/last shortcuts and native single-source offset jumps are delivered. Precise merged totals/indexed merged jumps, automatic global ZIP loader provisioning and cross-type ZIP dependency graphs remain follow-up work.

- Delivered refinement: compact Explore header, aligned instance/filter widths, source selector beside pagination, editable loaders, persistent per-instance/content-type version/loader choices and retained per-type page/scroll state.

- Delivered appearance editor: candy-red default accent, grouped editing/saving of all fourteen JSON palette roles, persistent overall/content/filter sizes and layout spacing, and a pinned live preview using the real card/filter components. Compact cards have equal artwork insets, filtered/ordered badges and right-side menus. Future appearance additions: named layout presets and import/export of layout preferences alongside a theme.

- Delivered refinement: channel/game-version/loader release filters and Clear, discovery active-filter chips with instance version locks, second-click menu dismissal, and removal of routine coverage/compatibility prose. Follow-up: a virtualized multi-column release table with real per-release provider download statistics when represented by the provider model.

- Delivered harmony pass: reserved scrollbar gutters and rounded clipped list corners, baseline-aligned authors, icon-led provider/follower/update statistics, configurable installed opacity with live preview, source description image/text/table blocks, enlarged gallery navigation/zoom and stable chip/icon hover geometry. Search fields have contained clear actions; instance tools group Hidden/Updates/Update all with right-aligned Refresh.
- Future source rendering: native animation support for GIF/WebP, richer image captions, safe lazy media/resource budgets and additional structured HTML layouts. Acceptance: preserve source order and theme, bound resource usage, maintain safe links, and verify real source fixtures without executing scripts or arbitrary CSS.

- Delivered settings organization: searchable Interface/Content/Filters/Colors tabs, four palette-group tabs, collapsible real-component live previews, per-field/section resets, navigation/density/scrollbar/tooltip/motion/card metadata preferences and persisted browsing defaults with explicit current-view application. Follow-up: named appearance profiles and versioned import/export; acceptance requires schema validation, preview before application, preservation of unrelated settings and round-trip tests.

- Delivered: type-aware stable updates for all four instance content categories, known-update-only Update all/selected, floating selection with local Share/copy and backed-up bulk enable/disable, compact activity/cancel/error details, underline content/version/author links and scoped author discovery.
- Author identity linking UI: provide reviewed cross-platform author profile association and explicit corrections, with provenance; never infer identity from names. Current author-identities.json exact mappings support merged source queries, while unlinked creators remain separate and CurseForge requires its approved key.

- Persist operation recovery across crashes: journal prepared file/manifest edits and replay or roll back after restart; retain failed-task commands safely without credentials. Acceptance: interruption at every commit boundary recovers one consistent instance before indexing. Current worker/process leases and runtime rollback do not provide power-loss atomicity.

- Delivered: dependency-free selected updates with Minecraft/loader-edition checks, outlined shared control geometry, contained notices/labels, concise statistic tooltips, normalized local search, bounded provider typo fallback, and lazy Latest/All versions changelogs. Full provider-wide typo matching needs a provider-supported fuzzy index or an explicitly maintained bounded local catalogue; current prefix fallback scans at most100 same-filter candidates. Acceptance for expansion: measured query/resource budgets, exact filter/identity preservation and honest totals, without downloading an unbounded catalogue.


- Delivered: silent backed-up same-name replacements, selected-file update staging, reference-only initial dependency planning, worker streaming/hash/archive checks, bounded file-fingerprint metadata/hash reuse, and content-only transaction inventory. Performance tests cover 200/500-mod packs and 1,500 configs with deterministic unchanged-file I/O checks plus elapsed-time/UI-heartbeat budgets. Further work: persistent metadata indexes for first launch after process restart and bounded parallel downloads within an initial dependency graph. Acceptance: measured benefit on large packs, exact hash/lock/compatibility and per-item failure isolation, bounded memory/network use, cancellation and no UI-thread archive/hash work. Cross-process external-launcher races and crash-durable transactions remain subject to the recovery work above.


- Owner-requested 100% application coverage remains an explicit target. The isolated GCC/gcovr reports expose line/function/branch gaps across all `src/`, including entry points and smoke code. Acceptance: execute or intentionally simplify every reachable production path, add failure/cancellation/external-change and provider-adapter integration cases, retain meaningful assertions, and achieve the stated metric without hiding uncovered business logic. QML requires separate rendered interaction/geometry/pixel cases and native secondary-monitor validation; C++ percentages must never be presented as QML coverage. Live authenticated CurseForge and actual Minecraft execution remain separate opt-in integration environments.


- Delivered per-instance Release/Beta/Alpha install/update policy, shared provider mapping and UI, dependency enforcement, generation guards and queued-policy revalidation. Release is the default; explicit choices keep installed files visible and never trigger a downgrade. Existing crash-recovery and full-coverage targets above remain open.

- Delivered owner-authorized source content-lock synchronization for recognized Modrinth and CurseForge instances. Native metadata is authoritative after confirmation; unsupported schema/untracked identities remain visible failures rather than simulated source success. Crash journals and source-launcher cache notification protocols remain outside this change.


## Technical improvement queue (23 October 2026)

| Priority | Improvement | Acceptance criteria | State |
| --- | --- | --- | --- |
| P1 | Shared bounded provider JSON reads | Reuse one response reader, parse large payloads on workers, enforce size limits, allow callbacks to dispose owners, preserve credentials/cancellation/cache boundaries and measure indexed large-pack update result processing. | Delivered; 40 suites and packaged smoke pass; 5000-file check 2229 ms to 260 ms |
| P2 | Bound QML build import scans | Measure configure-time scanners that currently use the whole repository as their root; exclude generated deployment/cache trees through supported Qt configuration while preserving QML imports and packaged smoke. | Backlogged |
| P1 | Persistent installed-content metadata index | Resolve exact project/author/tag metadata once on first load or installation, persist across sessions in SQLite, identify changed files by hash, publish metadata before update checks and keep failed lookups retryable. Include CurseForge-only known identities. | Delivered; 35 suites and packaged smoke pass |
| P1 | Unresolved existing-copy installation | Replace renamed/unresolved copies by verified file hash or archive runtime ID; reconcile duplicates, preserve disabled state and recognize already-present release hashes without another install. Verify NeoForge/Fabric and managed provider pins. | Delivered; 36 suites and packaged smoke pass |
| P1 | Loaded instance filter budgets | Reuse prepared search/filter/sort/author state; remove repeated model key scans, preserve persistent/duplicate identities and verify real rendered filtering without new catalogue reads. | Delivered; 33 suites and packaged smoke pass |
| P1 | Post-commit inventory recovery | Keep a verified committed install successful if a later inventory read fails; show separate verification diagnostics, preserve known state and block unsafe follow-up writes until a fresh read succeeds. | Delivered; 38 suites and packaged smoke pass |
| P1 | Optional dependency overlay | After initial installation, offer declared optional dependencies in a focused overlay with per-item Install actions and a Settings toggle. Required dependencies remain automatic; optional failures never undo installed content. | Delivered; 38 suites and packaged smoke pass |
| P1 | Independent initial dependency attempts | Commit selected content first; attempt each declared required dependency independently, retain disabled/locked files and report dependency failures separately. Verify both providers, cancellation, hash/write failures, cycles and bounded responsiveness. | Delivered; 32 suites and packaged smoke pass |
| P1 | Captured bulk update destination | Reentrant task notifications may change the visible instance; every accepted bulk update must retain the original instance and leave the other inventory unchanged. | Delivered; 29 suites and packaged smoke pass |
| P1 | Reentrant provider callback lifetime | Callbacks may dispose their catalogue client during success, error, optional missing responses, cached/coalesced delivery or intermediate signals; no later code may touch destroyed replies/clients or publish stale follow-up work. | Delivered; 28 suites and packaged smoke pass |
| P1 | Lossless lock patch correctness | Duplicate targets are idempotent; duplicate or shadowed source lock fields are rejected; invalid JSON never becomes a successful replacement; reused/null error outputs are safe; unrelated bytes remain exact. | Delivered; 26 suites and packaged smoke pass |
| P1 | Indexed metadata mutation | Index source addons once and build replacements in one pass, replacing target-by-addon scans and repeated buffer shifts. Large-pack tests enforce correct scope, bounded runtime and exact output. | Delivered; 26 suites and packaged smoke pass |
| P2 | Indexed source-lock UI lookup | Measure card/project lock reads against large source inventories, then remove repeated inventory scans while preserving original-instance and pending-operation semantics. | Delivered; 26 suites and packaged smoke pass |
| P2 | Cancel obsolete version lookups | A held obsolete provider lookup must not indefinitely delay newer eligible work; preserve concurrency caps, credential generations, callback lifetime and visible errors. | Delivered; 26 suites and packaged smoke pass |
| P2 | Separate content operation coordination | Extract focused lock/update orchestration from ContentActions without duplicating inventory/queue state or moving storage logic into views; preserve all transaction and rendered interaction tests. | Delivered focused planners/reservations; 29 suites and packaged smoke pass |
| P2 | Release-stage eligibility budgets | Measure repeated stage eligibility before changing data structures; retain exact default Release behavior and invalid-stage rejection. | Delivered; 26 suites and packaged smoke pass |
| P2 | Scoped indexed operation reservations | Derive claims only from the requested instance, reject conflicts without partial acquisition, release only the owning operation, and compare indexed behavior with a reference through cancellation. | Delivered; 26 suites and packaged smoke pass |
| P2 | Operation HUD count read budgets | Measure repeated active/failed count reads with large queues; preserve atomic transitions, pruning, clearing, task ordering and persistent model indices. | Delivered; 26 suites and packaged smoke pass |
| P2 | Bounded task ordering searches | Measure bulk task insertion, replace full-list position scans only with demonstrated benefit, and verify mixed-state ordering against a reference while retaining model indices and count notifications. | Delivered; 26 suites and packaged smoke pass |
| P2 | Bounded preference identity-key reuse | Measure repeated preference-key derivation, retain byte-compatible stored keys and current value reads, bound cache memory and verify independent instances/provider identities plus eviction. | Delivered; 27 suites and packaged smoke pass |
| P2 | Focused content update intent | Build one immutable per-content update at a time from indexed policy state, preserving exact IDs/hashes/types, disabled state and original destination while the coordinator owns queues and validation. | Delivered; 29 suites and packaged smoke pass |
| P2 | Focused content lock intent | Separate source/local/blocked lock routing into a side-effect-free component; preserve captured scope, pending protection, missing source behavior and existing worker validation. | Delivered; 28 suites and packaged smoke pass |
| P2 | Indexed target resolution during bulk acceptance | Measure repeated target scans for large queues, resolve against one authoritative instance snapshot, preserve row order, duplicate-key behavior, aliases and expected hash checks, and retain cancellation/retry reservations. | Delivered; 36 suites and packaged smoke pass; 5000 held-backend accepts3403ms to425ms |
| P2 | Identity-formatting read budgets | Measure repeated identity formatting in rendering and operation helpers; avoid temporary collections only where measurements justify it, preserving provider-qualified identity and ambiguity rules. | Delivered; 30 suites and packaged smoke pass |
