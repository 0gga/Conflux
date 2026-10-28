cmake_minimum_required(VERSION 3.24)
project(ModpackStudio VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(CONFLUX_ENABLE_COVERAGE "Instrument project C++ sources for GCC coverage" OFF)
if(CONFLUX_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "The coverage preset requires GCC and its matching gcov tool.")
    endif()
    add_link_options(--coverage)
endif()
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Concurrent Sql Test Svg)
qt_standard_project_setup(REQUIRES 6.8)
qt_add_qml_module(conflux_style STATIC URI Conflux.Style VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Conflux/Style
    SOURCES src/theme/ContentTime.h src/theme/ContentTime.cpp src/theme/ControlStyle.h src/theme/TextSearch.h src/theme/Theme.h src/theme/Theme.cpp src/theme/Branding.h src/theme/Branding.cpp
    src/theme/ThemePalette.h src/theme/ThemePalette.cpp src/theme/ThemeStore.h src/theme/ThemeStore.cpp
    src/theme/Appearance.h src/theme/Appearance.cpp src/theme/AppearanceStore.h src/theme/AppearanceStore.cpp
    RESOURCES themes/dark-grey.json themes/oled.json themes/brands.json)
target_link_libraries(conflux_style PRIVATE Qt6::Gui)
target_include_directories(conflux_style PUBLIC src/theme PRIVATE src)
add_library(miniz STATIC third_party/miniz.c third_party/miniz_tdef.c third_party/miniz_tinfl.c third_party/miniz_zip.c)
target_include_directories(miniz PUBLIC third_party)

qt_add_library(pack_foundation STATIC
    src/domain/Catalogue.h src/domain/Catalogue.cpp src/domain/CatalogueSearch.h src/domain/ReleaseStages.h
    src/platform/ReviewedAuthorIdentities.h src/platform/ReviewedAuthorIdentities.cpp
    src/platform/ProjectAuthors.h src/platform/ProjectAuthors.cpp
    src/platform/ReviewedProjectIdentities.h src/platform/ReviewedProjectIdentities.cpp
    src/services/CatalogueClient.h src/services/CatalogueClient.cpp
    src/services/JsonResponse.h src/services/JsonResponse.cpp
    src/services/GalleryImages.h src/services/GalleryImages.cpp
    src/services/InstallationDiscovery.h src/services/InstallationDiscovery.cpp
    src/services/ModInstaller.h src/services/ModInstaller.cpp
    src/platform/VerifiedDownload.h src/platform/VerifiedDownload.cpp
    src/services/PackRepository.h src/services/PackRepository.cpp)
target_include_directories(pack_foundation PUBLIC src)
target_link_libraries(pack_foundation PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql Qt6::Concurrent)
target_link_libraries(pack_foundation PRIVATE miniz)
qt_add_library(pack_storage STATIC src/platform/PackFileStore.h src/platform/PackFileStore.cpp src/platform/ContentAccess.h src/platform/ContentAccess.cpp
    src/platform/ContentFileCache.h src/platform/ContentFileCache.cpp)
target_include_directories(pack_storage PUBLIC src)
target_link_libraries(pack_storage PUBLIC Qt6::Core)
qt_add_library(conflux_content STATIC
    src/features/content/ContentDependencyGraph.h src/features/content/ContentDependencyGraph.cpp
    src/features/content/ContentOperationState.h src/features/content/ContentOperationState.cpp
    src/services/PackContent.h src/services/PackContent.cpp
    src/features/content/ArchiveReader.h src/features/content/ArchiveReader.cpp
    src/features/content/ModMetadataReader.h src/features/content/ModMetadataReader.cpp
    src/features/content/ContentInventory.h src/features/content/ContentInventory.cpp
    src/features/content/ConfigAssociation.h src/features/content/ConfigAssociation.cpp
    src/features/diagnostics/DiagnosticRule.h src/features/diagnostics/DiagnosticRules.cpp)
