cmake_minimum_required(VERSION 3.16)
project(deadspace_port CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(DEADSPACE_BUILD_APP "Build the game port (requires SDL2, OpenGL and Dynarmic)" ON)
include(CTest)

add_library(profile_io STATIC src/save_io.cpp src/save_validation.cpp)
target_include_directories(profile_io PUBLIC src)
add_library(geometry_diagnostics STATIC src/geometry_diagnostics.cpp src/geometry_capture.cpp)
target_include_directories(geometry_diagnostics PUBLIC src)

if (BUILD_TESTING)
    add_subdirectory(tests)
endif()

if (NOT DEADSPACE_BUILD_APP)
    return()
endif()

if (WIN32)
    enable_language(RC)
endif()

# dynarmic: 32-bit ARM JIT (only the A32 frontend is needed)
set(DYNARMIC_FRONTENDS "A32" CACHE STRING "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/dynarmic EXCLUDE_FROM_ALL)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

add_executable(deadspace
    src/main.cpp
    src/log.cpp
    src/apk.cpp
    src/apk_cli.cpp
    src/sha256.cpp
    src/asset_file.cpp
    src/settings.cpp
    src/temporary_profile.cpp
    src/save_lifecycle.cpp
    src/crash.cpp
    src/sbin.cpp
    src/game_patches.cpp
    src/rig_credits.cpp
    src/rig_credits_layout.cpp
    src/input.cpp
    src/devmode.cpp
    src/objects.cpp
    src/interact.cpp
    src/lights.cpp
    src/texpack.cpp
    src/debugview.cpp
    src/console.cpp
    src/console_debug.cpp
    src/menu_nav.cpp
    src/render/native.cpp
    src/guest_util.cpp
    src/memory.cpp
    src/cpu.cpp
    src/elf_loader.cpp
    src/hle/hle.cpp
    src/hle/timing.cpp
    src/hle/libc.cpp
    src/hle/files.cpp
    src/hle/errno.cpp
    src/hle/stdio.cpp
    src/hle/printf.cpp
    src/hle/pthread.cpp
    src/hle/android.cpp
    src/hle/gles.cpp
    src/jni/jni.cpp
    src/jni/java_classes.cpp
)
target_include_directories(deadspace PRIVATE src)
target_link_libraries(deadspace PRIVATE dynarmic OpenGL::GL profile_io geometry_diagnostics)
execute_process(COMMAND git rev-parse --short=12 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE DEADSPACE_BUILD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT DEADSPACE_BUILD_REVISION)
    set(DEADSPACE_BUILD_REVISION "unknown-source")
endif()
target_compile_definitions(deadspace PRIVATE DEADSPACE_BUILD_REVISION="${DEADSPACE_BUILD_REVISION}")
# Track both checkout switches and commits on the current branch, including worktrees.
execute_process(COMMAND git symbolic-ref -q HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE DEADSPACE_GIT_REF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
foreach (ref HEAD "${DEADSPACE_GIT_REF}")
    if (ref)
        execute_process(COMMAND git rev-parse --git-path "${ref}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE git_path OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if (git_path)
            get_filename_component(git_path "${git_path}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${git_path}")
        endif()
    endif()
endforeach()
if (WIN32)
    # fully static: the exe runs without any MSYS2 DLLs next to it
    target_link_libraries(deadspace PRIVATE SDL2::SDL2-static z ws2_32 winmm dbghelp)
    target_link_options(deadspace PRIVATE -static)
    target_sources(deadspace PRIVATE src/deadspace.rc)
else()
    target_link_libraries(deadspace PRIVATE SDL2::SDL2 ZLIB::ZLIB pthread dl)
endif()
