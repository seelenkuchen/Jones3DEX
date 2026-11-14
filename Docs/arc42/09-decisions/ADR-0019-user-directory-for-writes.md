# ADR-0019: Writes go to a user directory; the game keeps its search order

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

Quake 3 and libultraship read through an ordered list of locations and archives, and Quake 3 sends all writes
to a separate home path ([research](../../Research/Engine-Patterns.md)).

Jones3D already reads that way: `JonesFile_OpenFile` searches the install directory, the working directory,
the CD and `Resource`, each first for a loose file and then in its GOB archives. Loose files win over
archives.

Writes don't: `Jones.cfg` and savegames are written relative to the working directory, next to the game data.
Game data directories can be read-only, as in a macOS app bundle or a system-wide Linux install, and several
users can share one install.

## Decision

- The read search order stays in game code (`JonesFile`). Mod folders, when they come (M11 and later), are further entries
  in that list, marked `// Added:`.
- Babylon's Files module provides the per-user writable directory for each platform, through SDL3.
- `Jones.cfg`, savegames, replays and logs are written there.
- Existing files are still found: if no file exists in the user directory, the old location next to the game
  data is read.

## Consequences

- Babylon's Files module stays small: file access, paths and user directories, no archive or mount logic.
- Moving the write location is a user-visible change and goes into `CHANGELOG.md` when it lands.
- Tests point the user directory at a temporary directory. Headless runs default to the working directory
  and accept `--user-dir`, so they can be kept away from real saves.

## Implementation

Implemented in M8 (2026-11-14): the windowed Babylon game writes `Jones.cfg`, savegames, screenshots, key
sets, `JonesLog.txt`, `JonesError.txt` and the `datacheck.txt` cache to the directory chosen by
`--user-dir`, a `portable.txt` in the working directory or beside the executable (the working directory
is checked first), or else `FileSystem::userPath("", "Jones3DEX")`
(`~/.local/share/Jones3DEX/` on Linux, `%APPDATA%\Jones3DEX\` on Windows,
`~/Library/Application Support/Jones3DEX/` on macOS). An existing `Jones.cfg` in the working directory is
copied there when the user directory has none, and existing `.nds` saves are copied from the old save
folder when it has none; the old files are kept. Headless runs default to the working directory and
accept `--user-dir`.