target_include_directories(conflux_content PUBLIC src PRIVATE third_party)
target_link_libraries(conflux_content PUBLIC pack_storage pack_foundation Qt6::Gui PRIVATE miniz)
qt_add_library(conflux_config STATIC
    src/features/config/ConfigEditor.h src/features/config/ConfigEditor.cpp
    src/features/config/ConfigCodec.h src/features/config/JsonConfigCodec.cpp
    src/features/config/TomlConfigCodec.cpp src/features/config/PropertiesConfigCodec.cpp
    src/features/config/ConfigRepository.h src/features/config/ConfigRepository.cpp)
target_include_directories(conflux_config PUBLIC src PRIVATE third_party)
target_link_libraries(conflux_config PUBLIC pack_storage PRIVATE Qt6::Concurrent)
qt_add_library(conflux_desktop STATIC
    src/DesktopSession.h src/DesktopSession.cpp
    src/features/content/ContentUpdates.cpp
    src/features/navigation/CommandPalette.h src/features/navigation/CommandPalette.cpp
    src/features/navigation/NavigationHistory.h src/features/navigation/NavigationHistory.cpp
    src/features/navigation/NavigationInput.h src/features/navigation/NavigationInput.cpp
    src/features/preferences/WarningPreferences.h src/features/preferences/WarningPreferences.cpp
    src/features/workspace/PackWorkspace.h src/features/workspace/PackWorkspace.cpp
    src/features/workspace/PackFileBrowser.h src/features/workspace/PackFileBrowser.cpp
    src/features/workspace/PackLogViewer.h src/features/workspace/PackLogViewer.cpp
    src/features/workspace/PackWorldList.h src/features/workspace/PackWorldList.cpp)
