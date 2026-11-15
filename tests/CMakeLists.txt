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
add_executable(console_debug_tests console_debug_tests.cpp)
target_include_directories(console_debug_tests PRIVATE ../src)
add_test(NAME console_debug COMMAND console_debug_tests)
set_tests_properties(console_debug PROPERTIES TIMEOUT 60)
add_executable(scene_cache_tests scene_cache_tests.cpp)
target_include_directories(scene_cache_tests PRIVATE stubs/scene_cache ../src)
find_package(Threads REQUIRED)
target_link_libraries(scene_cache_tests PRIVATE Threads::Threads)
add_test(NAME scene_cache COMMAND scene_cache_tests)
set_tests_properties(scene_cache PROPERTIES TIMEOUT 60)
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

add_test(NAME release_packaging COMMAND "${Python3_EXECUTABLE}" -m unittest discover
    -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_release_package.py -v)
set_tests_properties(release_packaging PROPERTIES TIMEOUT 60)

add_executable(profile_tests profile_tests.cpp ../src/temporary_profile.cpp ../src/settings.cpp)
target_include_directories(profile_tests PRIVATE ../src)
add_test(NAME temporary_profile COMMAND profile_tests)
set_tests_properties(temporary_profile PROPERTIES TIMEOUT 60)

add_executable(rig_credits_tests rig_credits_tests.cpp ../src/rig_credits_layout.cpp ../src/sbin.cpp)
target_include_directories(rig_credits_tests PRIVATE ../src)
add_test(NAME rig_credits COMMAND rig_credits_tests)
set_tests_properties(rig_credits PROPERTIES TIMEOUT 60)

if (DEADSPACE_BUILD_APP)
    add_test(NAME cli_help COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_cli_help.py"
        --executable "$<TARGET_FILE:deadspace>" -v)
    set_tests_properties(cli_help PROPERTIES TIMEOUT 60)
endif()
