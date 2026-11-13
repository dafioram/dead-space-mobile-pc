find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
find_package(ZLIB REQUIRED)

add_executable(core_tests
    core_tests.cpp
    ../src/asset_file.cpp
    ../src/settings.cpp
)
target_include_directories(core_tests PRIVATE ../src)

add_executable(apk_import_probe
    apk_import_probe.cpp
    ../src/apk.cpp
    ../src/log.cpp
)
target_include_directories(apk_import_probe PRIVATE ../src)
target_link_libraries(apk_import_probe PRIVATE ZLIB::ZLIB)

add_test(NAME core COMMAND core_tests)
add_test(NAME apk_import COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_apk_import.py" --importer "$<TARGET_FILE:apk_import_probe>")
add_test(NAME asset_policy COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_asset_policy.py -v)
add_test(NAME starttest_exit_status COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_starttest.py -v)
set_tests_properties(core apk_import asset_policy starttest_exit_status PROPERTIES TIMEOUT 60)

add_test(NAME build_staging COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_staging.py -v)
set_tests_properties(build_staging PROPERTIES TIMEOUT 60)
