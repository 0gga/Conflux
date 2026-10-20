# Owner requirements

Source: direct owner requests in the 2026-10-19 Codex conversation, preserved in 2ndNature subject `minecraft-modpack-manager`, record `requirements/initial-owner-priorities`.

- Build a fast app with a clean interface for creating Minecraft modpacks.
- C++ and Qt were proposed by the owner; proceeding with C++20 and Qt Quick is the foundation implementation choice.
- Modrinth is the primary reference for UI, functionality and overall experience.
- Show installations from both Modrinth and CurseForge apps.
- Use publicly available APIs from both platforms.
- Make each modpack its own user-owned Git repository.
- Provide GitHub login and all normal Git operations through the UI.
- Offer merged, Modrinth-only and CurseForge-only content views.
- Display one entry for the same mod across platforms; always prioritize Modrinth when merging.
- Use Modrinth descriptions for shared projects.
- Combine galleries and show only unique images; combine comments and other available content.
- Merge equivalent releases, prefer Modrinth when both have them, and retain releases exclusive to either platform, including newer ones.
- First build a reviewable foundation, then iterate from owner feedback.
- Naming should imply a combination of technologies or a portal for mods. The owner rejected Packsmith and selected Conflux: technologies and sources coming together.

API limitations and release/project equivalence are implementation constraints, not permission to erase a requirement. No Minecraft version, loader, or long-term operating-system scope has been selected as a product constraint. Form defaults are examples.


## Owner continuation, 2026-10-20

- Remove the Library Configure button; opening a pack remains a row action.
- Add direct saved-world launch from Worlds, similar to Modrinth.
- Clicking an installed mod or catalogue project opens a native description/gallery/version showcase with Modrinth priority.
- Settings uses a vertical category index instead of horizontal tabs.
- Follow the supplied Modrinth content-layout references: warning indicator beside the pack name, full-width search, Upload files, Browse content, sorting, category filters and Update all. Share is excluded for now.
- Include Author, Environment, State, Update available, Open source and External filters.
- Rows include selection, update/switch version, enable/disable, remove, and a menu with Show file, Copy link and Freeze version.
- Choose an appropriate automatic/manual refresh combination. The implementation uses debounced local filesystem events plus explicit provider Refresh.
- These requested content actions authorize explicit file changes; discovery and launcher metadata, worlds and logs stay read-only. Runtime and API availability remain explicit implementation constraints.