target_sources(conflux_desktop PRIVATE
    src/features/content/ContentDependencyInstaller.h src/features/content/ContentDependencyInstaller.cpp
    src/features/content/OptionalDependencies.h src/features/content/OptionalDependencies.cpp
    src/features/preferences/InstallationPreferences.h src/features/preferences/InstallationPreferences.cpp
    src/platform/OptionalDependencySource.h src/platform/OptionalDependencySource.cpp
    src/platform/ContentDependencySource.h src/platform/ContentDependencySource.cpp
    src/features/content/ContentPorts.h
    src/features/content/ContentVersionSource.h
    src/features/content/InstalledContentVersions.h src/features/content/InstalledContentVersions.cpp
    src/features/content/ContentIndex.h
    src/platform/SqliteContentIndex.h src/platform/SqliteContentIndex.cpp
    src/platform/ContentMetadataSource.h src/platform/ContentMetadataSource.cpp
    src/platform/CurseforgeFingerprint.h src/platform/CurseforgeFingerprint.cpp
    src/features/content/SourceLockIndex.h src/features/content/SourceLockIndex.cpp
    src/features/preferences/PackReleasePolicy.h src/features/preferences/PackReleasePolicy.cpp
    src/features/explore/BrowseInstalls.h src/features/explore/BrowseInstalls.cpp
    src/features/explore/BrowseCatalogue.h src/features/explore/BrowseCatalogue.cpp
    src/features/preferences/BrowsePreferences.h src/features/preferences/BrowsePreferences.cpp
    src/features/preferences/BrowseFilterPreferences.h src/features/preferences/BrowseFilterPreferences.cpp
    src/features/preferences/ContentVisibility.h src/features/preferences/ContentVisibility.cpp
    src/platform/BrowseSettingsStore.h src/platform/BrowseSettingsStore.cpp
    src/platform/AssetInstaller.h src/platform/AssetInstaller.cpp
    src/features/content/ContentActions.h src/features/content/ContentActions.cpp
    src/features/status/OperationHistory.h src/features/status/OperationHistory.cpp
    src/features/status/RunningInstances.h src/features/status/RunningInstances.cpp
    src/platform/InstanceProcessSource.h src/platform/InstanceProcessSource.cpp
    src/features/content/ContentOperationQueue.h src/features/content/ContentOperationQueue.cpp
    src/features/content/ContentOperationExecutor.h src/features/content/ContentOperationExecutor.cpp
    src/features/content/ContentListModel.h src/features/content/ContentListModel.cpp
    src/features/content/ContentViewIndex.h src/features/content/ContentViewIndex.cpp
    src/features/content/DependencyRemovals.h src/features/content/DependencyRemovals.cpp
    src/features/content/ContentOperationReservations.h src/features/content/ContentOperationReservations.cpp
    src/features/content/ContentTargetIndex.h src/features/content/ContentTargetIndex.cpp
    src/features/content/ContentLockIntent.h src/features/content/ContentLockIntent.cpp
    src/features/content/ContentUpdateIntent.h src/features/content/ContentUpdateIntent.cpp
    src/features/content/ContentLockRegistry.h src/features/content/ContentLockRegistry.cpp
    src/features/preferences/ContentPreferences.h src/features/preferences/ContentPreferences.cpp
    src/platform/NativeContentStore.h src/platform/NativeContentStore.cpp
    src/platform/LauncherContentState.h src/platform/LauncherContentState.cpp
    src/platform/LauncherContentLocks.h src/platform/LauncherContentLocks.cpp
    src/platform/CurseforgeContentState.h src/platform/CurseforgeContentState.cpp
    src/platform/ContentInstaller.h src/platform/ContentInstaller.cpp
    src/platform/CurseforgeInstallSource.h src/platform/CurseforgeInstallSource.cpp
    src/services/ContentCatalogue.h src/services/ContentCatalogue.cpp
    src/platform/PackChangeMonitor.h src/platform/PackChangeMonitor.cpp
    src/platform/WindowFrame.h src/platform/WindowFrame.cpp
    src/features/launch/WorldLaunch.h src/features/launch/WorldLaunch.cpp
    src/platform/WorldLaunchAdapters.h src/platform/WorldLaunchAdapters.cpp
    src/features/showcase/DescriptionRenderer.h src/features/showcase/DescriptionRenderer.cpp
    src/features/explore/BulkContentInput.h src/features/explore/BulkContentInput.cpp
    src/features/explore/BulkContentAdd.h src/features/explore/BulkContentAdd.cpp
    src/features/config/ConfigLibrary.h src/features/config/ConfigLibrary.cpp
    src/platform/ConfigOwnershipStore.h src/platform/ConfigOwnershipStore.cpp
    src/features/showcase/ProjectShowcaseSource.h
    src/features/showcase/ProjectShowcase.h src/features/showcase/ProjectShowcase.cpp
    src/features/showcase/VersionChangelogs.h src/features/showcase/VersionChangelogs.cpp
    src/features/showcase/ReleaseFilters.h src/features/showcase/ReleaseFilters.cpp
    src/platform/InstalledModIdentity.h src/platform/InstalledModIdentity.cpp
    src/platform/CatalogueShowcaseSource.h src/platform/CatalogueShowcaseSource.cpp)
target_include_directories(conflux_desktop PUBLIC src)
target_link_libraries(conflux_desktop PUBLIC conflux_content conflux_config pack_foundation Qt6::Concurrent PRIVATE miniz)
if(WIN32)
    target_link_libraries(conflux_desktop PRIVATE dwmapi)
