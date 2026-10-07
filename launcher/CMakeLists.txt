# The Simpsons Wrestling launcher: TRG Launcher in its own SDL2 + OpenGL 3
# window. A separate build from the game, because the runtime uses SDL3 and
# the launcher's standalone window uses SDL2; the launcher starts the game.
cmake_minimum_required(VERSION 3.20)
project(SimpsonsWrestlingLauncher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(TRG_LAUNCHER_STANDALONE ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/trg-launcher trg-launcher)

set(_sw_sources main.cpp)
if(WIN32)
  list(APPEND _sw_sources launcher.rc)
endif()
add_executable(SimpsonsWrestling ${_sw_sources})
target_link_libraries(SimpsonsWrestling PRIVATE trg::launcher_standalone)
if(MSVC)
  target_compile_options(SimpsonsWrestling PRIVATE /utf-8)
endif()
if(WIN32)
  # A windowed app: no console. main() is still the entry point.
  set_target_properties(SimpsonsWrestling PROPERTIES WIN32_EXECUTABLE ON)
  if(MSVC)
    target_link_options(SimpsonsWrestling PRIVATE /ENTRY:mainCRTStartup)
  endif()
endif()
