# Content-page performance

The critical path was serial: identify installed bytes or resolve provider projects; refetch metadata; wait for descriptions, all release lists and every gallery download/decode; then format and construct the page. This delayed readable content behind unrelated work.

The current path publishes the clicked header immediately, formats the primary description as soon as its verified project body arrives, prepares versions in the background, and publishes the gallery independently. Failed secondary providers remain visible without hiding usable primary content.

| Candidate | Decision and bounds |
| --- | --- |
| Caching and request batching | Opt-in presentation-only parsed JSON LRU, 60-second TTL, 16 MiB estimated cost. Identical GETs share one request; resolve/body/gallery metadata reuse that response. Failed/missing reads and POSTs are excluded. No authentication is persisted. |
| Memoization | Eight content-hashed parsed descriptions, each admitted only below 1 Mi characters. Release filter options/rows recomputed on input changes. 160 successful gallery fingerprints expire after five minutes. |
| Algorithms and allocations | Release grouping uses hash-indexed mutable provider buckets and one final concatenation. This removes growing shared-list detach/copy and insertion scans. Reserve known list/hash capacity; keep Qt shared value results instead of serializing them through JSON again. |
| Async CPU work | Large JSON parsing, description formatting, release presentation/changelog normalization and gallery pixel decoding/fingerprinting run on workers. Qt models and QML remain on their owning thread. |
| Non-blocking I/O and pooling | Existing asynchronous QNetworkAccessManager requests and long-lived per-service connection pools are retained. Gallery concurrency is four including decoding; changelog fetching retains its independent two-operation limit. |
| Progressive/lazy rendering | Seeded header, static themed skeleton and early body text. Versions/gallery/changelog use asynchronous Loaders only for the selected pane. Description scroll state survives tab changes. |
| Cancellation and lifetime | Generation/epoch checks prevent obsolete requests and worker results from changing the current page. Gallery replies are aborted on navigation. Metadata reads can complete to populate the short-lived cache. Worker watchers have QObject owners; recursive gallery pumping uses weak ownership. |
| Prefetching | Reuse work already requested by navigation; do not add hover-triggered API traffic or speculative downloads. Full gallery validation still fetches originals for verified pixel deduplication; sharing those bytes with Qt Quick's image cache is a possible later change. |
| Serialization/disk cache | Keep provider documents in memory only. Avoid serializing credentials or long-lived release/download snapshots. Downloads still undergo uncached exact file/hash/compatibility validation. |
| Object pools | Not introduced: Qt value sharing and bounded caches address observed allocation work without retaining arbitrary UI objects or adding lifecycle complexity. |
| Load balancing/scaling | Provider infrastructure is external; a local desktop app cannot improve its server topology. Avoid a proxy/service layer solely for these optimizations. |
| Optimistic UI | Preserve existing queued, optimistic file operations and rollback/error handling. Read-only page loading progressively presents verified data; it does not speculate about installation success. |

## Regression checks

`showcase-loading-tests` uses explicitly injected loopback HTTP servers and temporary image bytes. Held replies test ordering directly instead of assuming the Internet is fast: the description must appear while versions are held, versions must finish while gallery is held, and a secondary provider must not block the primary body. Reopening a warm page requires zero extra metadata/gallery HTTP reads. Tests also cover stale navigation, same-pixel deduplication, concurrency, cancellation, cache TTL/eviction, uncached failures/malformed JSON, default uncached non-presentation clients, credential changes during queued/in-flight reads, and event-loop progress during a large response.

Performance budgets cover grouping 8,000 same-version variants and 10,000 reads of results for 4,000 filtered versions. Budgets allow debug/instrumented builds; they are regression guards, not claims about real-provider latency. Build/test artifacts record actual timings. Existing rendered UI, changelog, provider, installer and transaction suites remain required. Whole-process leak freedom is not inferred from ownership checks, and no new coverage percentage is claimed.


