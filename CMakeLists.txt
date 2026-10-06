# Desktop build (SDL2 + OpenGL ES 2) of Stremio TV, for development. The PS4
# package is built with Makefile.ps4 instead (see BUILDING.md).
#
#   cmake -S . -B build && cmake --build build -j && build/stremio_tv
#
# -DSTREMIO_DEV=ON adds developer tools: demo session (STREMIO_TV_DEMO=1),
# extra add-ons (STREMIO_TV_DEMO_ADDONS), scripted runs with screenshots
# (STREMIO_TV_SCRIPT) and an fps overlay.
cmake_minimum_required(VERSION 3.17)
file(STRINGS VERSION APP_VERSION LIMIT_COUNT 1)
project(stremio_tv VERSION ${APP_VERSION} LANGUAGES CXX)
option(STREMIO_DEV "Developer tools (demo session, scripted runs)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET sdl2 glesv2 freetype2 libcurl libavformat libavcodec libswscale libswresample libavutil)

add_executable(stremio_tv
    src/main.cpp
    src/platform/platform_desktop.cpp
    src/render/renderer.cpp
    src/render/font.cpp
    src/core/json.cpp
    src/core/models.cpp
    src/core/stremio.cpp
    src/core/subtitles.cpp
    src/net/http.cpp
    src/net/http_curl.cpp
    src/net/images.cpp
    src/net/range_reader.cpp
    src/ui/home_screen.cpp
    src/ui/card.cpp
    src/ui/tab_bar.cpp
    src/ui/search_screen.cpp
    src/ui/library_screen.cpp
    src/ui/settings_screen.cpp
    src/ui/login_screen.cpp
    src/ui/details_screen.cpp
    src/ui/player_screen.cpp
    src/video/video_ffmpeg.cpp
    src/video/video_player.cpp
)
target_include_directories(stremio_tv PRIVATE third_party/stb)
target_link_libraries(stremio_tv PRIVATE PkgConfig::DEPS)
target_compile_options(stremio_tv PRIVATE -Wall -Wno-unused-function)
target_compile_definitions(stremio_tv PRIVATE APP_VERSION="${APP_VERSION}")
if(STREMIO_DEV)
    target_compile_definitions(stremio_tv PRIVATE STREMIO_DEV)
endif()

# Assets next to the executable (platform::asset_path uses the exe folder).
add_custom_command(TARGET stremio_tv POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:stremio_tv>/assets)
