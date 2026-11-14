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
3. Its own data folder in the user directory.
4. The folder with `Jones3D` or `Jones3D.exe`, then the working directory.
5. The usual Steam and GOG folders.

The first folder that has the game data is used. If a required file is missing, a prompt names the
files. Choose `Choose folder...` and pick the game's folder, or its `Resource` folder. The prompt
repeats until the data is valid; `Quit` closes the game.

You can also point at the data directly:

```bash
./Jones3D_EX-<version>-x86_64.AppImage --data "/path/to/Indiana Jones and the Infernal Machine"
```

`--data` applies to that run only. A folder chosen in the prompt is saved in `Jones.cfg`.

## Copying the data from a CD

When the data is on a CD that you don't want to keep in the drive, or in a folder that may move, copy
the required files into the user directory once:

```bash
Jones3D --install-data "/media/cdrom"
```

or, for the AppImage:

```bash
./Jones3D_EX-<version>-x86_64.AppImage --install-data "/media/cdrom"
```

The command copies every known file it finds into `<user dir>/data/Resource/`, prints one line per
file, and checks the copy. Files already copied with the same size are skipped, and the source is never
moved. It exits without starting the game. In portable mode the copy goes into the data folder there
instead.

## Settings, saves and logs

The game writes these to a per-user directory:

| | Linux | Windows |
|---|---|---|
| User directory | `~/.local/share/Jones3DEX` | `%APPDATA%\Jones3DEX` |

Inside it are `Jones.cfg`, `SaveGames/`, `ScreenShots/`, `KeySets/`, `JonesLog.txt`, `JonesError.txt`
and `datacheck.txt`, the data check's cache. A game data copy made with `--install-data` is in `data/`
there.

The first start copies old settings and saves that it finds in the folders used before into the user
directory: `Jones.cfg` if the user directory has none, and `.nds` savegames if it has no savegames yet.
It copies; it does not move or delete anything.

### Portable mode

To keep settings and saves beside the game instead, create an empty file named `portable.txt` in the
folder you start Jones3D EX from (for a zip install, next to `Jones3D.exe` works as well). Jones3D EX
then uses that folder instead of the user directory.

Use `--user-dir <folder>` to choose another folder for one run.

## Game data check

At startup Jones3D EX checks the sizes and SHA-256 hashes of the game files against a table of known
releases. A known release gets one line in the log. A complete data set that does not match a known
release gets a warning naming the files; the game still starts.

The first check of the full data takes a few seconds. The result is cached in the user directory, so
later starts are quick.

The table knows the GOG English release today. A Steam or CD copy may therefore show the unknown-data
warning but plays normally. To have it added, run:

```bash
Jones3D --hash-data "/path/to/Indiana Jones and the Infernal Machine"
```

or the same option on the AppImage. It prints one row per known file with its size and SHA-256, and
exits without starting the game. Include that output in a report, together with where the copy came
from (GOG, Steam, CD, language). Do not attach the game files themselves.

## Command-line options

| Option | What it does |
|---|---|
| `--version` | Prints the version and exits. |
| `--data <folder>` | Uses the game data in this folder for this run. It wins over the saved `installPath`. |
| `--install-data <folder>` | Copies the required game files into the user directory, checks them and exits. |
| `--hash-data <folder>` | Prints size and SHA-256 rows for the known files and exits. |
| `--user-dir <folder>` | Uses this folder for settings, saves, screenshots, key sets and logs. |
| `--load <save>` | Starts with that savegame loaded. |

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
