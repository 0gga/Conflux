# Every Qt test writes an inspectable report, including GUI-subsystem executables
# whose stdout is not captured on Windows. Environment policy stays in one place.
function(conflux_add_test name executable)
    add_test(NAME ${name} COMMAND ${executable} -o "${CMAKE_CURRENT_BINARY_DIR}/${name}-results.txt,txt")
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    if(CONFLUX_ENABLE_COVERAGE)
        target_compile_definitions(${executable} PRIVATE CONFLUX_COVERAGE_BUILD=1)
    endif()
endfunction()
