# Desktop build (SDL2 + OpenGL ES 2) of Stremio PS4, for development, and the
# unit tests. The PS4 package is built with Makefile.ps4 instead (see
# BUILDING.md).
#
#   cmake -S . -B build && cmake --build build -j && build/stremio_ps4
#   ctest --test-dir build --output-on-failure
#
# -DSTREMIO_DEV=ON adds developer tools: demo session (STREMIO_PS4_DEMO=1),
# extra add-ons (STREMIO_PS4_DEMO_ADDONS), scripted runs with screenshots
# (STREMIO_PS4_SCRIPT) and an fps overlay.
cmake_minimum_required(VERSION 3.17)
file(STRINGS VERSION APP_VERSION LIMIT_COUNT 1)
project(stremio_ps4 VERSION ${APP_VERSION} LANGUAGES C CXX)
option(STREMIO_DEV "Developer tools (demo session, scripted runs)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET sdl2 glesv2 freetype2 harfbuzz libcurl libavformat libavcodec libswscale libswresample libavutil)

# The torrent engine (src/torrent), shared by the app and tools/torrent_fetch.
set(TORRENT_SOURCES
    src/torrent/bencode.cpp
    src/torrent/dht.cpp
    src/torrent/magnet.cpp
    src/torrent/net.cpp
    src/torrent/sha1.cpp
    src/torrent/torrent_stream.cpp
    src/torrent/trackers.cpp
)

add_executable(stremio_ps4
    src/main.cpp
    src/platform/platform_desktop.cpp
    src/render/renderer.cpp
    src/render/font.cpp
    src/render/bidi.cpp
    src/core/charset.cpp
    src/core/json.cpp
    src/core/addon_config.cpp
    third_party/qrcodegen/qrcodegen.c
    src/core/models.cpp
    src/core/stremio.cpp
    src/core/subtitles.cpp
    src/net/http.cpp
    src/net/http_curl.cpp
    src/net/images.cpp
    src/net/range_reader.cpp
    src/net/byte_source.cpp
    src/net/urls.cpp
    ${TORRENT_SOURCES}
    src/ui/addons_screen.cpp
    src/ui/home_screen.cpp
    src/ui/discover_screen.cpp
    src/ui/card.cpp
    src/ui/components.cpp
    src/ui/tab_bar.cpp
    src/ui/theme.cpp
    src/ui/search_screen.cpp
    src/ui/library_screen.cpp
    src/ui/settings_screen.cpp
    src/ui/login_screen.cpp
    src/ui/details_screen.cpp
    src/ui/player_screen.cpp
    src/video/video_ffmpeg.cpp
    src/video/video_player.cpp
)
target_include_directories(stremio_ps4 PRIVATE third_party/stb)
target_link_libraries(stremio_ps4 PRIVATE PkgConfig::DEPS)
target_compile_options(stremio_ps4 PRIVATE -Wall -Wno-unused-function)
target_compile_definitions(stremio_ps4 PRIVATE APP_VERSION="${APP_VERSION}")
if(STREMIO_DEV)
    target_compile_definitions(stremio_ps4 PRIVATE STREMIO_DEV)
endif()

# Assets next to the executable (platform::asset_path uses the exe folder),
# and the PS4 tile as the window icon.
add_custom_command(TARGET stremio_ps4 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:stremio_ps4>/assets
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/sce_sys/icon0.png $<TARGET_FILE_DIR:stremio_ps4>/icon0.png)

# Command-line torrent client for engine development: streams a magnet link's
# file like the player would and reports timings (see tools/torrent_fetch.cpp).
add_executable(torrent_fetch
    tools/torrent_fetch.cpp
    src/net/http.cpp
    src/net/http_curl.cpp
    src/net/urls.cpp
    ${TORRENT_SOURCES}
)
target_link_libraries(torrent_fetch PRIVATE PkgConfig::DEPS)
target_compile_options(torrent_fetch PRIVATE -Wall -Wno-unused-function)
target_compile_definitions(torrent_fetch PRIVATE APP_VERSION="${APP_VERSION}")

# Unit tests: the platform-independent code, plus the Stremio facade against
# fake platform / HTTP layers (tests/fakes.cpp). No window, network or PS4.
option(STREMIO_TESTS "Build the unit tests" ON)
if(STREMIO_TESTS)
    enable_testing()
    add_executable(stremio_ps4_tests
        tests/main.cpp
        tests/fakes.cpp
        tests/test_bidi.cpp
        tests/test_charset.cpp
        tests/test_json.cpp
        tests/test_models.cpp
        tests/test_nv12.cpp
        tests/test_stremio.cpp
        tests/test_subtitles.cpp
        tests/test_torrent.cpp
        tests/test_urls.cpp
        tests/test_addon_search.cpp
        src/net/urls.cpp
        src/torrent/bencode.cpp
        src/torrent/magnet.cpp
        src/torrent/sha1.cpp
        src/core/json.cpp
        src/core/addon_config.cpp
        third_party/qrcodegen/qrcodegen.c
        src/core/models.cpp
        src/core/stremio.cpp
        src/core/subtitles.cpp
        src/core/charset.cpp
        src/render/bidi.cpp
    )
    target_link_libraries(stremio_ps4_tests PRIVATE PkgConfig::DEPS)
    target_compile_options(stremio_ps4_tests PRIVATE -Wall -Wno-unused-function)
    add_test(NAME unit COMMAND stremio_ps4_tests)
endif()
