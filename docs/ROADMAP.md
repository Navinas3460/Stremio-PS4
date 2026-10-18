# Roadmap

Plans for Stremio PS4 after the first release, roughly in priority order.
Nothing here is promised.

## Release 1.0

- **Hardware check of the rename:** the app tile shows the new icon and name,
  and an existing session in `/data/stremio_tv/` moves to
  `/data/stremio_ps4/`, so users stay signed in after updating.
- **Tag v1.0.0.** CI builds the package and attaches it to the GitHub
  release.
- **Test builds from this repo:** `./build.sh ps4 --test N` with a separate
  title and content ID, so test packages install next to the release one. It
  replaces the older development copy that test builds still come from.

## Features

### Up next

When an episode ends, show a "Next episode in 10 s" card with Play and Cancel,
then start the next episode. `MetaItem::next_video` already finds it, and
the player already moves the library item to it after the credits. The new
parts are the card and finding the next episode's streams again, preferring
the same add-on and quality.

### Discover tab

Stremio's catalogue browser: pick a type, a catalogue and its filters (genre,
year, ...), then page through a poster grid. Home only shows the first items
of each catalogue, so most titles can only be reached by searching today.
`Catalog` already parses which extras a catalogue supports (`genre`, `skip`,
...); the genre options themselves still need parsing.

### Subtitle settings

Size, a timing offset for out-of-sync subtitles (± 0.25 s steps from the
player menu), and a background behind the text. These are remembered like
the subtitle language.

### Remove from Continue watching

Stremio's "dismiss" action for titles the user has stopped watching: reset
the item's progress and push it to the library, as stremio-core's
`RewindLibraryItem` does.

## Polish

### Non-Latin subtitles

Inter has no Chinese, Japanese, Korean or Arabic glyphs, so those subtitles
render blank. Probe whether the app can load the PS4's system fonts and use
them as a fallback when a glyph is missing.

### Image cache on disk

Keep downloaded artwork (shrunk, as already done for textures) in the data
folder, so Home and Library show posters instantly on launch instead of
downloading them every time.

### Details page extras

Rating, genres, runtime and cast, from fields add-ons already return in
their meta responses.

## Torrent streams

Most Stremio add-ons only offer torrents, and only direct HTTP streams play
today. Stremio's own streaming server is Node.js, which can't run on the PS4
(no JIT for V8, no Node port, and it spawns FFmpeg processes). Two options:

- **A native engine in the app:** a streaming-only BitTorrent client that
  fetches pieces in playback order and serves them through the existing
  `RangeReader`. The largest option, but it keeps the app self-contained.
- **A helper on a PC:** the official streaming server on another machine on
  the network, with the app sending torrent streams to it. Small on the PS4
  side, but users need a computer running.

First step either way: a probe on the console to measure how much memory,
CPU and network throughput are left while 1080p video is playing.