Observed on 22 October 2026 (Qt 6.10.1 / GCC 13.1 / Release): the focused suite reported 8,000 variants grouped in 18 ms and 10,000 cached reads over 4,000 release rows in 1 ms. All 24 CTest suites passed; the catalogue UI suite has 44 passing outcomes. The standalone package passed full application integration without Qt on PATH. Logs: artifacts/progressive-content-loading-tests.txt, progressive-content-final-ctest.log, progressive-content-rendered-tests.txt and progressive-content-verified/smoke.log. These are local fixture measurements, not end-to-end Internet latency or a new coverage measurement.

## Discover request amplification and badge costs

Discover now starts independent provider pages concurrently. Linked platform metadata and download totals are hydrated for every sort mode in exact-ID batches of up to 50 projects per provider/content type, using a reverse index of reviewed aliases plus verified response identities. Missing metadata errors remain visible; credential/filter generations reject old replies. This preserves combined-download ordering and Modrinth presentation while avoiding serial per-project lookups.

BrowseInstalls builds a provider/category index when its inventory changes. Card badge reads perform direct lookups and identical context notifications do not re-enrich rows or emit another model revision. Disabled content, destination-specific identity, exact-file pins and lock state remain authoritative.

New regression budgets exercise 50 paired projects behind simulated provider latency in download/relevance sorts and both single-platform directions, cap requests at three, verify combined counts, require independent provider reads to start before either reply, and verify cached context restoration performs no HTTP reads. Badge tests read 10,000 states over 5,000 installed mods within a one-second debug-friendly budget and require 100 identical inventory notifications to emit zero changes. These request/state assertions supplement wall-clock budgets and the existing rendering/cache/grouping tests.

The reported Sound Physics Remastered update is covered through the real ContentActions-to-ContentCatalogue path, including a zero-argument Refresh invocation. Its installed 1.5.1 release is alpha and 1.5.2 is beta; both support Minecraft 1.21.1/NeoForge (1.5.2 also lists 1.21). Update checks now default to Release for every installed stage; the owner removed Automatic on23 October2026. Beta/Alpha require explicit per-modpack selection, including for this Sound Physics beta update. Both providers follow that policy; game/loader membership, exact hashes and publication/newer-version checks remain required. Stable installations do not silently opt into prereleases.

Observed 22 October 2026 (Release, deterministic 20-ms provider delay): the pre-fix 50-pair download-sort case made 51 requests and took 1,564 ms. The batched implementation made two requests and took 113 ms. Ten thousand indexed badge reads over 5,000 installed files took 30 ms; 100 identical inventory notifications emitted zero changes. These local fixture measurements deliberately exclude Internet variability and do not claim those timings for live providers. The delay test also requires event-loop heartbeats while requests are pending. The exact Sound Physics 1.5.1 alpha -> 1.5.2 beta Refresh test was observed failing before the channel fix, then passing after it.


### Release-stage policy regression budgets

Policy reads are cached per normalized instance path; repeated UI checks do not reread settings or emit notifications. Rapid selection changes coalesce into one150ms-delayed update refresh, invalidate old response generations and preserve installed rows. Explicit policy groups Modrinth requests by effective stages rather than the former installed channel. Tests exercise all seven nonempty selections across both providers, legacy-empty/default transitions, individual/bulk/selected install entry points, queued revalidation, unknown stages and blocked dependencies. These checks do not claim complete application coverage.


### Lossless source-lock metadata patch budgets

The CurseForge lock patcher indexes addon paths and provider IDs once, deduplicates target byte spans, sorts unique replacements and constructs one output buffer. It no longer rescans all addons per target or repeatedly shifts the metadata buffer. Ordinary JSON keys use a direct UTF8 decode; escaped keys retain Qt decoding. The bounded token walk avoids temporary byte arrays per character and rejects duplicate/shadowed lock fields regardless of value type.

