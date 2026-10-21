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
| P1 | Approved CurseForge downloads | Verify live integration after API approval, enforce distribution settings and resolve required dependencies through a provider adapter. |
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

- Complete approved CurseForge downloads/updates and live API validation. Current provider-only showcases require an approved key; Modrinth-backed files use Modrinth first.
- Add safe folder-pack identity/mutations and cross-type required asset dependency planning and cross-batch rollback. Mixed known stable ZIP/mod updates are serialized with partial-completion refresh; individual verified Modrinth ZIP install/version selection is implemented; this does not imply a universal shader-settings adapter.
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
| P1 | Cross-type asset dependencies | Resolve required mod/resource/shader/data-pack dependencies as one reviewed transaction. Current ZIP installs visibly reject required dependencies. |
| P1 | Per-instance update channels | Choose stable/beta/alpha with per-project exceptions/freezes; show a changelog and compatibility diff before Update all. |
| P1 | Shader-engine and side checks | Detect Iris/Oculus/OptiFine and client/server requirements from verified metadata; explain prerequisites and offer reviewed alternatives. |
| P2 | Batch install queue and saved searches | Queue projects, review the combined dependency plan and preserve named searches beyond the implemented per-instance/content-type compatibility preferences. |
| P2 | Arbitrary page jumps and precise merged totals | Build resumable provider cursor indexes for large direct jumps; expose deduplicated totals when exact, without treating summed source listings as unique projects. |
| P2 | Instance cloning and profiles | Duplicate a pack with a new Git identity; support client/server/export profiles preserving exact shared pins and explicit overrides. |
| P2 | Reusable collections | Curate optional groups of mods with compatibility/dependency previews before adding a collection. |
| P2 | Git-friendly change review | Show added/removed/updated content and config diffs; compare commits and restore a pack version through UI. |
| P2 | Shared verified content cache | Deduplicate immutable downloads by hash, show disk usage and safely reclaim unreferenced files; never mutate shared bytes. |
| P2 | Global ZIP data-pack loader integration | Detect or offer a compatible global pack loader; validate pack formats and future-world activation without silently rewriting saves. Instance storage and mod-form variants are implemented. |
| P3 | Searchable command palette | Jump to instances, worlds, content, configs and common actions with clear context and keyboard shortcuts. |

- Delivered browsing: page-size/sort controls, cached page/scroll restoration, right multi-value compatibility/tag filters, persistent hide/restore menus, instance-scoped Data Packs and the Connector search preference. Precise merged totals/direct page jumps, automatic global ZIP loader provisioning and cross-type ZIP dependency graphs remain follow-up work.

- Delivered refinement: compact Explore header, aligned instance/filter widths, source selector beside pagination, editable loaders, persistent per-instance/content-type version/loader choices and retained per-type page/scroll state.

- Delivered appearance editor: candy-red default accent, grouped editing/saving of all fourteen JSON palette roles, persistent overall/content/filter sizes and layout spacing, and a pinned live preview using the real card/filter components. Compact cards have equal artwork insets, filtered/ordered badges and right-side menus. Future appearance additions: named layout presets and import/export of layout preferences alongside a theme.

- Delivered refinement: channel/game-version/loader release filters and Clear, discovery active-filter chips with instance version locks, second-click menu dismissal, and removal of routine coverage/compatibility prose. Follow-up: a virtualized multi-column release table with real per-release provider download statistics when represented by the provider model.

- Delivered harmony pass: reserved scrollbar gutters and rounded clipped list corners, baseline-aligned authors, icon-led provider/follower/update statistics, configurable installed opacity with live preview, source description image/text/table blocks, enlarged gallery navigation/zoom and stable chip/icon hover geometry. Search fields have contained clear actions; instance tools group Hidden/Updates/Update all with right-aligned Refresh.
- Future source rendering: native animation support for GIF/WebP, richer image captions, safe lazy media/resource budgets and additional structured HTML layouts. Acceptance: preserve source order and theme, bound resource usage, maintain safe links, and verify real source fixtures without executing scripts or arbitrary CSS.

- Delivered settings organization: searchable Interface/Content/Filters/Colors tabs, four palette-group tabs, collapsible real-component live previews, per-field/section resets, navigation/density/scrollbar/tooltip/motion/card metadata preferences and persisted browsing defaults with explicit current-view application. Follow-up: named appearance profiles and versioned import/export; acceptance requires schema validation, preview before application, preservation of unrelated settings and round-trip tests.

- Delivered: type-aware stable updates for all four instance content categories, known-update-only Update all/selected, floating selection with local Share/copy and backed-up bulk enable/disable, compact activity/cancel/error details, underline content/version/author links and scoped author discovery.
- Author identity linking UI: provide reviewed cross-platform author profile association and explicit corrections, with provenance; never infer identity from names. Current author-identities.json exact mappings support merged source queries, while unlinked creators remain separate and CurseForge requires its approved key.
