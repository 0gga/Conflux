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

## Owner description, navigation, config and artwork correction

- Render readable native descriptions despite provider HTML/font/palette styling.
- Mouse Back/Forward traverses pages, not tab changes.
- Duplicate-ID warnings must identify the conflicting files; dependencies must not be mistaken for owning mod IDs.
- Investigate Catalogue/Configured and expose usable launcher-side visual config controls instead of a raw file editor.
- Replace the UI icons with a simple, prominent and consistent set.
- Content artwork fills its rounded square edge to edge, without a padded background box.

## Owner instance browsing and rounded catalogue request

- Rename Explore mods to Explore content and keep the clicked instance as the installation target.
- Offer Mods, Resource Packs, Data Packs and Shaders tabs, as shown in the supplied reference.
- Include an Install action on each project and mark installed project/version state only for the selected instance.
- Keep canonical IDs internal; display Fabric, Forge, NeoForge, Quilt and other loader names with correct capitalization and recognizable colored icons.
- Use the supplied project-card layout with large rounded artwork, author, summary, tags and per-platform download totals/icons. Never add provider download totals together.
- Keep subtle rounded edges consistently on cards, controls, menus and artwork.
- Propose convenience/features for later owner review; proposals are distinct from delivered work.

- Owner correction: Minecraft version and loader use adjacent matching pills wherever compatibility is displayed; Library details stay aligned. Every scroll surface must stop at content boundaries with no overshoot/rebound.

## Owner paged browsing and compatibility filtering

- Manage Data Packs per instance, with no per-world browse target.
- Add pagination, selectable entries per page and sorting by relevance, downloads, followers, publication date and update date. Restore the page and scroll position after viewing content.
- Place hide/restore actions in three-dot menus at the far left of content entries.
- Show project tags and provide tag, game-version and loader filters on the right. Versions and loaders each accept multiple alternatives using OR.
- Allow unlocking the instance-derived browse filters to change selected versions/loaders.
- Detect installed Connector and optionally browse primary-loader plus Fabric content; expose the preference in Settings.

The source statements and reference images distinguish these requirements from implementation constraints or proposed backlog ideas.

- Show verified source platforms alongside content tags using icons only; loader metadata also uses icon-only badges, retaining accessible names and hover labels.

## Owner compact Explore and remembered compatibility correction

- Move the source selector beside the pagination controls; remove the page-entry count and routine missing-CurseForge-key notice.
- Reduce the Explore title and top padding. Match the instance selector width and alignment to the right filter panel.
- Loader filters are editable immediately, independently of the game-version lock.
- Keep Connector-assisted Fabric selection in Settings; remembered manual loader choices take priority over defaults.
- Remember game-version and loader filters separately for every instance and content type, including across app restarts.

- Follow-up: move Explore title/context to the top bar as Exploring content for "instance name", remove the duplicate page heading/subtitle and top-right Explore/New modpack actions, and keep instance selection aligned over the filters.
- When versions are unlocked, show a bounded scrollable multi-select list of all API-provided versions instead of a dropdown. Tooltips share rounded, theme-aware presentation.
