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