endif()
qt_add_executable(modpack-studio src/main.cpp src/ApplicationController.h src/ApplicationController.cpp src/verification/DesktopSmokeTest.h src/verification/DesktopSmokeTest.cpp)
qt_add_qml_module(modpack-studio URI ModpackStudio VERSION 1.0
    QML_FILES
    qml/components/ReleaseStagePicker.qml
    qml/components/NotificationBanner.qml qml/components/ChangelogView.qml
    qml/components/SearchableSelect.qml qml/components/CreatePackDialog.qml qml/settings/GitSettings.qml
    qml/components/MenuList.qml qml/components/MenuOption.qml
    qml/Main.qml
    qml/pack/ConfigEditorPanel.qml
    qml/pack/ConfigField.qml
    qml/pack/ConfigPanel.qml
    qml/pack/ContentPanel.qml
    qml/pack/DiagnosticsBanner.qml
    qml/pack/FilePanel.qml
    qml/pack/GitPanel.qml
    qml/pack/LogPanel.qml
    qml/pack/WorldPanel.qml
    qml/pack/VersionPicker.qml
    qml/settings/BrowseSettings.qml
    qml/explore/BrowseFilters.qml qml/explore/PaginationBar.qml qml/explore/PageNavigation.qml
    qml/settings/AppearancePreview.qml qml/settings/AppearanceMetric.qml qml/settings/ThemeColorGroup.qml
    qml/settings/AppearanceField.qml qml/settings/ThemeSettings.qml
    qml/settings/AppearanceSettings.qml
    qml/settings/ConnectionSettings.qml
    qml/settings/WarningSettings.qml
    qml/components/ToggleSwitch.qml
    qml/components/Icon.qml
    qml/components/IconPaths.js
    qml/components/BrandBadge.qml qml/components/ProjectCard.qml
    qml/components/WindowControls.qml qml/components/WindowDragArea.qml qml/components/WindowResizeEdges.qml
    qml/components/CompatibilityBadges.qml qml/components/BoundedScrollView.qml
    qml/components/SmoothScroll.qml qml/components/SmoothListView.qml
    qml/components/ContentListView.qml qml/components/ContentGridView.qml qml/components/ContentScrollBar.qml qml/components/ScrollViewportCorners.qml qml/components/ContentStatistic.qml
    qml/showcase/DescriptionView.qml qml/showcase/GalleryView.qml qml/showcase/GalleryViewer.qml
    qml/components/CommandPaletteDialog.qml
    qml/components/FocusPopup.qml qml/components/FocusDialog.qml
    qml/components/OptionalDependenciesDialog.qml qml/components/DependenciesDialog.qml qml/settings/InstallationSettings.qml
    qml/components/OperationHud.qml qml/components/RoundedToolTip.qml qml/components/PopupButton.qml qml/components/ChoiceFilter.qml qml/components/FilterChip.qml
    qml/explore/ActiveBrowseFilters.qml
    qml/components/ContentIcon.qml
    qml/components/SearchField.qml
    qml/components/SelectBox.qml
    qml/components/SectionTabs.qml
    qml/components/NavigationButton.qml
    qml/components/SelectionBox.qml
    qml/pack/ContentSelectionBar.qml qml/pack/ContentActivity.qml qml/components/ContentLink.qml qml/components/AuthorLinks.qml
    qml/components/ContentFilter.qml
    qml/components/ActionButton.qml qml/components/SourceBadge.qml qml/components/EmptyState.qml qml/components/LibraryRow.qml
    qml/pages/LibraryPage.qml qml/pages/DiscoverPage.qml qml/pages/PackPage.qml qml/pages/SettingsPage.qml)