A deterministic fixture covers5,000 addons,1,500 target entries and1,000 unique changes with exact expected output, including nested unrelated lock fields. The Release build completed the patch in27ms under a3-second regression budget. Duplicate targets, escaped keys, duplicate source fields, stale/null error outputs and malformed metadata have independent correctness checks. This is an in-memory fixture measurement, not live launcher latency.


### Indexed source-lock UI reads

SourceLockIndex builds provider-qualified project lookups when accepted instance metadata changes. ContentActions reads the index from UI-facing sourceLock calls; it does not scan every source file per card. Accepted state is kept separately from captured project targets, and initial operation state is seeded with the latest accepted source metadata. Raw/loading contexts cannot erase a known snapshot; an explicit empty snapshot clears it. Provider collisions, original-instance navigation and pending lock intent retain their scope.

The deterministic5,000-entry fixture made2,000 source-lock reads in2,995ms before the change and2ms after it, within a1-second regression budget. A separate before-fix failure reproduces a captured target resurrecting an obsolete source lock; accepted-state and captured-install regressions now pass. These are local CPU/state measurements, not provider or disk latency claims.


### Obsolete version lookup cancellation

A deterministic deferred-provider regression holds an old version callback indefinitely. After cancellation, the next eligible request must start within500ms without releasing that old callback. A loopback integration fixture verifies that the held HTTP version socket is aborted while an unrelated update-identification socket stays connected and completes normally. Scoped queue tests preserve installs for other instances. Cache tests cover coalesced requests, reused request keys, queued cached responses and cancellation inside a callback. These enforce local scheduling and isolation rather than live provider latency; the20-second transfer timeout remains an independent bound.


### Release-stage eligibility read budgets

releaseStageAllowed checks the three canonical provider stages directly, defaults an empty selection to Release, and uses exact selected-stage lookup before the existing trim/lowercase compatibility fallback. It no longer constructs effective and supported stage lists for each boolean read. Normalization and serialization still use the shared canonical list; unknown provider stages remain rejected.

The1.6-million-read all-stages fixture took580ms before this change and8ms after it on this workstation. The regression budget is250ms and also exercises Release-only and empty/default selections. Separate equivalence cases compare the optimized predicate with effectiveReleaseStages for unusual selections, duplicates, case, whitespace, Unicode and unknown candidate stages. These are local CPU measurements, not download latency or application-wide allocation counts.


### Operation reservation claim budgets

ContentOperationReservations replaces pending-operation overlap scans with an instance-scoped hash index. Checking and acquiring a command is proportional to its reservation keys; release checks ownership before removing keys. Deriving target keys still scans the requested instance rows and is not claimed as an indexed inventory lookup. Inventory, pending command and rendered projection state remain outside the reservation component.

A deterministic fixture acquires12,000 two-key operations, rejects12,000 conflicts and releases the claims in8ms under a1,500ms budget on this workstation. A separate3,000-step seeded sequence compares results with an ordered reference through mixed acquisitions and cancellations. Integration tests reproduce and fix background commands borrowing visible-instance aliases, and verify queued cancellation releases its own claims while active work remains protected. These measure in-memory claim handling, not end-to-end bulk update latency.


### Constant-time operation HUD counts

OperationHistory maintains active and failed totals when its single-threaded model inserts, changes, prunes, dismisses or clears entries. Getter calls return those totals without scanning task maps or constructing status lists. Counters are updated with the underlying row before completion notifications, so model observers see the same counts as the current entries. Task ordering and persistent indices retain the existing incremental model operations.

The deterministic900-task fixture performs20,000 reads of each count. It took5,397ms before this change and less than1ms at millisecond timer resolution afterward, under a250ms regression budget. Transition tests independently recompute counts from entries during model and feature notifications, including repeated completion, reactivation, missing IDs, pruning and clearing. This is a read-path CPU result; insertion/reordering costs remain separate work.


### Bounded task ordering searches

