# Minecraft Modpack Manager

The owner selected the display name Conflux. The internal ModpackStudio identity and workspace path are stable storage/build identifiers.

When 2ndNature is installed, use the installed agent-knowledge skill to locate its entry point and retrieve this project's knowledge:
`python "<2ndNature-root>/knowledge.py" context "task topic" --cwd "." --database minecraft-modpack-manager`
Detailed owner requirements are in `requirements/initial-owner-priorities`; use subject-specific search for implementation records. Public contributors can use `docs/REQUIREMENTS.md` and `docs/ARCHITECTURE.md` without the local knowledge harness.

Follow the installed agent-knowledge skill. Declare changed files with source alias `project`, update reviewed implementation records and complete the knowledge checkpoint with actual verification.

Preserve Modrinth priority, both provider identities, exact file hashes and compatibility metadata. Never merge projects by title or slug alone. Launcher metadata, worlds and logs remain read-only in Conflux. On 2026-10-20 the owner authorized explicit content import, enable/disable, removal, version switching and updates, including launcher installations; use the dedicated content ports, backups, hash checks and staged dependency transactions. Never rewrite another launcher's database or manifest to simulate an API. The owner also authorized visual editing of identified mod configs: only explicit Save actions may write supported config files, with backups and optimistic external-change checks. Never commit downloaded jars, worlds, caches or credentials. Provider/API errors must remain visible. No embedded API keys or invented working authentication.

Always rebuild, verify and reopen Conflux after changes. The owner explicitly requested this on 2026-10-19.
Keep all pack sources together in Library, with a source tag on every pack. Persist hidden packs. Onboarding depends on visible packs across all sources. OLED is the current default theme. Themes use the small JSON token groups in themes/oled.json and themes/dark-grey.json; keep hardcoded UI colors out of QML. Shared buttons must distinguish actions from persistent selection and expose hover, pressed, disabled and keyboard-focus states.

Follow SOLID rigorously. Organize each feature around one reason to change; keep parsing, filesystem access, diagnostic policy, preferences, navigation, and QML presentation separate. Inject narrow interfaces into consumers. Extend codecs and diagnostic rules through their contracts; never put format branches or filesystem writes into views. DesktopSession is the composition root, not a place for business rules. CMake targets enforce the content/config/storage/desktop dependency direction. Prefer clear contracts and maintainable composition over convenience shortcuts. Maintain docs/BACKLOG.md with concrete acceptance criteria for unfinished additions. Preserve unsupported config fields and original comments where the format adapter supports lossless edits. Never silently infer a config schema from a filename.
