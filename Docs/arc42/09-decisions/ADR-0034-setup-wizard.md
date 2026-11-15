# ADR-0034: A setup wizard for the game data

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

M8 ships no game data, and a player's copy comes from an installed Steam or GOG version, a GOG offline
installer, or one of the two CDs. [ADR-0032](ADR-0032-packages-and-game-data-location.md) made the game
search stores and ask for a folder in a dialog, with `--install-data` and `portable.txt` as the ways to
put the files where the game could find them. Those are command-line and hand-made-file workflows.

The owner's rule is that players need neither: a first start must turn any common source of the game
into a playable install from the game's own window. Jones3D-Portable's wxWidgets data installer is the
model, for its flow and source choices only; no code is taken from it.

## Decision

- **A wizard before the game window.** When startup finds no complete game data, the windowed game opens
  **Jones3D EX Setup** (960x640) before its own window. The steps are Welcome, Source, Licence,
  Location, Progress, Disc 2 and Done, with Cancelled and Error outcomes. If the windowed path cannot
  start, a native-dialog fallback runs the same model.
- **Source choices, not a file hunt.** The Source step lists the Steam and GOG copies the wizard can
  see, GOG installer files in the download folders, and mounted CDs, and offers **Choose a game folder**,
  **Choose a GOG installer file** and **Search again**. An installed copy or chosen folder that lacks a
  required GOB is marked `(incomplete)` and names the missing files; discs are never marked incomplete,
  and a disc 1 install continues with the Disc 2 step.
- **Licence only where one comes with the source.** A GOG installer shows the EULA extracted from it; a
  CD shows `install/license.txt`. Installed copies and chosen folders skip the step.
- **One thread.** The wizard, the install engine and the innoextract child process run on the UI thread.
  The window loop pumps events and redraws from the progress callbacks; there are no worker threads and
  no locks. Cancel sets a flag the engine polls between copy blocks and the process driver polls between
  output lines.
- **Copy always.** Every source is copied into `<chosen base>/GameData`, never used in place. The
  source is only read and stays untouched. One copy gives the game a single folder to search and check,
  beside its settings, and does not depend on a store library that may move or be removed.
- **The name is `GameData`, not `Data`.** The game already ships its own `Data/` next to the executable
  for fonts and prompt assets ([ADR-0031](ADR-0031-babylon-font-and-image-modules.md)). On Windows and
  other case-insensitive file systems a game-data folder named `Data` would collide with it.
- **The chosen base is the user directory or the executable's folder.** The user directory is the
  recommended choice. **Next to the game** is offered only when the executable's folder is writable. A
  complete `GameData` next to the executable is the data marker that makes that folder the user
  directory; this replaces `portable.txt`, with no separate marker file to create.
- **GOG installers are unpacked with innoextract.** The Linux AppImage bundles it as
  `usr/bin/innoextract`; the wizard looks beside the executable first, then in `PATH`. The Windows zip
  does not bundle it, so Windows players use an installed copy, a CD, a plain folder, or their own
  innoextract in `PATH`.
- **The wizard checks what it installed.** After copying it runs the same data table and cache as
  startup and writes `datacheck.txt` in the user directory, so the first game start does not hash the
  data again.
- **The hand-made workflows go.** `--install-data`, `portable.txt`, the folder prompt and the store
  search at startup are removed. `--data`, `--hash-data` and `--user-dir` remain developer options. The
  startup search is `--data`, `installPath` in `Jones.cfg`, `<user dir>/GameData`, the executable's
  folder, `<executable folder>/GameData`, then the working directory.

## Consequences

- One player flow covers Steam, GOG installed copies, GOG installer files, both CDs and any folder. No
  terminal and no hand-made files are needed for a first start.
- Copying always uses disk space for a second copy when the player keeps the store install. The
  original is untouched and can be removed by the player.
- The setup can be run again only by removing or moving `GameData`; there is no menu entry yet.
- Source discovery moves into the wizard. Store layout changes now need changes in
  `JonesInstall_FindSources` instead of the startup search.
- The AppImage carries innoextract and its third-party licences. Windows players need their own
  innoextract for a GOG installer file.
- Headless runs have no wizard and still need `--data` or an existing `GameData`; a windowed run without a
  usable window or GPU uses the native-dialog fallback.
