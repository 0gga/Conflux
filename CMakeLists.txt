cmake_minimum_required(VERSION 3.24)
project(ModpackStudio VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Concurrent Sql Test)
qt_standard_project_setup(REQUIRES 6.8)

qt_add_library(pack_foundation STATIC
    src/domain/Catalogue.h src/domain/Catalogue.cpp
    src/services/CatalogueClient.h src/services/CatalogueClient.cpp
    src/services/InstallationDiscovery.h src/services/InstallationDiscovery.cpp
    src/services/ModInstaller.h src/services/ModInstaller.cpp
    src/services/PackRepository.h src/services/PackRepository.cpp)
target_include_directories(pack_foundation PUBLIC src)
target_link_libraries(pack_foundation PUBLIC Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql Qt6::Concurrent)
qt_add_executable(modpack-studio src/main.cpp src/ApplicationController.h src/ApplicationController.cpp)
qt_add_qml_module(modpack-studio URI ModpackStudio VERSION 1.0
    QML_FILES
    qml/Main.qml
    qml/components/ActionButton.qml qml/components/SourceBadge.qml qml/components/EmptyState.qml qml/components/LibraryRow.qml
    qml/pages/LibraryPage.qml qml/pages/DiscoverPage.qml qml/pages/PackPage.qml qml/pages/SettingsPage.qml)
target_link_libraries(modpack-studio PRIVATE pack_foundation Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent)
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
    target_link_libraries(library-row-tests PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    target_compile_definitions(library-row-tests PRIVATE CONFLUX_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
    add_test(NAME library-row COMMAND library-row-tests)
    set_tests_properties(library-row PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    set_tests_properties(foundation PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
install(TARGETS modpack-studio BUNDLE DESTINATION . RUNTIME DESTINATION bin)
