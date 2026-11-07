# cmake -DSRC=<dir of libmain.so> -DDST=<assets dir> -P collect_assets.cmake
#
# Copies the runtime's data files (what psxrecomp stages next to the game:
# game.toml, game_options.toml, mods/bundled, the bundled BIOS and its notice)
# into DST/runtime for the APK, and writes DST/runtime/files.txt listing them
# so the app can unpack them. Build output (.so, .a, object dirs, CMake files)
# is left out. game.toml gets renderer = "software": desktop OpenGL 3.3 does
# not exist on Android, and the software renderer is the light one.
if(NOT SRC OR NOT DST)
    message(FATAL_ERROR "collect_assets.cmake: SRC and DST are required")
endif()
set(_out "${DST}/runtime")
file(REMOVE_RECURSE "${_out}")
file(MAKE_DIRECTORY "${_out}")
file(GLOB_RECURSE _files RELATIVE "${SRC}" "${SRC}/*")
set(_manifest "")
foreach(_f IN LISTS _files)
    if(_f MATCHES "(^|/)CMakeFiles/|(^|/)_deps/|(^|/)game/|\\.(so|a|o|obj|ninja|cmake|log|d)$|^CMakeCache\\.txt$|^\\.ninja|^compile_commands|^build\\.ninja$|(^|/)\\.cmake/")
        continue()
    endif()
    get_filename_component(_dir "${_out}/${_f}" DIRECTORY)
    file(MAKE_DIRECTORY "${_dir}")
    if(_f STREQUAL "game.toml")
        file(READ "${SRC}/${_f}" _toml)
        string(REGEX REPLACE "renderer = \"[a-z]+\"" "renderer = \"software\"" _toml "${_toml}")
        file(WRITE "${_out}/${_f}" "${_toml}")
    else()
        file(COPY_FILE "${SRC}/${_f}" "${_out}/${_f}")
    endif()
    string(APPEND _manifest "${_f}\n")
endforeach()
file(WRITE "${_out}/files.txt" "${_manifest}")
