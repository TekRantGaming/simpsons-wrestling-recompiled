# Android build of The Simpsons Wrestling Recompiled.
#
# Gradle (android/app/build.gradle) builds this with the NDK. It builds the
# same game as the PC version -- the repository's CMakeLists.txt, psxrecomp's
# runtime and the generated game C -- with four Android differences:
#
#  * The game is a shared library, libmain.so, not a program: Android starts
#    an app as Java, and SDL's SDLActivity loads libmain.so and calls its
#    main() (SDL_main.h renames it SDL_main).
#  * android_fiber.c replaces psxrecomp's ucontext fibers (Android's C library
#    has no getcontext/makecontext/swapcontext).
#  * android_host.c moves the working directory to the app's own storage
#    before main() runs, so settings, saves and the bundled data are found
#    "next to the game" just as on PC.
#  * The files the build stages next to the game (game.toml, mods/bundled,
#    the bundled BIOS) are collected into an assets folder that goes into the
#    APK; the app unpacks them on first start.
cmake_minimum_required(VERSION 3.20)
project(SimpsonsWrestlingAndroid LANGUAGES C CXX)

get_filename_component(SWR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SWR_ASSETS_OUT "" CACHE PATH "Where to collect the runtime's data files for the APK")
# Every ABI stages the same data; one of them collects it (no parallel writes).
set(SWR_ASSETS_ABI "" CACHE STRING "The Android ABI whose build collects the data files")

# psxrecomp creates the game with add_executable(psx-runtime ...). Build that
# one target as a shared library instead; any other executable is unchanged.
function(add_executable name)
    if(name STREQUAL "psx-runtime")
        set(_args ${ARGN})
        list(REMOVE_ITEM _args WIN32 MACOSX_BUNDLE EXCLUDE_FROM_ALL)
        add_library(${name} SHARED ${_args})
    else()
        _add_executable(${name} ${ARGN})
    endif()
endfunction()

# Low-end TVs: no debug server / heartbeat threads, no Vulkan (the game draws
# with the software renderer and SDL's GLES presenter), no LTO/PGO.
set(PSX_DEBUG_TOOLS OFF CACHE BOOL "" FORCE)
set(PSX_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
set(PSX_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
set(PSX_SDL3_FETCH ON CACHE BOOL "" FORCE)

add_subdirectory("${SWR_ROOT}" game)

target_sources(psx-runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/android_host.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/android_fiber.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/android_gl_compat.c")
target_include_directories(psx-runtime PRIVATE "${PSXRECOMP_ROOT}/runtime/include")
# psx_fiber.c's POSIX backend is ucontext, which Android lacks: android_fiber.c
# implements the same psx_fiber.h API instead.
set_source_files_properties("${PSXRECOMP_ROOT}/runtime/src/psx_fiber.c"
    DIRECTORY "${SWR_ROOT}" PROPERTIES HEADER_FILE_ONLY ON)
set_target_properties(psx-runtime PROPERTIES OUTPUT_NAME main)
target_link_libraries(psx-runtime PRIVATE android log)

# Collect the staged runtime data for the APK (everything next to libmain.so
# that is not build output). game.toml switches to the software renderer.
# Gradle builds this target for every ABI; only one of them does the copying.
if(SWR_ASSETS_OUT AND (NOT SWR_ASSETS_ABI OR ANDROID_ABI STREQUAL SWR_ASSETS_ABI))
    add_custom_target(swr-runtime-assets ALL
        COMMAND ${CMAKE_COMMAND}
            -DSRC=$<TARGET_FILE_DIR:psx-runtime>
            -DDST=${SWR_ASSETS_OUT}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/collect_assets.cmake"
        COMMENT "Collecting runtime data for the APK"
        VERBATIM)
else()
    add_custom_target(swr-runtime-assets ALL)
endif()
add_dependencies(swr-runtime-assets psx-runtime)
