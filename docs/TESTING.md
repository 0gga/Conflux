# Verification and coverage

Run `scripts/build.ps1` for the release build and all CTest suites. `-Deploy` also updates the packaged application. UI checks run offscreen; native frame checks use the secondary monitor without activation or global pointer injection.

For read-only live provider checks, set `MODPACK_STUDIO_LIVE_TEST=1` and run `build/foundation-tests`. CurseForge additionally requires an approved `CURSEFORGE_API_KEY` in the process environment; otherwise its test skips. The live test uses the production Qt adapter to check catalogue identity/counts/authors, Minecraft/loader-compatible release metadata, hashes, changelogs and facets. It never installs content. Keys remain session-only: use the masked Connections field or a securely supplied process environment, never a checked-in file, command-line argument or test report. Live API availability is separate from offline regression results.

Run `scripts/coverage.ps1` for an isolated GCC-instrumented build and all tests. The release build and packaged app are not instrumented. Tools and build output stay under `artifacts/`; gcovr is pinned in `scripts/coverage-requirements.txt` and installed locally, without modifying global Python packages.

Reports under `artifacts/coverage/current/` include per-file HTML, JSON, text and Cobertura XML. Source C++ line, function and branch coverage excludes Qt/generated code, tests and third-party dependencies. Compiler exception/unreachable branches are excluded explicitly. QML is not counted as C++ coverage: rendered interaction, geometry and pixel assertions validate it separately.

Use `-ReportName baseline` to preserve a baseline report, `-SkipBuild` to rerun an existing instrumented build, or `-Filter 'src/features/explore/BrowseCatalogue.cpp' -FailUnderLine 100` to enforce complete measured line coverage of that source. `-TestRegex` selects CTest suites; filtered runs measure only the chosen tests. Unexecuted paths remain visible and must not be labelled covered merely because a scenario was listed.

Use `-IncludeSmokeTest` to add the offscreen application integration flow to the measured run. It uses a fresh, test-owned temporary instance and live public Modrinth requests, so it is an optional network integration check rather than an offline unit-test dependency. It never opens a native window or changes owner instances. Report the tests-only and integration-inclusive scopes separately. Header code called from test executables is instrumented as well; reports still include only `src/`.