OperationHistory locates insertion/move destinations with binary search over the existing sorted rows. A changing row is omitted through logical indexing while the remaining rows retain their ordering. Running tasks keep descending IDs, queued tasks ascending IDs and finished tasks descending IDs. Native model insertion/move signals, count deltas and persistent-index behavior remain unchanged. Array movement and ID lookup are separate costs; this change bounds only destination searches.

A deterministic5,000-task insertion fixture took1,795ms before the change and13ms after it, within a500ms regression budget. A separate reference sorts its own state map through out-of-order insertion and1,200 seeded start/failure/success/dismiss/progress transitions, including inactive pruning and final clearing. Existing model-observer count checks and persistent-index tests also pass. These are local CPU measurements, not end-to-end download/update times.


### Bounded preference identity-key reuse

SettingsContentPreferences reuses the three immutable settings keys derived from an exact instance/content identity pair. The keys retain the original SHA256 input bytes and frozen/locked/updated prefixes, including explicit false values and path fallback identities. A per-reader QCache uses a1MiB estimated entry-cost budget, evicts least-recently-used keys and bypasses entries larger than that budget. This is a bounded cost estimate rather than a measured allocator maximum.

Every preference value is still read from QSettings; negative/default or populated values are never cached. Tests preserve legacy keys, Unicode paths, independent instances/providers, current writes across readers,6,000 identities through eviction and oversized-key fallback. The800,000-read single-identity fixture improved from922ms to217ms on this workstation under an800ms budget. First-time reads still derive keys and normal settings synchronization behavior is unchanged.


### Stable content key and constant mapping read budgets

contentRowKey now returns an explicit stored key before constructing its category/path fallback, retaining empty, null, numeric and opaque key behavior. Fallback field names use constant QString literals. resolveContentTargets returns immediately if either input is empty; nonempty selection still retains inventory order and duplicate rows. Content type/category maps and the loader list are immutable function-local constants; label prefix checks avoid constructing a new prefix string per loader. Case, unknown fallback and version suffix behavior are unchanged.

Deterministic fixtures improved500,000 stable-key reads from171ms to11ms,800,000 type/category reads from378ms to22ms,350,000 labels from151ms to35ms, and1,000 empty selections over5,000 rows from1,844ms to below1ms at timer resolution. Budgets are100/150/125/150ms respectively. The existing300-update acceptance fixture improved from82ms to19ms after these helper changes while retaining one batched projection and a stable model. These are local CPU observations; nonempty target resolution still scans the inventory and is explicitly backlogged for separate measurement/indexing.


### Indexed loaded-content presentation

ContentViewIndex retains one immutable inventory snapshot and caches normalized search strings/words, author choices, filter fields and lazy sort orders. Changing a query or filter reads those prepared entries without consulting visibility preferences or reconstructing metadata. Inventory allocation identity, instance path and explicit visibility revision invalidate the presentation snapshot. Compact version and omitted-letter matching preserve the original TextQuery behavior, including Unicode normalization.

ContentListModel removes obsolete rows in contiguous groups, builds a keyed survivor order, remaps persistent indices in one layout change for actual reordering, inserts contiguous new runs and emits grouped data changes. Duplicate/empty keys retain occurrence order. It avoids scanning QVariant row maps once for every target; persistent identities survive filtering and sorting without a model reset.

Local before/after measurements:36 filter transitions over310 rows improve67ms to3ms;3,000 rows improve3,499ms to51ms. Reversing5,000 model rows improves1,998ms to3ms. The rendered offscreen/software310-row fixture completes24 filter transitions in186ms with no additional catalogue reads, then verifies a rendered frame. These are local CPU/rendered fixture observations, not provider latency or disk-cache measurements. Randomized model/reference reconciliation and QAbstractItemModelTester validate120 mixed insert/remove/reorder/change states plus duplicate keys and persistent indices. Persistent cross-session metadata indexing remains the next implementation slice.
