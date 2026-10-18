# Stremio PS4

An unofficial, native [Stremio](https://www.stremio.com) client for
jailbroken PlayStation 4 consoles. It's designed for the TV and the
DualShock 4, and plays the streams from your Stremio add-ons on the console
itself.

## Features

- **Sign in with a QR code:** scan it with a phone that's logged in to
  Stremio. No typing on the TV.
- **Home:**
  - Continue watching, with progress and new-episode badges.
  - Rows from your add-on catalogues.
- **Search** across all your add-ons, with an on-screen keyboard.
- **Library:** everything in your Stremio library as a poster grid, filtered
  by type and sorted by recently watched, name or most watched. Add or
  remove titles from their details page with △.
- **Details:** description, seasons and episodes, and every stream your
  add-ons offer.
- **Player:**
  - Plays MKV and MP4, H.264, HEVC (including 10-bit), AAC, AC3/E-AC3, DTS
    and more. Decoding is done in software with FFmpeg, and it handles
    1080p.
  - Subtitles embedded in the video (text and Blu-ray/DVD image subtitles)
    or from subtitle add-ons such as OpenSubtitles.
  - Audio track selection.
  - Remembers your subtitle and audio languages.
- **Synced with your account:** watch progress is saved to your Stremio
  library, so you can continue on any device.

## Requirements

- A PS4 or PS4 Pro running [GoldHEN](https://github.com/GoldHEN/GoldHEN).
  Tested on a PS4 Slim with FW 11.02.
- A Stremio account with add-ons that provide **direct (HTTP/HTTPS)
  streams**, e.g. a debrid service.

## Installing

1. Download `stremio-ps4-<version>.pkg` from the
   [releases](../../releases), or [build it yourself](BUILDING.md).
2. Copy it to the console, for example with GoldHEN's FTP server, into
   `/data/pkg/`.
3. Install it with GoldHEN's Package Installer.
4. Open **Stremio PS4** and scan the QR code.

## Controls

| Button | Action |
|---|---|
| D-pad | Move |
| × | Select / play-pause |
| ○ | Back |
| L1 / R1 | Previous / next tab (Home, Search, Library, Settings) |
| ◀ ▶ (in the player) | Seek 10 s (presses add up) |
| ▲ (in the player) | Audio & subtitles |

## Limitations

- **Torrent streams** aren't supported yet; they need a streaming server.
  Streams that can't be played are listed but greyed out.
- **Subtitle fonts:** subtitles in scripts that Inter doesn't cover (for
  example Arabic, Chinese, Japanese) aren't rendered yet.

What's planned next is in [docs/ROADMAP.md](docs/ROADMAP.md).

## Building

See [BUILDING.md](BUILDING.md). `./build.sh ps4` builds the package
reproducibly in Docker. The technical background is in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## License

Stremio PS4 is free software, licensed under the
[GNU General Public License v3.0](LICENSE). It bundles FFmpeg (LGPL 2.1), the
Inter fonts (SIL Open Font License) and stb (public domain); see
[BUILDING.md](BUILDING.md#licences-of-bundled-components).

## Disclaimer

This project is not affiliated with Stremio. It uses Stremio's public API
and add-on protocol, and it doesn't host or provide any content. What you
watch comes from the add-ons you install.
