# Installing Jones3D EX

Jones3D EX is an enhanced engine for *Indiana Jones and the Infernal Machine*, not the game itself. It
contains no game data. You need your own copy of the game from GOG, Steam or the original CD.

## What you need

- A copy of *Indiana Jones and the Infernal Machine*.
- The game data files `Jones3D.GOB`, `CD1.GOB` and `CD2.GOB`. The intro movies `jonesopn*.snm` are
  optional.
- On Linux: a 64-bit system with glibc 2.39 or newer (Ubuntu 24.04, Debian 13, Fedora 40 or newer) and
  a Vulkan driver.
- On Windows: 64-bit Windows 10 or newer.

Jones3D EX never downloads game data. Use the files from your own copy.

## Linux

Download `Jones3D_EX-<version>-x86_64.AppImage`, make it executable and run it:

```bash
chmod +x Jones3D_EX-<version>-x86_64.AppImage
./Jones3D_EX-<version>-x86_64.AppImage
```

The AppImage is one file and installs nothing into the system. Put it wherever you like.

The AppImage needs a Vulkan driver from your distribution. If the game does not open a window, see
[Troubleshooting](#troubleshooting).

## Windows

Download `Jones3D_EX-<version>-windows-x64.zip` and extract it into any folder. Run `Jones3D.exe` from
there.

The zip has no installer. Settings and saves go to the user directory (below); the extracted files
stay in their folder.

## First start

On first start Jones3D EX looks for the game data itself. It tries, in order:

1. The folder given with `--data`.
2. The `installPath` saved in `Jones.cfg`.
3. Its own data folder in the user directory: `<user dir>/data`, or `data` in portable mode.
4. The folder the program is in, then the working directory.
5. Steam libraries, then GOG and other store folders.

The first folder that has `Jones3D.GOB`, `CD1.GOB` and `CD2.GOB` is used. A folder may hold the
files itself or in its `Resource` folder.

When no complete folder is found, a window titled `Jones3D EX` says: "Jones3D EX needs the game data
from your copy of Indiana Jones and the Infernal Machine (GOG, Steam or CD): Jones3D.GOB, CD1.GOB
and CD2.GOB." For an incomplete folder it adds the folder and the missing files, then says: "Choose
the game's folder or the Resource folder inside it." The buttons are `Choose folder...` and `Quit`.
Escape and closing the window choose `Quit`, which closes the game. If the folder picker cannot be
shown, the game shows the message and the hint `Start Jones3D EX with --data <folder>.` and exits.

The store search looks for `Indiana Jones and the Infernal Machine` in:

- `<Steam library>/steamapps/common/` for the Steam roots and every library listed in
  `steamapps/libraryfolders.vdf`. On Linux the roots are `~/.steam/steam`, `~/.local/share/Steam`
  and `~/.var/app/com.valvesoftware.Steam/.local/share/Steam`; on Windows
  `%ProgramFiles(x86)%\Steam` and `%ProgramFiles%\Steam`.
- `C:\GOG Games` and `%ProgramFiles(x86)%\GOG Galaxy\Games` on Windows.
- `~/GOG Games`, `~/Games` and `~/Games/Heroic` on Linux.

You can also point at the data directly:

```bash
./Jones3D_EX-<version>-x86_64.AppImage --data "/path/to/Indiana Jones and the Infernal Machine"
```

`--data` applies to that run only. A folder chosen in the prompt is saved as `installPath` in
`Jones.cfg`; a folder found automatically or given with `--data` is not saved.

## Copying the data from a CD

When the data is on a CD that you don't want to keep in the drive, or in a folder that may move, copy
the files into the user directory:

```bash
Jones3D --install-data "/media/cdrom"
```

or, for the AppImage:

```bash
./Jones3D_EX-<version>-x86_64.AppImage --install-data "/media/cdrom"
```

Run the command once for each CD. Each run copies the known files it finds into
`<user dir>/data/Resource/` and prints `copied` or `unchanged` for each one. Files already copied with
the same size are skipped, and the source is never moved. A single disc run is usually incomplete, so the
command exits 1 until the last disc is copied; it exits 0 only when `Jones3D.GOB`, `CD1.GOB` and
`CD2.GOB` are all in place. It never starts the game. In portable mode the copy goes into `data/`
under the folder in use.

## Settings, saves and logs

The game writes these to a per-user directory:

| | Linux | Windows |
|---|---|---|
| User directory | `~/.local/share/Jones3DEX` | `%APPDATA%\Jones3DEX` |

The game keeps `Jones.cfg`, `SaveGames/`, `ScreenShots/`, `KeySets/`, `JonesLog.txt`,
`JonesError.txt` and `datacheck.txt` (the data check's cache) there. A game data copy made with
`--install-data` is in `data/`.

When the user directory has no `Jones.cfg`, the game copies the one in the working directory there.
When it has no `.nds` savegame, it copies the saves from the old save folder next to the data. It
copies; it does not move or delete anything.

### Portable mode

To keep settings and saves beside the game instead, create a file named `portable.txt` (it can be
empty) in the folder you start Jones3D EX from. For a zip install, a `portable.txt` next to
`Jones3D.exe` also works. Jones3D EX uses the working directory when `portable.txt` is there, else the
executable's folder when it is there. `--user-dir <folder>` chooses another folder for one run.

## Game data check

At startup Jones3D EX checks the game files against a table of known releases. It compares sizes, then
SHA-256 hashes, with the table. The table knows the GOG English release today and logs it by name. A
complete set that does not match a release gets one warning per differing file, or a warning that the
files come from different known releases; the game still starts.

The first check of the full data takes a few seconds. It is cached in `datacheck.txt` in the user
directory, so later starts are quick.

A Steam or CD copy may therefore show the unknown-data warning but plays normally. To have it added,
run:

```bash
Jones3D --hash-data "/path/to/Indiana Jones and the Infernal Machine"
```

or the same option on the AppImage. It prints one `{ "name", size, "sha256" },` row for each known
file it finds and exits 0; it exits 1 with an error when it cannot read the data. Include that output
in a report, together with where the copy came from (GOG, Steam, CD, language). Do not attach the game
files themselves.

## Command-line options

| Option | What it does |
|---|---|
| `--version` | Prints the version and exits. |
| `--data <folder>` | Uses the game data in this folder for this run. It wins over the saved `installPath` and is not saved. |
| `--install-data <folder>` | Copies the known game files it finds into `<user dir>/data/Resource`, checks the set and exits. Exits 0 only when the set is complete, 1 otherwise; run it once per CD. |
| `--hash-data <folder>` | Prints the size and SHA-256 rows for the known files it finds and exits. Exits 0, or 1 when it cannot read the data. |
| `--user-dir <folder>` | Uses this folder for settings, saves, screenshots, key sets and logs. |
| `--load <save>` | Starts with that savegame loaded. |

The headless runner `Jones3D-headless` takes `--data`, `--hash-data`, `--install-data`, `--user-dir`
and `--load` too. Without `--user-dir` it writes to the working directory. With `--load` it loads the
save only when `startMode` is 1 (load game); the windowed game starts that save directly.

## Troubleshooting

- **No window, or an error about Vulkan.** Jones3D EX renders on Vulkan through SDL3 GPU. Install the
  Vulkan driver for your graphics card from your distribution, then try again.
- **The AppImage doesn't start.** The type-2 AppImage runtime works without `libfuse2` on most systems.
  If yours still asks for FUSE, run it with `--appimage-extract-and-run`:
  `./Jones3D_EX-<version>-x86_64.AppImage --appimage-extract-and-run`.
- **The game cannot find the data.** Check that `Jones3D.GOB`, `CD1.GOB` and `CD2.GOB` are in the folder
  or in its `Resource` folder, then start with `--data "<folder>"`.
- **The log says the data is unknown.** That is a warning, not an error. If you want the release added,
  see [Game data check](#game-data-check).

## See also

- [`Jones.cfg` reference](Jones.cfg.md)
- [ADR-0032: packages and game data location](arc42/09-decisions/ADR-0032-packages-and-game-data-location.md)
