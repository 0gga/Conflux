# Conflux backlog

Requests are prioritized here; listing an idea does not mean it is implemented. Keep acceptance criteria and limitations explicit. The SOLID contracts in ARCHITECTURE.md apply to every item.

## Delivered in this workspace update

- Rounded shared controls and theme-aware vector icons, embedded mod/resource icons and related-mod icons for unambiguous config matches.
- Back/forward buttons, mouse buttons, Alt+Left/Right and guarded navigation for unsaved config edits.
- Independent Content, Configs, Files, Logs and Worlds sections.
- Left-sidebar Settings with Appearance, Warnings and Connections tabs. Warning visibility persists by category.
- Visual JSON/TOML/properties forms, typed controls, lists, TOML documented ranges/choices, explicit saves, backups and external-change detection.
- Separate storage, content, config and desktop libraries with injected interfaces and feature-specific presentation models.

## Next additions

| Priority | Addition | Acceptance criteria |
| --- | --- | --- |
| P1 | JSON5, YAML and additional CFG formats | New codec adapters preserve comments/unsupported syntax, validate round trips, and never pass unknown formats through another parser. |
| P1 | Reviewed ownership mappings | Users can correct/confirm config ownership per pack; ambiguity never silently chooses a mod. Persist explicit identity and provenance. |
| P1 | Mod-provided config schemas | Optional adapters expose actual ranges, enums, descriptions and restart requirements; no invented constraints. |
| P1 | Config changes in Git | Explicitly include reviewed config files in managed commits; preserve secret/unrelated-file staging guards. Current Git save remains for the pack manifest. |
| P1 | Config change preview and restore | Show a diff before save, list timestamped backups, and restore only after an explicit action with conflict checks. |
| P1 | GitHub sign-in and sync | OAuth, repository creation, push/pull and conflicts through UI; credentials stay outside repositories. |
| P1 | Approved CurseForge downloads | Verify live integration after API approval, enforce distribution settings and resolve required dependencies through a provider adapter. |
| P2 | Dependency and incompatibility graph | Separate rules evaluate version constraints, duplicate/nested IDs, optional dependencies and explicit incompatibilities with evidence. Static results do not promise a successful game launch. |
| P2 | Game installation and launch | Resolve loader/Java versions and account authentication; display launch progress and failures without editing other launchers' data. |
| P2 | Richer mod identity and icons | Resolve nested-JAR identity/icons and verified provider artwork; retain a clear generic fallback. Cache with size limits and invalidation. |
| P2 | Log tools | Search/filter/copy, live tail with cancellation, gzip log archives and explicit privacy controls before sharing. |
| P2 | World metadata and backups | Read level.dat through a bounded NBT adapter, show actual world names/version/last played, and provide explicit backup/restore flows. |
| P2 | Config editing history | Undo/redo across search/filter changes, schema-aware list controls and richer nested objects/tables and comment-preserving collection edits. |
| P2 | Pack Overview / Changes tabs | Summarize pack metadata, health and reviewed changes in independent panels; avoid duplicating Content or Git state. |
| P2 | Import/export and provider merging | Standard Modrinth/CurseForge formats, reviewed identity mappings, license/distribution rules and gallery deduplication. |
| P3 | Accessibility and performance | Keyboard/screen-reader review, scaling, contrast, large-library benchmarks, paged config inventory and cancellable scans. |
| P3 | Distribution | Reproducible Windows packaging, dependency/license notices and releases; expand platform support only with real build/runtime verification. |

## Current editing limits

Ownership is an unambiguous filename/directory match, not a declaration from a mod. Unknown owners and unsupported formats remain inspectable without an Edit action. JSON is reserialized with its values preserved; TOML/properties patch supported values and preserve unrelated text. Complex values remain unchanged and are omitted from forms. No runtime schema, enum or range is inferred for JSON. Logs/worlds/files are inspection tools. Scans do not guarantee runtime compatibility. Backups are local application data and have no automatic retention policy yet.
