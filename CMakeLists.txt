cmake_minimum_required(VERSION 3.16)
project(OpenAssault C CXX)
# MSVC: link the C/C++ runtime statically, so the exe runs without the Visual C++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL2: use the system package if present, otherwise download and build it.
find_package(SDL2 CONFIG QUIET)
if(NOT SDL2_FOUND)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_CCACHE OFF CACHE BOOL "" FORCE)   # SDL picks up any ccache on PATH, which can vanish between builds
  FetchContent_Declare(SDL2 URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
  FetchContent_MakeAvailable(SDL2)
endif()

# Pure data/simulation code (no SDL/OpenGL): used by the tools too.
add_library(xacore STATIC
  src/core/fs.cpp
  src/core/lang.cpp
  src/assets/visn.cpp
  src/assets/textures.cpp
  src/assets/hdtex.cpp
  src/assets/model.cpp
  src/assets/world.cpp
  src/assets/typ.cpp
  src/assets/def.cpp
  src/assets/sprites.cpp
  src/game/pose.cpp
  src/game/collision.cpp
  src/game/heli.cpp
  src/game/mission.cpp
  src/assets/path.cpp
  src/assets/font.cpp)
target_include_directories(xacore PUBLIC src)

if(TARGET SDL2::SDL2-static)
  set(XA_SDL SDL2::SDL2-static)
else()
  set(XA_SDL SDL2::SDL2)
endif()

add_executable(openassault
  src/main.cpp
  src/render/glapi.cpp
  src/render/renderer.cpp
  src/audio/audio.cpp
  src/audio/stb_vorbis_impl.c
  src/game/combat.cpp
  src/game/enemies.cpp
  src/ui/text.cpp
  src/ui/hud.cpp
  src/game/briefing.cpp
  src/game/profile.cpp
  src/game/pad.cpp)
target_link_libraries(openassault PRIVATE xacore)
if(TARGET SDL2::SDL2main)
  target_link_libraries(openassault PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(openassault PRIVATE ${XA_SDL})
if(MSVC)
  target_compile_options(xacore PRIVATE /W4)
  target_compile_definitions(xacore PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(xacore PRIVATE -Wall -Wextra)
endif()
if(MINGW)
  target_link_options(openassault PRIVATE -static -static-libgcc -static-libstdc++)
endif()
# Windows launcher: play, language, HD textures, translation template and texture export with buttons
if(WIN32)
  if(MSVC)
    add_executable(OpenAssaultLauncher WIN32 tools/launcher/launcher.cpp tools/launcher/launcher.manifest)
  else()
    add_executable(OpenAssaultLauncher WIN32 tools/launcher/launcher.cpp tools/launcher/launcher_mingw.rc)
    target_link_options(OpenAssaultLauncher PRIVATE -municode -static -static-libgcc -static-libstdc++)
  endif()
  target_compile_definitions(OpenAssaultLauncher PRIVATE UNICODE _UNICODE)
  target_link_libraries(OpenAssaultLauncher PRIVATE comctl32 shell32)
endif()
add_executable(xa_check tools/xa_check.cpp)
target_link_libraries(xa_check PRIVATE xacore)
