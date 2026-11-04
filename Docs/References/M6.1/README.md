# M6.1 reference screenshots

Screenshots of the DX9 renderer, taken while the M6.1 reference captures were recorded (2026-11-04, build
`acd4873c`). The MinGW DX9 build ran under wine on Xvfb at 640x480 with Mesa's software OpenGL, so the images show
wine's DX9-to-OpenGL rendering: close to, but not pixel-identical with, DX9 on a Windows driver
([ADR-0011](../../arc42/09-decisions/ADR-0011-retire-win32-directx.md)). The new renderer is confirmed visually
against them. The render captures themselves contain game data and are kept in a private repository.

The file names give the seconds since the game started.

| Folder | Scene |
|---|---|
| `intro/` | Intro movie (LucasArts logo, newspapers, map), title, Canyonlands level start |
| `canyon/` | Canyonlands from the savegame after the opening: HUD (`s32`, `s36`, `s52`, `s57`), item menu with the item bar (`s43`, `s46`) |
| `indoor/` | Second intro movie, then the Palawan Volcano interior with the HUD |

The images are from *Indiana Jones and the Infernal Machine* and remain the property of their owners.
