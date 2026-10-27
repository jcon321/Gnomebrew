cmake_minimum_required(VERSION 3.16)
project(my-client CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)
    set(MYCLIENT_WARNINGS /W4 /permissive- /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    set(MYCLIENT_WARNINGS -Wall -Wextra)
endif()
if(WIN32)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Vendored rdplib: the recovered EQ RDP transport (MIT), copied from EQMacEmu's server/submodules/rdplib.
set(RDPLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/rdplib)

# core: networking, crypto and asset formats. Portable (Linux and Windows).
add_library(eqcore STATIC
    core/rdp_session.cpp
    core/login_client.cpp
    core/world_client.cpp
    core/zone_names.cpp
    core/zone_session.cpp
    core/packet_log.cpp
    core/ini_file.cpp
    core/des.cpp
    core/inflate.cpp
    core/pfs.cpp
    core/bitmap.cpp
    core/eqtext.cpp
    core/eqstr.cpp
    core/tga.cpp
    core/xml.cpp
    core/dds.cpp
    core/wld.cpp
    core/creation_rules.cpp
    core/inventory.cpp
    core/spell_file.cpp
)
target_include_directories(eqcore PUBLIC core)
target_link_libraries(eqcore PUBLIC rdplib)
if(WIN32)
    # rdplib only adds these for MSVC; MinGW (the Linux cross-check build) needs them too.
    target_link_libraries(eqcore PUBLIC ws2_32 winmm)
endif()
target_compile_options(eqcore PRIVATE ${MYCLIENT_WARNINGS})

find_package(Threads REQUIRED)

# audio: the sound and music (the mix, WAV, MP3, XMI, the client's sound manager). Portable.
add_library(eqaudio STATIC
    audio/wav.cpp
    audio/xmi.cpp
    audio/synth.cpp
    audio/mixer.cpp
    audio/eq_sound.cpp
    audio/actor_sound_table.cpp
)
target_include_directories(eqaudio PUBLIC audio)
target_link_libraries(eqaudio PUBLIC eqcore Threads::Threads)
target_compile_options(eqaudio PRIVATE ${MYCLIENT_WARNINGS})

# eqclient: console client, used for testing against a server.
add_executable(eqclient console/main.cpp)
target_link_libraries(eqclient PRIVATE eqcore)
target_compile_options(eqclient PRIVATE ${MYCLIENT_WARNINGS})

# render: the 3D view (zone and model loading, software rasterizer). Portable.
add_library(eqrender STATIC
    render/renderer.cpp
    render/rasterizer.cpp
    render/textures.cpp
    render/animations.cpp
    render/point_lights.cpp
    render/skeleton.cpp
    render/zone_scene.cpp
    render/zone_regions.cpp
    render/occlusion.cpp
    render/character_model.cpp
    render/actor_tags.cpp
    render/sky.cpp
    render/particles.cpp
    render/spell_particles.cpp
)
target_include_directories(eqrender PUBLIC render)
target_link_libraries(eqrender PUBLIC eqcore Threads::Threads)
target_compile_options(eqrender PRIVATE ${MYCLIENT_WARNINGS})

# frontend: the login/server-select screens. The logic and drawing are portable;
# only the window, GDI text and input glue (win32_main.cpp) are Windows-specific.
add_library(eqfrontend STATIC
    frontend/canvas.cpp
    frontend/art.cpp
    frontend/widgets.cpp
    frontend/frontend.cpp
    frontend/sidl.cpp
    frontend/charselect_view.cpp
    frontend/charcreate.cpp
    frontend/zone_view.cpp
    frontend/player_movement.cpp
    frontend/spawn_actor.cpp
    frontend/zone_doors.cpp
    frontend/actor_sound.cpp
    frontend/weather.cpp
    frontend/debug_menu.cpp
    frontend/window_manager.cpp
    frontend/chat_window.cpp
    frontend/item_tags.cpp
    frontend/chat_commands.cpp
    frontend/chat_text.cpp
    frontend/game_values.cpp
    frontend/game_windows.cpp
    frontend/spell_book.cpp
    frontend/spell_casting.cpp
    frontend/spell_effects.cpp
    frontend/item_display.cpp
    frontend/name_sprites.cpp
    frontend/settings_file.cpp
)
target_include_directories(eqfrontend PUBLIC frontend)
target_link_libraries(eqfrontend PUBLIC eqcore eqrender eqaudio)
target_compile_options(eqfrontend PRIVATE ${MYCLIENT_WARNINGS})

if(WIN32)
    # Direct3D 11 draws the 3D views and presents; d3dcompiler_47.dll is loaded at runtime.
    add_executable(eqclient_gui WIN32 frontend/win32_main.cpp frontend/win32_text.cpp frontend/win32_d3d11.cpp
        frontend/win32_audio.cpp)
    target_link_libraries(eqclient_gui PRIVATE eqfrontend gdi32 user32 shell32 winmm d3d11 dxgi)
    target_compile_options(eqclient_gui PRIVATE ${MYCLIENT_WARNINGS})
    if(MINGW)
        # Self-contained exe: no libstdc++/libgcc/libwinpthread DLLs to ship.
        target_link_options(eqclient_gui PRIVATE -static -municode) # -municode: wWinMain entry point
        target_link_options(eqclient PRIVATE -static)
    endif()
else()
    # Linux-only developer tool: drives the front end headlessly and writes PNG snapshots.
    add_executable(frontend_preview tools/frontend_preview.cpp)
    target_link_libraries(frontend_preview PRIVATE eqfrontend)
    target_compile_options(frontend_preview PRIVATE ${MYCLIENT_WARNINGS})

    # Linux-only developer tool: plays the client's sound headlessly and writes a WAV.
    add_executable(sound_preview tools/sound_preview.cpp)
    target_link_libraries(sound_preview PRIVATE eqfrontend) # eqaudio, and the weather
    target_compile_options(sound_preview PRIVATE ${MYCLIENT_WARNINGS})

    # Linux-only developer tool: renders the 3D view to PNG.
    add_executable(scene_preview tools/scene_preview.cpp)
    target_link_libraries(scene_preview PRIVATE eqfrontend)
    target_compile_options(scene_preview PRIVATE ${MYCLIENT_WARNINGS})
endif()
