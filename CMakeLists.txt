cmake_minimum_required(VERSION 3.24)
project(ModpackStudio VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Concurrent Sql Test Svg)
qt_standard_project_setup(REQUIRES 6.8)
qt_add_qml_module(conflux_style STATIC URI Conflux.Style VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Conflux/Style
    SOURCES src/theme/Theme.h src/theme/Theme.cpp
    RESOURCES themes/dark-grey.json)
target_link_libraries(conflux_style PRIVATE Qt6::Gui)
target_include_directories(conflux_style PUBLIC src/theme)
add_library(miniz STATIC third_party/miniz.c third_party/miniz_tdef.c third_party/miniz_tinfl.c third_party/miniz_zip.c)
target_include_directories(miniz PUBLIC third_party)

qt_add_library(pack_foundation STATIC
    src/domain/Catalogue.h src/domain/Catalogue.cpp
    src/services/CatalogueClient.h src/services/CatalogueClient.cpp
    src/services/InstallationDiscovery.h src/services/InstallationDiscovery.cpp
    src/services/ModInstaller.h src/services/ModInstaller.cpp
    src/services/PackRepository.h src/services/PackRepository.cpp)
target_include_directories(pack_foundation PUBLIC src)
target_link_libraries(pack_foundation PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql Qt6::Concurrent)
qt_add_library(pack_storage STATIC src/platform/PackFileStore.h src/platform/PackFileStore.cpp)
target_include_directories(pack_storage PUBLIC src)
target_link_libraries(pack_storage PUBLIC Qt6::Core)
qt_add_library(conflux_content STATIC
    src/services/PackContent.h src/services/PackContent.cpp
    src/features/content/ArchiveReader.h src/features/content/ArchiveReader.cpp
    src/features/content/ModMetadataReader.h src/features/content/ModMetadataReader.cpp
    src/features/content/ContentInventory.h src/features/content/ContentInventory.cpp
    src/features/content/ConfigAssociation.h src/features/content/ConfigAssociation.cpp
    src/features/diagnostics/DiagnosticRule.h src/features/diagnostics/DiagnosticRules.cpp)
target_include_directories(conflux_content PUBLIC src)
target_link_libraries(conflux_content PUBLIC pack_storage Qt6::Gui PRIVATE miniz)
qt_add_library(conflux_config STATIC
    src/features/config/ConfigCodec.h src/features/config/JsonConfigCodec.cpp
    src/features/config/TomlConfigCodec.cpp src/features/config/PropertiesConfigCodec.cpp
    src/features/config/ConfigRepository.h src/features/config/ConfigRepository.cpp)
target_include_directories(conflux_config PUBLIC src PRIVATE third_party)
target_link_libraries(conflux_config PUBLIC pack_storage)
qt_add_library(conflux_desktop STATIC
    src/DesktopSession.h src/DesktopSession.cpp
    src/features/navigation/NavigationHistory.h src/features/navigation/NavigationHistory.cpp
    src/features/navigation/NavigationInput.h src/features/navigation/NavigationInput.cpp
    src/features/preferences/WarningPreferences.h src/features/preferences/WarningPreferences.cpp
    src/features/workspace/PackWorkspace.h src/features/workspace/PackWorkspace.cpp
    src/features/workspace/PackFileBrowser.h src/features/workspace/PackFileBrowser.cpp
    src/features/workspace/PackLogViewer.h src/features/workspace/PackLogViewer.cpp
    src/features/workspace/PackWorldList.h src/features/workspace/PackWorldList.cpp
    src/features/config/ConfigEditor.h src/features/config/ConfigEditor.cpp)
target_include_directories(conflux_desktop PUBLIC src)
target_link_libraries(conflux_desktop PUBLIC conflux_content conflux_config Qt6::Concurrent)
qt_add_executable(modpack-studio src/main.cpp src/ApplicationController.h src/ApplicationController.cpp src/verification/DesktopSmokeTest.h src/verification/DesktopSmokeTest.cpp)
qt_add_qml_module(modpack-studio URI ModpackStudio VERSION 1.0
    QML_FILES
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
    qml/settings/AppearanceSettings.qml
    qml/settings/ConnectionSettings.qml
    qml/settings/WarningSettings.qml
    qml/components/ToggleSwitch.qml
    qml/components/Icon.qml
    qml/components/IconPaths.js
    qml/components/ContentIcon.qml
    qml/components/SearchField.qml
    qml/components/SelectBox.qml
    qml/components/SectionTabs.qml
    qml/components/ActionButton.qml qml/components/SourceBadge.qml qml/components/EmptyState.qml qml/components/LibraryRow.qml
    qml/pages/LibraryPage.qml qml/pages/DiscoverPage.qml qml/pages/PackPage.qml qml/pages/SettingsPage.qml)
target_link_libraries(modpack-studio PRIVATE pack_foundation conflux_desktop conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::Svg)
set_target_properties(modpack-studio PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)
include(CTest)
if(BUILD_TESTING)
    qt_add_executable(foundation-tests tests/FoundationTests.cpp)
    target_link_libraries(foundation-tests PRIVATE pack_foundation Qt6::Test)
    add_test(NAME foundation COMMAND foundation-tests)
    qt_add_executable(installer-tests tests/InstallerTests.cpp)
    target_link_libraries(installer-tests PRIVATE pack_foundation Qt6::Test)
    add_test(NAME installer COMMAND installer-tests)
    qt_add_executable(library-row-tests tests/LibraryRowTests.cpp)
    target_link_libraries(library-row-tests PRIVATE conflux_styleplugin Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(library-row-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    add_test(NAME library-row COMMAND library-row-tests)
    qt_add_executable(workspace-tests tests/WorkspaceTests.cpp src/ApplicationController.h src/ApplicationController.cpp)
    target_link_libraries(workspace-tests PRIVATE pack_foundation conflux_desktop miniz conflux_style conflux_styleplugin Qt6::Quick Qt6::Test)
    target_compile_definitions(workspace-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    add_test(NAME workspace COMMAND workspace-tests)
    qt_add_executable(feature-tests tests/FeatureTests.cpp)
    target_link_libraries(feature-tests PRIVATE conflux_desktop Qt6::Quick Qt6::Test)
    add_test(NAME features COMMAND feature-tests)
    set_tests_properties(features PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    set_tests_properties(workspace PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    set_tests_properties(library-row PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    set_tests_properties(foundation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
install(TARGETS modpack-studio BUNDLE DESTINATION . RUNTIME DESTINATION bin)
