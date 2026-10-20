# Contributing to Stremio PS4

Thanks for helping. Bug reports, testing on other consoles and firmwares, and
pull requests are all welcome.

## Reporting a bug

Open an issue with:

- **Your setup:**
  - console model (PS4, Slim, Pro) and firmware;
  - GoldHEN version;
  - the app version from Settings.
- **What happened,** and the steps to make it happen again.
- **For playback problems,** the kind of stream:
  - container and codecs (e.g. MKV, HEVC 10-bit, E-AC3);
  - resolution;
  - which add-on it came from.
- **The app log:** `/data/stremio_ps4.log`, fetched with GoldHEN's FTP
  server. For a crash, also add the kernel log (`nc <ps4-ip> 3232`).

**Check the log before posting it.** The app shortens stream and add-on URLs
in the log to hide the account keys they carry. Logs from versions before
1.0.4 contain full URLs, which can include your personal debrid tokens, so
remove those first.

## Suggesting a feature

What's already planned is in [docs/ROADMAP.md](docs/ROADMAP.md). Open an issue
for anything that isn't on it, or to say which planned item matters most to
you. For larger changes, please discuss them in an issue before starting, so
work isn't duplicated or done in a direction that won't be merged.

## Development setup

Most work is done on the desktop build, which runs the same code with SDL2.
[BUILDING.md](BUILDING.md) covers building it, the keyboard controls, and
building the PS4 package.

```sh
./build.sh desktop --dev
STREMIO_PS4_DATA=/tmp/stremio-dev STREMIO_PS4_DEMO=1 build/stremio_ps4
```

**Use demo mode and a separate data folder while developing.** With them,
the app never touches your real session and never writes progress or library
changes to your Stremio account. Only sign in with your own account to test
syncing on purpose.

## Making a change

### Code

- **Language:** C++17.
- **Formatting:** match the surrounding code, which is indented with tabs.
- **Naming:**
  - `snake_case` for functions and variables;
  - `PascalCase` for types;
  - `UPPER_CASE` for constants;
  - a trailing `_` for members.
- **Comments:** explain why the code does something, not what it does.
- **Porting Stremio behaviour:** follow stremio-core
  (https://github.com/Stremio/stremio-core) so the app behaves like the
  official clients, and name the stremio-core model or function the code
  comes from.
- **PS4 limitations:** read the "PS4 pitfalls" section of
  [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) before touching threading,
  networking, video or the renderer. Several things that work on desktop
  crash or stall on the console.
- **Dependencies:** a new one must build for the PS4 with the OpenOrbis
  toolchain and have a licence compatible with the GPL v3.0. Please ask in
  an issue first.

### UI

- **Theme tokens:** screens use the roles in `src/ui/theme.h`
  (`BASE_200`, `PRIMARY`, ...) and the sizes scaled with `S()`. Never use raw
  colours.
- **Shared components:** use the ones in `src/ui/components.h` (chips, focus
  rings, ...). If a screen needs something new that others could use, add it
  there.
- **Design rules:** [docs/DESIGN.md](docs/DESIGN.md) has the typography,
  colours and focus rules.
- **Controller-first:** everything must be reachable with the D-pad, ×
  and ○, and the focused item must always be visible from across the room.

### Tests

- Run `./build.sh test` before opening a pull request.
- Changes to the Stremio logic in `src/core` should come with a test in
  `tests/`. The fakes in `tests/fakes.cpp` stand in for the API and the
  add-ons, so tests don't need a network or an account.
- Changes to UI, video or platform code can't be unit-tested. Say how you
  tested them in the pull request: desktop only, or on a console, and which
  model and firmware.

### Docs

Update the README, [BUILDING.md](BUILDING.md) or the files in `docs/` when a
change affects features, controls, building or design.

## Pull requests

- **Scope:** one topic per pull request. Small ones are reviewed faster.
- **CI:** it builds the PS4 package and the desktop build and runs the
  tests. It must pass.
- **Screenshots:** for UI changes, add one. The developer build can take
  them with `STREMIO_PS4_SCRIPT`; see BUILDING.md.
- **Description:** describe what changed and why.

## Licence

Stremio PS4 is licensed under the [GNU GPL v3.0](LICENSE). Contributions are
accepted under the same licence. Only submit code that you wrote yourself or
that is under a GPL-compatible licence, and keep the original copyright
notice on code that comes from elsewhere.

## Out of scope

This app is a client for Stremio add-ons and doesn't host or provide any
content. Pull requests that add or link to sources of pirated content won't
be accepted.
