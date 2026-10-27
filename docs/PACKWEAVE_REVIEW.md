# Packweave source and feature review

Reviewed 26 October 2026 at upstream commit `ad9f6db7adb4e14bd401aa9cffd6f3de1ea0e75f`. References: [feature overview](https://packweave.com/#mods), [pinned source](https://github.com/packweavers/packweave/tree/ad9f6db7adb4e14bd401aa9cffd6f3de1ea0e75f). The public website describes intended capabilities; source inspection below confirms particular mechanisms, not end-to-end runtime correctness. Upstream was not built, installed or run. The reference checkout stays under ignored `artifacts/packweave-reference/`.

Owner update27 October2026: implement the advertised features, starting with bulk add in the existing dependency overlay. The comparison below is an implementation roadmap; the exhaustive technical audit is not a prerequisite. Preserve earlier owner requirements, including optional Git.

This began as an inspiration and gap review. No upstream code or assets were added to Conflux. The repository identifies its code as GPL-3.0 and reserves its branding separately in COPYING.md. Any future direct reuse needs its own dependency/license decision; the proposals here describe independently implemented behavior using Conflux's existing ports.

## Product comparison

| Area | Packweave reference | Conflux disposition |
| --- | --- | --- |
| Pasted batch import | Names, provider links and archive filenames; per-line found/already-present/unresolved results | PW-01 now implements a checked review list and per-entry Next match using the shared dependency overlay and existing independent install queue. Filename/name matches are suggestions, never proof of installed or cross-platform identity. |
| Dependency assistance | Required/optional relations, update notes and problems with direct actions | Relationships, optional overlay, explicit cleanup and changelogs already exist. Add typed remediation commands under PW-02; diagnostics never veto an otherwise valid selected install. |
| Troubleshooting | Dependency-aware binary search with persistent progress and restore | Planned PW-03. An explicit, reversible session must honor locks, running games, external changes and uncertain multi-mod interactions. |
| Two-way instance sync | Push/pull choices and a separate both-sides-changed section | Conflux operates directly on launcher instances today. PW-04 applies only to a future distinct authored-pack/instance pair; never manufacture a second copy or rewrite launcher manifests to imitate sync. |
| File/config tools | Form/text editing, pack-wide search/replace and player-edit preservation | Visual config editing exists. PW-05 extends it with previewed multi-file changes and explicit preservation policy; unsupported fields/comments and external edits stay protected. |
| History and undo | Git history plus bounded in-memory undo snapshots | Git remains optional in Conflux. PW-06 builds on backups and the existing crash-journal backlog; explicit restore must be hash-checked and survive restarts. |
| Export and publishing | Multiple pack formats, client/server outputs, generated changelogs and publishing | Existing import/export/profile proposals gain PW-07 acceptance criteria. Credentials and publish actions remain separate from local artifact generation. |
| Player updater | Launch-time updater and player-edit preservation | Requested PW-08, sequenced after export and durable recovery; it remains optional for ordinary editing and installing. |
| Navigation | Unified command palette, commands/content/files/instances and shortcut choices | Existing command-palette proposal gains PW-09. Use Conflux literal search, keyboard focus restoration and immediate local results. |
| Platform/source handling | Multiple sources per content item, preferred source, side metadata | Conflux already retains verified provider links, exact hashes and compatible release policy. Export must preserve these identities; title equality is insufficient. |

## Implementation lessons

**Separate parsing, lookup and accepted mutations.** `src-tauri/src/bulkadd.rs` keeps input parsing and candidate scoring separate from `commands/content_ops.rs::bulk_lookup`. Results retain the input line and distinguish guesses from exact lookup results. Conflux should adopt that separation and a stable row identity. The reference parser recognizes provider-domain substrings, and bulk lookup uses `join_all` across all lines; Conflux should instead validate URL scheme/host with QUrl, bound input size and request concurrency, deduplicate verified identities, cancel obsolete lookups and keep successful entries when another line fails. Do not import guessed archive versions as exact release pins.

**Keep diagnostic fixes typed.** `validate.rs::Fix` represents remove/update/enable/unpin choices, while `src/panel/Problems.svelte` dispatches them. Extend Conflux's independent diagnostic rules with a typed, captured command descriptor. Revalidate original-instance inventory, locks and reservations at execution through ContentActions; QML must not write files or infer commands from error prose. Optional author/artwork failures remain quiet. Missing/disabled dependencies can offer actions without blocking the selected mod's independent install.

**Model troubleshooting independently of the screen.** `src/lib/debug/bisect.ts` contains dependency closure, partitioning, answers and backtracking; `state/bisect.svelte.ts` performs instance changes and persists progress. This separation supports deterministic headless tests. Conflux should index forward/reverse edges rather than repeatedly scan every dependent, persist exact original hashes and enabled state, and treat inseparable dependency groups as groups. Binary search assumes a reproducible symptom; interaction bugs and inconsistent answers require an inconclusive result, not a guaranteed culprit. Restore only session-owned changes, and preserve externally changed files.

**Use an agreed baseline for sync.** `syncbase.rs` persists a per-pack/instance fingerprint map; `sync.rs::classify` compares both sides with it. That is a useful three-way comparison model. Its no-baseline fallback uses modification times to select a direction. Conflux should show ambiguity for differing bytes without an agreed baseline, retain deletion tombstones and update the baseline only after confirmed writes. Rename, deletion, clock skew and interrupted application need fixtures. This is future sync design, not permission to expand current launcher metadata writes.

**Budget caches and distinguish freshness from identity.** `modrinth.rs` owns a reusable HTTP client, an eight-permit request semaphore and category-specific TTLs; `resolver.rs` processes dependency layers with bounded `buffer_unordered`. `cache.rs` wraps serialized values with timestamps. Conflux already has QNetworkAccessManager reuse, bounded in-memory response caching, coalesced metadata reads, persistent SQLite installed metadata and immutable file-hash reuse. Extend these rather than add another generic cache. Compare cold/warm/restart navigation, cap disk bytes/entries, version schemas, isolate provider/auth/query keys, expire negative results and make explicit refresh invalidate mutable update/version lists. Confirmed artifact identity can be durable; provider metadata is mutable. Background read parallelism must not widen concurrent mutation claims or make all installs one transaction.

**Keep undo bounded and durable where promised.** `undo.rs` separates snapshot capture from restore and caps history at 40 entries / 256 MiB. The inspected snapshots are held in memory and restore with ordinary file writes; that is not crash recovery. Conflux should retain its staged atomic writes, exact hash checks and source-lock ports, use bounded on-disk recovery records, and never capture entire worlds or credentials. Persistent recovery and user-facing undo are separate contracts.

**Make commands discoverable without another search system.** `src/lib/palette.ts` groups commands, content, files, branches and instances and filters unavailable commands. Its fuzzy scoring conflicts with the owner's latest literal-search instruction. Reuse TextQuery, build local results from loaded indexes, expose provider search explicitly and bound asynchronous results. Keep shortcuts and command availability in one registry shared by buttons and the palette.

**Treat release outputs as separate artifacts.** `release.rs` sorts archive entries, `preserve.rs` models path/setting preservation, and provider abstractions retain side/dependency metadata. Conflux can use explicit export plans with reproducible ordering, exact manifests, client/server inclusion, redistribution rules and per-destination results. Local export must work with Git and publishing disabled. Player preference preservation is separate from immutable content locks; do not conflate them.

## Order and verification

Owner-prioritized delivery order: PW-01 bulk add, PW-02 actionable dependency fixes, then the requested file/history/undo, troubleshooting, sync/import/export/publishing, updater and palette capabilities. Implement prerequisites with their consuming feature; keep Git optional. Audit work remains open and receives appropriate checks without blocking product delivery. All advertised capabilities are requested, not merely inspiration candidates. Do not label an unimplemented feature delivered.

Each item needs pure policy/parser tests, worker/provider fault and cancellation fixtures, original-instance/reentrant command tests, cold/warm and large-input performance evidence, and rendered keyboard/narrow-layout checks when UI is added. Use temporary test-owned packs only. Passing current Conflux suites does not validate unimplemented proposals or Packweave itself. Full non-UI and later UI coverage remain open targets.


## Feature parity scope after the owner clarification

| Capability | Current scope / remaining work |
| --- | --- |
| List import and alternative matches | PW-01: names, project links and filename suggestions; checkboxes, exact-match priority, Next match, independent compatible installs. |
| Actionable dependency problems | PW-02: existing relations/optional and removal overlays plus typed Enable/Install/View/Remove actions. |
| File authoring | Forms exist; text editing, pack-wide search/replace and player-preserved settings remain requested. |
| Instance workflow | Modrinth handoff/log reading and direct instance editing exist; Prism/MultiMC/vanilla support and reviewed two-way sync remain requested. |
| History and collaboration | Git stays optional; config staging, branches, push/pull, conflict review and durable undo remain requested. |
| Troubleshooting | Persistent dependency-aware binary search and conflict-aware restoration remain requested. |
| Export and release | Modrinth, CurseForge, Prism, client/server artifacts; generated changelogs and per-destination publishing remain requested. |
| Player updates | Optional launch-time updating from release feeds/custom URLs, with player-setting preservation, remains requested. |
| Navigation and imports | Command palette/keymaps and standard archive/packwiz/Git import remain requested. |

The public overview and upstream HEAD were rechecked27 October2026; HEAD remains `ad9f6db7adb4e14bd401aa9cffd6f3de1ea0e75f`. New Conflux code is independently implemented; no upstream source/assets were copied.
