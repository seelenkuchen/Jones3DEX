# Installing Jones3D EX

Jones3D EX is an enhanced engine for *Indiana Jones and the Infernal Machine*, not the game itself. It
contains no game data. You need your own copy of the game from GOG, Steam or the original CD.

## What you need

- A copy of *Indiana Jones and the Infernal Machine*.
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

The zip has no installer. The first start opens the setup wizard (below), which copies the game files next
to your settings or next to the program.

## First start: the setup wizard

Start the game. If it finds no complete game files, it opens the **Jones3D EX Setup** window before the
game window. Follow its steps:

1. **Welcome.** Read the short introduction and choose **Continue**.
2. **Source.** Choose where the game files are:
   - an installed Steam or GOG copy the wizard found in the usual places;
   - a GOG offline installer file in your download folder;
   - a CD in the drive;
   - **Choose a game folder...** for any other copy;
   - **Choose a GOG installer file...** for an installer somewhere else;
   - **Search again** after you inserted a CD or installed the game.

   A row marked `(incomplete)` is missing at least one of the game files. Choosing an installed copy or
   a folder that is incomplete shows which files are missing.
3. **Licence agreement.** A GOG installer and a CD show their licence. Read it and choose **I accept**.
   Installed copies and folders skip this step.
4. **Location.** Choose **Your user folder (recommended)** or **Next to the game**. The second choice is
   offered only when the program's folder is writable. If the source has saved games, a **Copy saved
   games** tick appears; leave it on to bring them along.
5. **Installing.** The wizard unpacks a GOG installer or copies the files and shows its progress. Choose
   **Cancel** to stop.
6. **Disc 2.** A CD install that needs the second disc asks for it. Insert the disc, choose **Search
   again** if it is not listed, or choose **Choose a folder...**.
7. **Done.** Choose **Start the game**.

The source copy is only read. The wizard never moves or deletes it.

### Where the files go

The wizard copies the game files into a folder named `GameData`:

- **Your user folder** (recommended): `<user folder>/GameData`.
- **Next to the game**: `<folder with Jones3D>/GameData`. In an AppImage this is the folder that
  holds the `.AppImage` file, not the temporary read-only mount inside it.

A complete `GameData` next to the program also makes that folder the place for settings and saves, so
everything stays together. See [Settings, saves and logs](#settings-saves-and-logs).

### Running the setup again

From the main menu, open **Options > Game data** and choose **Run setup again...**. The tab also shows
the release the startup check identified and the folder in use. The game closes its own window, opens the
setup wizard and starts again when the wizard finishes. Run setup again is disabled while a level is
loaded; return to the main menu first.

A rerun installs into `<user folder>/GameData.new` and replaces `GameData` only after the copy completes.
**Cancel**, an error or **Back to the game** keeps the old data. Your settings and saves are kept.

### GOG installer files

On Linux the AppImage includes the program that unpacks GOG installers (`innoextract`), so
**Choose a GOG installer file...** and the installer files in your download folder work out of the box.

The Windows zip does not include `innoextract`. Use an installed Steam or GOG copy, a CD, or a plain
game folder. If you have `innoextract` in your `PATH`, the wizard also offers GOG installer files on
Windows.

## Settings, saves and logs

The game writes these to a per-user directory:

| | Linux | Windows |
|---|---|---|
| User directory | `~/.local/share/Jones3DEX` | `%APPDATA%\Jones3DEX` |

The game keeps `Jones.cfg`, `SaveGames/`, `ScreenShots/`, `KeySets/`, `JonesLog.txt`,
`JonesError.txt` and `datacheck.txt` (the data check's cache) there. The game data the wizard installed
is in `GameData/`.

When the user directory has no `Jones.cfg`, the game copies the one in the working directory there.
When it has no `.nds` savegame, it copies the saves from the old save folder next to the data. It
copies; it does not move or delete anything.

### Keeping everything next to the game

Choose **Next to the game** in the setup wizard. The wizard puts `GameData` beside the program, and later
starts use that folder for settings and saves too. This needs a writable program folder; the option is
not offered otherwise.

To move an existing setup, move the whole folder, including `GameData` and the user files, or move
`GameData` alone and copy `Jones.cfg` and your saves from `SaveGames/` into the new user directory.

## Game data check

At startup Jones3D EX checks the game files against a table of known releases. It compares sizes, then
SHA-256 hashes, with the table. The table knows the GOG English release today and logs it by name. A
complete set that does not match a release gets one warning per differing file, or a warning that the
files come from different known releases; the game still starts.

The setup wizard checks the files it copied and writes the cache, so the first game start does not hash
them again. Later starts are quick too.

A Steam or CD copy may therefore show the unknown-data warning but plays normally. To have it added, run
the developer command in [Developer options](#developer-options) and include its output in a report,
together with where the copy came from (GOG, Steam, CD, language). Do not attach the game files
themselves.

## Developer options

These options are for development and problem reports. Players do not need them.

| Option | What it does |
|---|---|
| `--version` | Prints the version and exits. |
| `--data <folder>` | Uses the game data in this folder for this run. It wins over the saved `installPath` and is not saved. |
| `--hash-data <folder>` | Prints the size and SHA-256 rows for the known files it finds and exits. Exits 0, or 1 when it cannot read the data. |
| `--user-dir <folder>` | Uses this folder for settings, saves, screenshots, key sets and logs. |
| `--load <save>` | Starts with that savegame loaded. |

The headless runner `Jones3D-headless` takes `--data`, `--hash-data`, `--user-dir` and `--load`. Without
`--user-dir` it writes to the working directory. With `--load` it loads the save only when `startMode`
is 1 (load game); the windowed game starts that save directly.

`--data` applies to that run only. `installPath` in `Jones.cfg` is still read when it is set, but the
game no longer sets it.

Data paths up to 576 bytes work on Linux. Windows still limits paths to 260 characters.

The old `--install-data` option and `portable.txt` are gone. The setup wizard replaces both.

## Troubleshooting

- **No window, or an error about Vulkan.** Jones3D EX renders on Vulkan through SDL3 GPU. Install the
  Vulkan driver for your graphics card from your distribution, then try again.
- **The AppImage doesn't start.** The type-2 AppImage runtime works without `libfuse2` on most systems.
  If yours still asks for FUSE, run it with `--appimage-extract-and-run`:
  `./Jones3D_EX-<version>-x86_64.AppImage --appimage-extract-and-run`.
- **The setup wizard does not find the game.** Check that `Jones3D.GOB`, `CD1.GOB` and `CD2.GOB` are in
  the folder or in its `Resource` folder, then choose **Choose a game folder...** and pick it.
- **The wizard does not offer a GOG installer file.** On Windows, `innoextract` is not included. Use an
  installed copy, a CD or a plain game folder, or put `innoextract` in your `PATH`.
- **No file dialog is available.** On Linux the file dialog comes from the desktop portal
  (`xdg-desktop-portal` with your desktop's backend) or `zenity`. Install one of them, or choose **Search
  again** after putting the game where the wizard looks: an installed Steam or GOG copy, a GOG installer
  in the download folder, or a CD.
- **The log says the data is unknown.** That is a warning, not an error. If you want the release added,
  see [Game data check](#game-data-check).

## See also

- [`Jones.cfg` reference](Jones.cfg.md)
- [ADR-0034: the setup wizard](arc42/09-decisions/ADR-0034-setup-wizard.md)
- [ADR-0032: packages and game data location](arc42/09-decisions/ADR-0032-packages-and-game-data-location.md)
