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
- Place hide/restore actions in content-entry three-dot menus. The later appearance request moves these menus to the far right.
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

## Owner configurable appearance and compact cards

- Format the contextual title as ModpackName - Exploring Content, without literal quotation marks.
- Move content menus to the far right, keep artwork equally inset on all sides, reduce default card height/padding and align the badge row with the artwork bottom.
- Order badges Platform, selected Loaders, Version, then Tags; exclude loader badges that are not selected by an active loader filter.
- Add configurable overall UI size, independent content and filter sizing, and a live Appearance preview. Keep size and spacing controls grouped.
- Allow creating custom themes and editing every semantic UI color in Appearance. Candy red #FF005E is the default launcher accent.

- Version showcases expose multi-select Release/Beta/Alpha channels, game versions and loaders, with Clear and source selection. Default to instance compatibility while allowing wider browsing; installs still validate the actual instance.
- Three-dot action buttons toggle their open menu closed on a second click. Discovery shows active filter chips, locking only instance-controlled selections. Keep routine explanatory paragraphs out of content views; retain meaningful errors and warnings.

- Popups take focus and block background interaction, including conflict-warning popups. Instance content tables have no outer outline; row hover fills the contiguous table cells inside one rounded outer frame. Unavailable actions, including Installed, have no hover response.

- Reserve scrollbar space beside cards and other scrollable content; clipped hover backgrounds retain rounded viewport corners. Align content author and title baselines. Use icon-led card statistics and configurable installed-content opacity with a live Appearance preview.
- Preserve source description imagery and useful markup in the native theme. Gallery images open in a focused enlarged viewer with navigation and zoom.
- Shared hover/press behavior must preserve control geometry and pill silhouettes. Search fields have an internal trailing clear button. In instance content, put Updates between Hidden and Update all, use a padded short divider after Filter and put Refresh at the far right.

- Keep card actions and statistics harmoniously aligned with consistent icon sizing. Instance-table hover fills the entire cell; partially visible bottom rows retain one clean outer corner without nested rounding or an end divider.

## Expanded configuration and settings organization

- Keep improving the GUI and populate settings with working configuration. Large categories, including Appearance, use tabs to avoid clutter.
- Appearance has Interface, Content, Filters and Colors tabs, section search, a collapsible live preview and scoped reset actions. Colors retain all semantic roles, organized into four palette groups.
- Configure navigation style, library/instance row density, scrollbar width, tooltip visibility/delay, reduced motion and individual card metadata alongside existing sizing/color controls.
- Browsing separates defaults from compatibility. Persist sort, page size, source and installed-content defaults; preserve visited views and saved instance compatibility unless the user explicitly applies defaults to the current view.

- Clicking outside an enlarged gallery image closes the viewer; image interaction and toolbar controls remain usable. At100% zoom the image should occupy roughly the previous83% size. Gallery fit is configurable in Appearance.

- Preserve whole-card hover while the pointer is over tags, downloads and other descendant controls.

- Discovery card tags and actions have no tooltips. A later owner instruction enables right-side download/follower/update statistics tooltips with exact counts and readable dates. Installed content is dimmed using Appearance opacity; uninstalled content stays at normal opacity. Resource and Data Pack discovery omit loader filters but retain platform/loader metadata tags. Version pages support OR platform selection including Data Pack, with searchable game-version menus, selected counts and inline clearing.

- Updates include compatible newer mods, resource packs, shaders and per-instance data packs. Update all uses the known update set and skips frozen/current content; explicit Refresh performs discovery. Mixed updates retain verified downloads, backups and dependency checks.
- Selection actions float over the list without moving it: Clear, Update, Share/copy names/files/links/Markdown, Enable, Disable and Delete. Routine operation progress/cancellation uses compact status icons; failures remain inspectable.
- Content names, authors and version labels underline on hover. Content names open the project, version labels open Versions, and authors open all their content with both source platforms combined through verified identities.

- Content already on its latest installed loader edition must not appear in Updates; Barehanded1.6.6 NeoForge versus its later-published Fabric build is the owner-reported regression.

- Hide Gallery entirely when a content project has no gallery images. Scrolling should feel smoother and travel farther per wheel step, while retaining strict scroll bounds.

The owner requests custom window chrome instead of the standard title bar, centered icons in their containing controls, and removal of redundant labels and descriptive chrome copy. Settings remains accessible; the sidebar tagline, WORKSPACE caption, Sources heading and Library marketing subtitle are removed. The existing unsaved-config close guard remains authoritative. Maximize, Snap, drag-to-restore and Restore must preserve valid window geometry and leave the taskbar visible; the minimum height is reduced to 480 logical pixels.

Content file actions must respond immediately in the UI, execute heavy work through a hidden queue and notify errors without disturbing the current page. Installing/importing content, enable/disable, removal and updates use optimistic state with reconciliation and rollback. Persistent content locks prevent Conflux updates, version switching, enable/disable and removal; locked content remains visible in Updates. Locked and recently updated filters are required. The implementation uses a seven-day window for versions changed through Conflux; this is a default implementation choice. Locks do not change external filesystem permissions.

The discovery page number is editable, with first, nearby and last page shortcuts. Right-side content statistics use lighter rounded tooltips for exact counts and readable update date/time. Display one Downloads icon and the sum of known platform counts; its tooltip separates each platform count. Download sorting uses the same total, preserving reviewed cross-platform identities and available provider access.

Content commands must respect both Conflux and source-launcher locks, reject concurrent changes to the same content, keep reads consistent with complete writes, and avoid duplicate transient rows. A missing locked original must have an exact-version recovery path. The custom title bar must expose running-instance status and current/failed content tasks with details and retry, without routine progress clutter.

Updates replace only selected newer compatible files without resolving dependencies again; they must preserve the instance Minecraft version and each content item's loader edition, including cross-compatible releases with multiple supported values. Initial-install dependencies remain separate. Use outlined controls and a small shared set of corner families, contain labels/actions at narrow widths, keep short statistic tooltips on one line and show concise dates without seconds. Search should tolerate compact version numbers and small omitted letters in names. Omit redundant Sort by prefixes. Add Changelog with Latest/All versions and exact-version links from Versions. Keep the interface simple, readable, responsive and efficient; review architecture and performance after these changes.

Bulk updates commit each content item independently. A failed or cancelled update must leave successful updates installed and must not prevent the remaining eligible items from running. Update all and Update selected skip locked, frozen and already pending content, including locks under either verified platform identity. Preserve atomic writes within each individual update. Keep the queue and optimistic interface responsive, detect selected-file changes while queued, and provide broad regression coverage for content types, protection policies, mixed outcomes, retries, cancellation and instance changes.
