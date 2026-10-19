# Foundation architecture

## Ownership and flow

`ApplicationController` owns the UI-facing state and `CatalogueClient`. QML pages receive the controller through an explicit required `app` property. Provider HTTP work is asynchronous through `QNetworkAccessManager`. Search and detail generations reject stale responses. Git processes and installation discovery run on QtConcurrent workers. Repository mutations are serialized by the controller's repository-busy state.

`Project` retains provider IDs and source IDs. `mergeProjects` accepts only explicit `provider:id -> shared identity` mappings. If present, load mappings from `project-identities.json` in `QStandardPaths::AppConfigLocation`; example:

```json
{"modrinth:verified-id":"shared-project", "curseforge:12345":"shared-project"}
```

These mappings require review; filenames, slugs, titles and shared source repositories alone do not prove project equivalence. Modrinth wins presentation fields. `mergeReleases` groups exact SHA-1 references only when Minecraft and loader sets match, preserves both provider IDs, and sorts by publication time. Same-label different-file releases remain distinct.

`decodedImageFingerprint` hashes decoded RGBA pixels and dimensions, handling different lossless image containers. Resizing, lossy recompression, cropping, and integration of both galleries need a separate visual comparison service. The current UI's gallery is from Modrinth and deduplicates repeated URLs.

## Local storage

App identity and settings use stable `ModpackStudio` identifiers; the owner-selected display name is Conflux. Managed repositories live below `QStandardPaths::AppLocalDataLocation/packs`. Profiles owned by other launchers are inspected only and never imported or modified by this foundation. Modrinth SQLite opens read-only and queries public profile columns only. Folder scanning supports metadata copied into profiles. CurseForge folders use `minecraftinstance.json`. Metadata absent from Modrinth profiles may be shown as unknown; this does not infer a loader or game version.

`pack.json` schema 1 contains a UUID, display name, Minecraft version, loader, unresolved loader version, and exact mod release references. References retain hashes, provider IDs, compatibility metadata and required dependencies. `QSaveFile` writes manifests atomically. Pack creation uses exclusive mkdir and sanitized names, including Windows device-name avoidance. A collision is an error. A failed Git initialization may leave the newly created folder for inspection; no recursive cleanup occurs.

Git runs through argument arrays with interactive prompts disabled and timeouts. Save stages only `pack.json` and the pack's `.gitignore` and refuses a commit when other files have already been staged outside the app. GitHub authentication, remote operations, credential storage and conflict resolution are intentionally future boundaries.

## Integration boundaries

Modrinth API v2 requests identify the app. CurseForge requests use an in-memory approved key, with no persistent API catalogue cache. Rate-limit and HTTP errors remain visible; a partial merged result reports unavailable providers. This does not resolve CurseForge API licensing/approval questions.

This foundation pins primary files only, records required dependencies without resolving them, and does not download mods. It does not equate successful pinning with a runnable pack. Future installation work needs verified download hashes, dependency graphs, exact loader resolution, cancellable jobs, game authentication and import/export contracts.
