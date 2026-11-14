# ADR-0032: Packages and game data location

- **Status:** Accepted
- **Date:** 2026-11-14

## Context

M8 is the first release. Players own the game data and the project ships none, so a package contains only the
engine, its UI assets and licenses. It has to run from a read-only location and must not install anything into
the system. Writes already go to a user directory ([ADR-0019](ADR-0019-user-directory-for-writes.md)), but where
the game data comes from and how a first start finds it were still open.

Game data sits in different places depending on where the player bought the game: Steam libraries, GOG folders,
or a CD. Requiring a copy into one fixed location would duplicate a gigabyte or more of files that are already
there.

The pinned shader toolchain downloads a prebuilt DXC whose shared library needs glibc 2.36 and 2.38 symbols, so a
Linux package built on Ubuntu 22.04 (glibc 2.35) cannot link it.

## Decision

- **Linux packages as an AppImage** (x86-64), built in the release workflow on Ubuntu 24.04. It is one file, with
  no runtime, repository or system install. The glibc floor follows the build host and the DXC toolchain: 2.39,
  so the Ubuntu 24.04, Debian 13 and Fedora 40 era or newer. It needs a Vulkan driver from the system. Flatpak is
  deferred; it can be added later without changing the game.
- **Windows packages as a zip** with `Jones3D.exe`, `Data/`, licenses, README and changelog, built with the static
  CRT so no Visual C++ redistributable is needed. It runs on 64-bit Windows 10 or newer.
- **Writes go to the SDL user path `Jones3DEX`** ([ADR-0019](ADR-0019-user-directory-for-writes.md)):
  `~/.local/share/Jones3DEX` on Linux and `%APPDATA%\Jones3DEX` on Windows. **Portable mode** keeps everything in
  the folder instead: a `portable.txt` in the working directory selects the working directory, else one next to
  the executable selects the executable's directory. `--user-dir` overrides the choice for one run.
- **Data search order**, first folder with `Jones3D.GOB`, `CD1.GOB` and `CD2.GOB` wins: `--data <dir>`,
  `installPath` from `Jones.cfg`, `<user dir>/data` (`data` in portable mode), the executable's directory, the
  working directory, Steam libraries, then GOG and other store folders. A folder may hold the files in its
  `Resource` subfolder. The folder found this way becomes the run's install path. Only a folder the player picks
  in the folder picker is written back to `installPath`.
- **Point, don't copy:** the game uses an existing GOG, Steam or CD install where it is. `--install-data <dir>`
  copies the known files into `<user dir>/data/Resource` for discs and moving installs, one disc at a time; the
  source is never moved.
- **First start without usable data** names the required files and offers a folder picker in the windowed build.
  Quitting, or a headless run without `--data`, fails cleanly with a non-zero exit status.
- **The release workflow** builds both artifacts on `v*` tags and on `workflow_dispatch`, runs the unit tests and
  uploads the artifacts to the workflow run. It has read-only permissions and neither creates a GitHub release nor
  publishes anything; the owner attaches the artifacts by hand for now.

## Consequences

- One Linux artifact that runs on distributions from the Ubuntu 24.04 era or newer. Older systems need to build
  from source.
- The Windows package needs no Visual C++ redistributable; nothing is installed system-wide.
- The game never downloads game data and never needs write access beside its own files.
- Store locations are heuristics. Steam or GOG layout changes, new languages and new releases need search-order
  tests and rows in the data table of [ADR-0021](ADR-0021-detect-game-data.md).
- Releasing stays a manual, supervised step: the workflow produces artifacts only.
