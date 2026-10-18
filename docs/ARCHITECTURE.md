# Architecture

Stremio PS4 is a native C++17 app. A single codebase builds for:

- **PS4:** OpenOrbis homebrew, using Piglet (OpenGL ES 2.0) and the PS4's
  system libraries.
- **Desktop:** SDL2 and OpenGL ES 2, for development.

## Why native (and not an engine)

PS4 homebrew gets OpenGL ES 2.0 through Piglet, but it has no runtime shader
compiler. GLSL can't be compiled on the console, so engines such as Godot
can't run. The UI is drawn with three of the PS4's own precompiled
system-UI ("swordfish") shaders, which handle solid colours, textured images
and text masks. Video frames are converted from YUV to RGBA on the CPU with
SSE2, because there is no YUV shader.

## Layers

| Folder | PS4 | Desktop |
|---|---|---|
| `src/platform/` | `platform_ps4.cpp`: Piglet/EGL, DualShock 4, precompiled shaders, files, log | `platform_desktop.cpp`: SDL2 + GLES2 with GLSL copies of the shaders |
| `src/render/` | Renderer (rectangles, rounded shapes, images, fades, text) and FreeType glyph atlas | same |
| `src/core/` | JSON, Stremio models, the `Stremio` facade (session, QR sign-in, library, add-ons, Continue watching, notifications, catalogs, search, player progress rules), subtitle parsing | same |
| `src/net/` | Async HTTP (`http_ps4.cpp`: libSceHttp), image cache, `ByteSource` (what the players read from) and the parallel range reader for HTTP video | `http_curl.cpp`: libcurl |
| `src/torrent/` | The torrent engine (below): BSD sockets with `SO_NBIO`, DNS through `sceNetResolver` | BSD sockets, `getaddrinfo` |
| `src/ui/` | Screens: sign-in, Home, Search, Library, Settings, details, player; theme tokens and shared components (docs/DESIGN.md) | same |
| `src/video/` | FFmpeg player (default) and AvPlayer hardware player | FFmpeg player |

