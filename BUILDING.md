# Building Stremio PS4

This repository builds two things:

- the PS4 package (OpenOrbis homebrew);
- a desktop build of the same code, for development.

## PS4 package (Docker, recommended)

Requirements: Linux x86-64 with Docker or Podman, and an internet connection.

```sh
./build.sh ps4
```

The result is `build_ps4/stremio-ps4-<version>.pkg`. The first run builds the
build image and FFmpeg, which takes about 10 minutes. FFmpeg is cached in
`ffmpeg_ps4/` and reused after that.

### Pinned versions

| Component | Version | Verified by |
|---|---|---|
| Base image | Ubuntu 24.04 (clang/lld/llvm 18, nasm) | apt |
| OpenOrbis PS4 toolchain | v0.5.4 `toolchain-llvm-18.tar.gz` | sha256 in `tools/docker/Dockerfile` |
| OpenSSL 1.1 (needed by PkgTool.Core) | `libssl1.1_1.1.1f-1ubuntu2.24` | sha256 in `tools/docker/Dockerfile` |
| FFmpeg | 7.1, LGPL, static, only the parts the app uses | sha256 in `tools/build_ffmpeg_ps4.sh` |

## PS4 package without Docker (Linux)

Requirements:

- **OpenOrbis toolchain:** v0.5.4 (`toolchain-llvm-18.tar.gz` from
  https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/releases). Set
  `OO_PS4_TOOLCHAIN` to its `PS4Toolchain` folder.
- **LLVM:** clang, lld and llvm (llvm-ar etc.), version 18 or newer.
- **Build tools:** `make`, `nasm`, `curl`.
- **OpenSSL 1.1 libraries:** needed by `PkgTool.Core`. On Arch, install
  `openssl-1.1`.

```sh
export OO_PS4_TOOLCHAIN=/path/to/OpenOrbis/PS4Toolchain
./build.sh ps4 --local
```

This runs `tools/build_ffmpeg_ps4.sh` once, then `make -f Makefile.ps4`.

## Versions and releases

The version lives in `VERSION` (`major.minor.patch`). It's shown in
Settings, and the PS4 package's version is derived from it (`01.00` for
1.0.x).

GitHub Actions (`.github/workflows/build.yml`) builds the package on every
push. Pushing a tag such as `v1.0.0` also attaches the package to a GitHub
release.

## Desktop build

Packages needed:

| Distribution | Packages |
|---|---|
| Arch | `cmake sdl2 mesa freetype2 curl ffmpeg` |
| Ubuntu | `cmake g++ pkg-config libsdl2-dev libgles-dev libfreetype-dev libcurl4-openssl-dev libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev` |

```sh
./build.sh desktop
build/stremio_ps4
```

| Action | Keys |
|---|---|
| Move | Arrows |
| Select | Enter / Space |
| Back | Esc / Backspace |
| Switch tabs | Q / E (L1 / R1) |
| Add to / remove from library | T (△) |

The window size can be changed with `STREMIO_PS4_WINDOW=WxH`. Set
`STREMIO_PS4_DATA=<dir>` to use another data folder instead of
`~/.local/share/stremio_ps4`.

### Tests

```sh
./build.sh test
```

The unit tests (`tests/`) need no window, network or console, and run in CI
on every push. They cover:

- JSON, subtitle (SRT/WebVTT) and add-on model parsing.
- The SSE2 YUV → RGBA conversion, checked against the scalar reference.
- The Stremio facade, run against a fake API and fake add-ons
  (`tests/fakes.cpp`): session, library and add-on sync, the board,
  new-episode notifications, Continue watching, search, signing out when the
  key is revoked, and the player's watch-progress rules.

`build/stremio_ps4_tests <name>` runs only the tests whose name contains
`<name>`; `TEST_VERBOSE=1` shows the app's log.

### Developer tools

`./build.sh desktop --dev`, or CMake with `-DSTREMIO_DEV=ON`, adds tools that
are never part of the PS4 package:

| Tool | How |
|---|---|
| Demo session: sample library, nothing written to any account | `STREMIO_PS4_DEMO=1` |
| Extra add-ons on top of the account's | `STREMIO_PS4_DEMO_ADDONS=<manifest-url>,…` |
| Scripted runs with screenshots | `STREMIO_PS4_SCRIPT="wait 300; r1; wait 30; shot /tmp/s.png; quit"` |
| An fps and draw-call counter | on by default in these builds |

## Debugging on the console

- **App log:** `/data/stremio_ps4.log`. Every line is flushed immediately,
  so it survives crashes.
- **Kernel log:** `nc <ps4-ip> 3232`, served by GoldHEN. Crash reports
  there show the exception, the thread name and the loaded system modules.
- **System libraries:** `curl -Q DECRYPT ftp://<ps4-ip>:2121/system/common/lib/X.sprx`
  downloads a system library decrypted, so you can read its imports.
- **Data folder:** `/data/stremio_ps4/` holds the session, the remembered
  subtitle and audio languages, and the video player choice from Settings
  (`player.cfg`).

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) explains how the app is built
and the PS4 pitfalls it works around.

## Folder layout

| Path | Contents |
|---|---|
| `src/core` | Stremio logic: API, library, add-ons, player rules, JSON, subtitles |
| `src/net` | HTTP (curl / libSceHttp), images, the parallel range reader for video |
| `src/render` | OpenGL ES 2 renderer and FreeType fonts |
| `src/ui` | Screens (sign-in, Home, Search, Library, Settings, details, player), theme tokens, shared components |
| `src/video` | Players: FFmpeg (default), AvPlayer (PS4 hardware), YUV conversion |
| `src/platform` | PS4 (Piglet, pad, kernel) and desktop (SDL2) back-ends |
| `assets` | Inter fonts |
| `sce_sys` | Package icon |
| `third_party/stb` | stb_image, stb_image_write |
| `tests` | Unit tests, fake platform / HTTP layers |
| `tools` | FFmpeg build script, Docker build image |
| `docs` | Architecture and design notes |

## Licences of bundled components

Stremio PS4 itself is licensed under the GNU GPL v3.0 ([LICENSE](LICENSE)).

- **FFmpeg** is built as LGPL 2.1 (no GPL parts) and linked statically. The
  LGPL 2.1 is compatible with the GPL v3.0. Publishing the complete source
  of the app, as this repository does, also covers FFmpeg's relinking
  requirement. `tools/build_ffmpeg_ps4.sh` fetches FFmpeg's source.
- **Inter fonts:** SIL Open Font License (`assets/fonts/Inter-LICENSE.txt`).
- **stb:** public domain / MIT.
- **stremio-core:** the library, add-on and player-progress logic in
  `src/core` is ported from [stremio-core](https://github.com/Stremio/stremio-core),
  MIT, Copyright © 2019 SmartCode OOD
  (`third_party/stremio-core/LICENSE.md`).