qt_target_qml_sources(modpack-studio QML_FILES qml/pages/ShowcasePage.qml)
target_link_libraries(modpack-studio PRIVATE pack_foundation conflux_desktop conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::Svg)
set_target_properties(modpack-studio PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)
include(CTest)
if(BUILD_TESTING)
    include(cmake/ConfluxTesting.cmake)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME coverage-audit-tool COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p "test_coverage*.py" -v)
    set_tests_properties(coverage-audit-tool PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 30)
    qt_add_executable(config-codec-tests tests/ConfigCodecTests.cpp)
    target_link_libraries(config-codec-tests PRIVATE conflux_config Qt6::Test)
    conflux_add_test(config-codecs config-codec-tests)
    qt_add_executable(config-editor-tests tests/ConfigEditorTests.cpp)
    target_link_libraries(config-editor-tests PRIVATE conflux_config Qt6::Test)
    conflux_add_test(config-editor config-editor-tests)
    qt_add_executable(content-version-tests tests/ContentVersionTests.cpp)
    target_link_libraries(content-version-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-versions content-version-tests)
    qt_add_executable(world-launch-tests tests/WorldLaunchTests.cpp)
    target_link_libraries(world-launch-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(world-launch world-launch-tests)
    qt_add_executable(content-operation-queue-tests tests/ContentOperationQueueTests.cpp)
    target_link_libraries(content-operation-queue-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-operation-queue content-operation-queue-tests)
    qt_add_executable(launcher-content-lock-tests tests/LauncherContentLockTests.cpp)
    target_link_libraries(launcher-content-lock-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(launcher-content-locks launcher-content-lock-tests)
    qt_add_executable(release-policy-tests tests/ReleasePolicyTests.cpp)
    target_link_libraries(release-policy-tests PRIVATE conflux_desktop conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(release-policy-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    conflux_add_test(release-policy release-policy-tests)
    qt_add_executable(content-metadata-tests tests/ContentMetadataTests.cpp)
    target_link_libraries(content-metadata-tests PRIVATE conflux_desktop miniz Qt6::Test)
    conflux_add_test(content-metadata content-metadata-tests)
    qt_add_executable(archive-reader-tests tests/ArchiveReaderTests.cpp)
    target_link_libraries(archive-reader-tests PRIVATE conflux_content miniz Qt6::Test)
    conflux_add_test(archive-reader archive-reader-tests)
    qt_add_executable(content-index-tests tests/ContentIndexTests.cpp)
    target_link_libraries(content-index-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-index content-index-tests)
    qt_add_executable(content-target-index-tests tests/ContentTargetIndexTests.cpp)
    target_link_libraries(content-target-index-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-target-index content-target-index-tests)
    qt_add_executable(content-filtering-tests tests/ContentFilteringTests.cpp)
    target_link_libraries(content-filtering-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-filtering content-filtering-tests)
    qt_add_executable(content-dependency-tests tests/ContentDependencyTests.cpp)
    target_link_libraries(content-dependency-tests PRIVATE conflux_desktop miniz Qt6::Test)
    conflux_add_test(content-dependencies content-dependency-tests)
    qt_add_executable(command-palette-tests tests/CommandPaletteTests.cpp)
    target_link_libraries(command-palette-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(command-palette command-palette-tests)
    qt_add_executable(command-palette-interaction-tests tests/CommandPaletteInteractionTests.cpp)
    target_link_libraries(command-palette-interaction-tests PRIVATE conflux_desktop conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(command-palette-interaction-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml" CONFLUX_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/artifacts")
    conflux_add_test(command-palette-interaction command-palette-interaction-tests)
    qt_add_executable(content-dependency-graph-tests tests/ContentDependencyGraphTests.cpp)
    target_link_libraries(content-dependency-graph-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-dependency-graph content-dependency-graph-tests)
    qt_add_executable(dependency-removal-tests tests/DependencyRemovalTests.cpp)
    target_link_libraries(dependency-removal-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(dependency-removals dependency-removal-tests)
    qt_add_executable(dependency-removal-interaction-tests tests/DependencyRemovalInteractionTests.cpp)
    target_link_libraries(dependency-removal-interaction-tests PRIVATE conflux_desktop conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(dependency-removal-interaction-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml" CONFLUX_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/artifacts")
    conflux_add_test(dependency-removal-interaction dependency-removal-interaction-tests)
    qt_add_executable(optional-dependency-tests tests/OptionalDependencyTests.cpp)
    target_link_libraries(optional-dependency-tests PRIVATE conflux_desktop miniz Qt6::Test)
    conflux_add_test(optional-dependencies optional-dependency-tests)
    qt_add_executable(optional-dependency-interaction-tests tests/OptionalDependencyInteractionTests.cpp)
    target_link_libraries(optional-dependency-interaction-tests PRIVATE conflux_desktop conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(optional-dependency-interaction-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml" CONFLUX_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/artifacts")
    conflux_add_test(optional-dependency-interaction optional-dependency-interaction-tests)
    qt_add_executable(catalogue-dependency-tests tests/CatalogueLoadingTests.cpp)
    target_link_libraries(catalogue-dependency-tests PRIVATE pack_foundation miniz Qt6::Test)
    conflux_add_test(catalogue-dependencies catalogue-dependency-tests)
    qt_add_executable(content-transaction-tests tests/ContentTransactionTests.cpp)
    target_link_libraries(content-transaction-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-transactions content-transaction-tests)
    qt_add_executable(content-pipeline-tests tests/ContentPipelineTests.cpp)
    target_link_libraries(content-pipeline-tests PRIVATE conflux_desktop miniz Qt6::Test)
    conflux_add_test(content-pipeline content-pipeline-tests)
    qt_add_executable(status-tests tests/StatusTests.cpp)
    target_link_libraries(status-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(status status-tests)
    qt_add_executable(window-frame-tests tests/WindowFrameTests.cpp)
    target_link_libraries(window-frame-tests PRIVATE conflux_desktop Qt6::Test Qt6::Quick)
    conflux_add_test(window-frame window-frame-tests)
    qt_add_executable(content-time-tests tests/ContentTimeTests.cpp)
    target_link_libraries(content-time-tests PRIVATE conflux_style conflux_styleplugin Qt6::Test)
    conflux_add_test(content-time content-time-tests)
    qt_add_executable(appearance-tests tests/AppearanceTests.cpp)
    target_link_libraries(appearance-tests PRIVATE conflux_style conflux_styleplugin Qt6::Test)
    conflux_add_test(appearance appearance-tests)
    qt_add_executable(foundation-tests tests/FoundationTests.cpp)
    target_link_libraries(foundation-tests PRIVATE pack_foundation Qt6::Test)
    conflux_add_test(foundation foundation-tests)
    qt_add_executable(installer-tests tests/InstallerTests.cpp)
    target_link_libraries(installer-tests PRIVATE pack_foundation Qt6::Test)
    conflux_add_test(installer installer-tests)
    qt_add_executable(curseforge-install-tests tests/CurseforgeInstallTests.cpp)
    target_link_libraries(curseforge-install-tests PRIVATE conflux_desktop miniz Qt6::Test)
    conflux_add_test(curseforge-install curseforge-install-tests)
    qt_add_executable(library-row-tests tests/LibraryRowTests.cpp)
    target_link_libraries(library-row-tests PRIVATE conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(library-row-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    conflux_add_test(library-row library-row-tests)
    qt_add_executable(workspace-tests tests/WorkspaceTests.cpp src/ApplicationController.h src/ApplicationController.cpp)
    target_link_libraries(workspace-tests PRIVATE pack_foundation conflux_desktop miniz conflux_style conflux_styleplugin Qt6::Quick Qt6::Test)
    target_compile_definitions(workspace-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    conflux_add_test(workspace workspace-tests)
    qt_add_executable(feature-tests tests/FeatureTests.cpp)
    target_link_libraries(feature-tests PRIVATE conflux_desktop Qt6::Quick Qt6::Test)
    conflux_add_test(features feature-tests)
    qt_add_executable(showcase-loading-tests tests/ShowcaseLoadingTests.cpp)
    target_link_libraries(showcase-loading-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(showcase-loading showcase-loading-tests)
    qt_add_executable(showcase-tests tests/ShowcaseTests.cpp)
    target_link_libraries(showcase-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(showcase showcase-tests)
    qt_add_executable(version-changelog-tests tests/VersionChangelogTests.cpp)
    target_link_libraries(version-changelog-tests PRIVATE conflux_desktop conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(version-changelog-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    conflux_add_test(version-changelogs version-changelog-tests)
    qt_add_executable(content-read-budget-tests tests/ContentReadBudgetTests.cpp)
    target_link_libraries(content-read-budget-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-read-budgets content-read-budget-tests)
    qt_add_executable(content-update-intent-tests tests/ContentUpdateIntentTests.cpp)
    target_link_libraries(content-update-intent-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-update-intent content-update-intent-tests)
    qt_add_executable(content-lock-intent-tests tests/ContentLockIntentTests.cpp)
    target_link_libraries(content-lock-intent-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-lock-intent content-lock-intent-tests)
    qt_add_executable(content-preferences-tests tests/ContentPreferenceTests.cpp)
    target_link_libraries(content-preferences-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-preferences content-preferences-tests)
    qt_add_executable(bulk-content-tests tests/BulkContentTests.cpp)
    target_link_libraries(bulk-content-tests PRIVATE conflux_desktop miniz Qt6::Test)
    conflux_add_test(bulk-content bulk-content-tests)
    qt_add_executable(content-actions-tests tests/ContentActionsTests.cpp)
    target_link_libraries(content-actions-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-actions content-actions-tests)
    qt_add_executable(content-update-tests tests/ContentUpdateTests.cpp)
    target_link_libraries(content-update-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-updates content-update-tests)
    qt_add_executable(content-catalogue-update-tests tests/ContentCatalogueUpdateTests.cpp)
    target_link_libraries(content-catalogue-update-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-catalogue-updates content-catalogue-update-tests)
    qt_add_executable(json-response-tests tests/JsonResponseTests.cpp)
    target_link_libraries(json-response-tests PRIVATE pack_foundation Qt6::Test)
    conflux_add_test(json-responses json-response-tests)
    qt_add_executable(content-update-integration-tests tests/ContentUpdateIntegrationTests.cpp)
    target_link_libraries(content-update-integration-tests PRIVATE conflux_desktop miniz Qt6::Test)
    conflux_add_test(content-update-integration content-update-integration-tests)
    qt_add_executable(content-operation-state-tests tests/ContentOperationStateTests.cpp)
    target_link_libraries(content-operation-state-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(content-operation-state content-operation-state-tests)
    qt_add_executable(browse-installs-tests tests/BrowseInstallsTests.cpp)
    target_link_libraries(browse-installs-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(browse-installs browse-installs-tests)
    qt_add_executable(asset-installer-tests tests/AssetInstallerTests.cpp)
    target_link_libraries(asset-installer-tests PRIVATE conflux_desktop miniz Qt6::Test)
    conflux_add_test(asset-installer asset-installer-tests)
    qt_add_executable(browse-catalogue-tests tests/BrowseCatalogueTests.cpp)
    target_link_libraries(browse-catalogue-tests PRIVATE conflux_desktop Qt6::Test)
    conflux_add_test(browse-catalogue browse-catalogue-tests)
    qt_add_executable(catalogue-interaction-tests tests/CatalogueInteractionTests.cpp)
    target_link_libraries(catalogue-interaction-tests PRIVATE conflux_desktop conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(catalogue-interaction-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml" CONFLUX_TEST_ARTIFACT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/artifacts")
    conflux_add_test(catalogue-interaction catalogue-interaction-tests)
    qt_add_executable(content-interaction-tests tests/ContentInteractionTests.cpp)
    target_link_libraries(content-interaction-tests PRIVATE conflux_desktop conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(content-interaction-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    conflux_add_test(content-interaction content-interaction-tests)
endif()
install(TARGETS modpack-studio BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(CONFLUX_ENABLE_COVERAGE)
    get_property(coverage_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS coverage_targets)
        get_target_property(target_type ${target} TYPE)
        if(NOT target_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$" OR target STREQUAL "miniz")
            continue()
        endif()
        target_compile_options(${target} PRIVATE -O0 -g --coverage -fprofile-abs-path -fprofile-update=atomic)
    endforeach()
endif()