The `Stremio` facade ports the parts of
[stremio-core](https://github.com/Stremio/stremio-core) the app needs:

- library items and Continue watching;
- notifications from `lastVideosIds` catalogues;
- the catalogue board, and search across add-on catalogues;
- meta and stream requests, and subtitles from add-ons;
- the player's progress rules: watched at 70%, credits at 90%, saving
  progress every 90 s.

## Video

- **`video_ffmpeg.cpp` (default):**
  - Software decoding (MKV/MP4/TS, H.264, HEVC including 10-bit, VP9, AV1,
    AAC/AC3/E-AC3/DTS/TrueHD/Opus…) on 5 threads.
  - The audio output queue is the master clock.
  - Embedded subtitles (text and PGS/DVD images) and audio-track switching.
  - It reads the stream through a `ByteSource`: `RangeReader` for HTTP
    (Range requests in 1 MB blocks over 4 parallel connections, up to 24 MB
    ahead, redirects resolved once) or `TorrentStream` for magnet links.
- **`video_avplayer.cpp`:**
  - The PS4's hardware decoder, for MP4/H.264, with `sceAudioOut` for sound.
  - AvPlayer reads through its file-replacement callbacks, which are
    answered on an I/O thread from the same `ByteSource` as FFmpeg.
  - It can be selected in Settings.
- **`nv12.h`:** SSE2 YUV 4:2:0 → RGBA conversion for NV12, I420 and 10-bit
  planar input. It takes about 4 ms per 1080p frame on a PS4.

## Torrents

Add-ons return torrent streams as `infoHash` + `fileIdx` + `sources`.
`StreamItem::playable_url()` turns them into a magnet link (`so=` carries the
file index), and `ByteSource::create` opens it as a `TorrentStream`. The
engine (`src/torrent/torrent_stream.cpp`) is a download-only BitTorrent
client for one file:

- **Peers** from UDP/HTTP trackers (the link's plus six public ones), a
  `get_peers`-only DHT client marked read-only (BEP 43), and peer exchange.
  Up to 80 connection attempts run at once and 50 peers stay connected; a
  peer that doesn't complete the handshake in 3 s counts as failed. When
  every address has been tried and few peers are sending, trackers and the
  DHT are asked again sooner (45 s / 15 s instead of 120 s / 60 s), and
  addresses that failed are retried after 30 s instead of 180 s.
- **Metadata** (the file list) comes from peers (`ut_metadata`) and is
  checked against the info-hash. `open()` returns once it's in and the file
  is picked: `so`, else the largest video.
- **Pieces** are fetched from the read position onwards: about 90 s of video
  at the rate the player reads (40–112 MiB), plus the file's last 2 MiB
  early, where MP4 and MKV keep their index. Blocks for the two most urgent
  pieces are re-requested from a second peer after 2 s. Every piece is SHA-1
  checked.
- **Memory only:** verified pieces stay in a 160 MiB cache, the ones
  farthest from the read position dropped first. Nothing is written to the
  PS4's hard drive (16 KiB random writes run at 6 MB/s there).
- **Threads:** one `poll()` loop owns all sockets; tracker announces and DNS
  run on short-lived threads, and the player's I/O thread waits in `read()`.

`build/torrent_fetch MAGNET [MiB] [seek] [pace MB/s]` (desktop) reports time
to the file list, first byte, speed and seek delay.

## PS4 pitfalls

- **No automatic library loading.** Every system library must be loaded
  explicitly, with `sceSysmoduleLoadModule(Internal)` or
  `sceKernelLoadStartModule` and the sandbox path
  (`/<sceKernelGetFsSandboxRandomWord()>/common/lib/...`). Calling an
  unloaded library crashes with `PRX_NOT_RESOLVED_FUNCTION`.
- **Load order matters.** `sceUserServiceInitialize` and `sceAudioOutInit`
  crash unless their modules were loaded first.
- **GnmDriver.**
  - AvPlayer's H.264 decoder (`libSceVdecSavc`) needs `libSceGnmDriver`.
  - Once that is loaded, the system won't suspend or close the app until
    `sceGnmSubmitDone` is called (crash code
    `CPU_FAULT_SUBMITDONE_TIMEOUT_IN_SUSPEND_ASYNC`).
  - Calling it every frame stalls the GPU, so the app only calls it while
    the system UI covers the app.
- **Piglet memory.** Piglet reserves direct memory up front. The sample
  values (250 + 512 MB) leave none for the video decoder, so the app uses
  128 + 256 MB.
- **Tiny stacks in callbacks.** AvPlayer calls its callbacks on threads with
  tiny stacks: don't log or allocate there.
- **libSceHttp stack size.** libSceHttp fails with
  `INSUFFICIENT_STACKSIZE` on small stacks. All HTTP runs on threads with
  1 MB stacks.
- **No vsync.** `eglSwapBuffers` blocks forever with vsync on, so the app
  caps itself at 60 fps.
- **Non-blocking sockets.** `fcntl(O_NONBLOCK)` silently does nothing,
  both the libc and the libkernel one. Only
  `setsockopt(SOL_SOCKET, 0x1200 /* SO_NBIO */)` works.
- **DNS.** `getaddrinfo` fails; `sceNetResolverStartNtoa` (with a pool from
  `sceNetPoolCreate`) works. A process can have at most 256 sockets.
- **Texture row width.** Uploading a 1436-pixel-wide video frame took
  ~50 ms per frame; padded to 1440 it takes 1.7 ms. The FFmpeg player pads
  frame rows to a multiple of 32 pixels and draws only the picture's columns
  (`Texture::shown_width`).
- **Translucent text.** The text shader ignores the alpha of the fill
  colour, so translucent text is pre-blended toward the backdrop.

## Debugging on the console

See [BUILDING.md](../BUILDING.md#debugging-on-the-console).
