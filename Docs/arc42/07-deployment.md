# 7. Deployment view

## 7.1 Builds

| Build | Platform | Status | Product |
|---|---|---|---|
| Headless Null | Null platform, non-Windows, 32-bit or 64-bit | Works | `Jones3D-headless` |
| Headless Babylon | Babylon files, clock and input; Linux, macOS, 64-bit Windows | Works; the 64-bit Windows program runs under wine in development | `Jones3D-headless` (`Jones3D-headless.exe` on Windows) |
| Windowed Babylon | Babylon on SDL3 and SDL3 GPU, x86-64; Linux, macOS, 64-bit Windows | Plays the game with sound, saving and the in-game menus | `Jones3D` (`Jones3D.exe` on Windows) |

The Win32 and DirectX 6/9 builds were removed in M6.3 ([ADR-0011](09-decisions/ADR-0011-retire-win32-directx.md));
32-bit Windows is a configure error. CI builds and tests every Babylon build on its own operating system from
M5 on.

## 7.2 What a player installs

```mermaid
flowchart LR
    subgraph install [Player's machine]
        exe[Jones3D EX<br/>executable and libraries]
        data[(Player's copy<br/>GOG, Steam or CD)]
        user[(User directory<br/>GameData, Jones.cfg, savegames, logs)]
        mods[(Mod folders)]
    end
    exe --> data
    exe <--> user
    exe --> mods
```

| Part | Comes from | Where |
|---|---|---|
| Executable and its libraries (SDL3; miniaudio, stb and jansson are compiled in) | Jones3D EX release package: the Linux AppImage or the Windows zip | Anywhere. The AppImage is one file; the zip is extracted into the folder the player chooses |
| Original game data | The player's copy of the game (GOG, Steam or CD), never the release package | The setup wizard copies it into `<user dir>/GameData`, or `<executable folder>/GameData` for **Next to the game**, from an installed Steam or GOG copy, a GOG installer file, a CD or a chosen folder |
| Settings, savegames, screenshots, logs | Written by Jones3D EX | `~/.local/share/Jones3DEX` on Linux, `%APPDATA%\Jones3DEX` on Windows; beside the executable when its `GameData` folder is complete |
| Mods | The player | Planned: a mods directory beside the user data |

On first start the windowed game checks for complete game data before it creates its own window. When it
finds none, it opens the **Jones3D EX Setup** wizard, which lists the sources it can see (installed Steam
and GOG copies, GOG installer files in the download folders, mounted CDs), offers a folder or GOG
installer picker and **Search again**, shows the source's licence when it has one, and copies the files
into `GameData` with progress and cancel. A CD install that lacks `CD2.GOB` asks for disc 2. The wizard
checks the copied data, writes `datacheck.txt` in the user directory, and offers **Start the game**.
Sources are only read. Headless runs do not open the wizard; without `--data` or an existing complete
`GameData` they report the missing files and exit nonzero. The player-facing steps are in
[Docs/Install.md](../Install.md); the reasons for the package and data layout are in
[ADR-0032](09-decisions/ADR-0032-packages-and-game-data-location.md) and
[ADR-0034](09-decisions/ADR-0034-setup-wizard.md).

## 7.3 Packages

The [release workflow](../../.github/workflows/release.yml) builds both artifacts from the same game
component, runs the unit suites in Release on Linux and Windows, and uploads the artifacts to the
workflow run. It does not create a release or publish anything.

| Artifact | Contents | Built by | Runs on |
|---|---|---|---|
| `Jones3D_EX-<version>-x86_64.AppImage` | The windowed program, `Data/`, a static `innoextract`, `licenses/`, `README.md` and `CHANGELOG.md`, in an AppDir | `packaging/linux/make-appimage.sh` in the `linux-appimage` job on Ubuntu 24.04 | x86-64 Linux with glibc 2.39 or newer and a Vulkan driver |
| `Jones3D_EX-<version>-windows-x64.zip` | `Jones3D.exe`, `Data/`, `licenses/`, `README.md` and `CHANGELOG.md` at the archive root | CPack's ZIP generator in the `windows-zip` job on `windows-2025-vs2026` | 64-bit Windows 10 or newer |

The AppImage also carries innoextract 1.9 for the setup wizard, which unpacks the GOG installer. It is a
separate program, not linked into the game, and its licenses (innoextract's zlib, Boost's BSL-1.0, liblzma's
0BSD, zlib's zlib and bzip2's BSD-style terms) are under `usr/share/licenses/jones3d-ex/innoextract/`. The
wizard looks for it beside the game executable first, then in `PATH`. The Windows zip does not bundle
innoextract, so its players use an installed copy, a CD, a plain folder, or their own copy in `PATH`.

`cmake --install --component game` produces the tree both start from. The headless runner is a tools
component and is not packaged. On Linux the same CPack setup can also make a
`Jones3D_EX-<version>-linux-x86_64.tar.gz` for local checks; it is not a release artifact. No package
contains game data: players use their own copy.
