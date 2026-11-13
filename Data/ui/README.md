# UI runtime assets

The menus read the files below at runtime, relative to the executable's
directory (`<basePath>Data/ui/...`). They are not game data and are not
generated from it. Each source has its license text beside it. The build
copies this tree next to the Babylon-column executables and the install rules
ship it next to the installed program.

## Fonts

| File | Source | License |
|---|---|---|
| `fonts/Cinzel[wght].ttf` | Cinzel variable font, The Cinzel Project Authors, <https://github.com/NDISCOVER/Cinzel> | SIL Open Font License 1.1, `fonts/OFL-Cinzel.txt` |
| `fonts/Jost[wght].ttf` | Jost variable font, The Jost Project Authors, <https://github.com/indestructible-type/Jost> | SIL Open Font License 1.1, `fonts/OFL-Jost.txt` |

The font files are the variable (weight-axis) builds from Google Fonts'
repository (<https://github.com/google/fonts>, `ofl/cinzel` and `ofl/jost`,
`main` branch, downloaded 2026-11-13). Their license texts are committed
unchanged.

## Button prompts

`prompts/xbox`, `prompts/ps5`, `prompts/switch`, `prompts/steamdeck` and
`prompts/keyboard` hold the PNGs from Xelu's Free Controller and Key Prompts,
kept under their original file names:

- Source: <https://github.com/DJLink/Xelu_Free_Controller-Key_Prompts>
- Commit: `e71969dc7ac7f35ed7ad47d38b0524d099d4aa61`
- Original pack: <https://thoseawesomeguys.com/prompts/>
- License: CC0 1.0 Universal, public domain, `prompts/LICENSE`

The five directories correspond to the pack's `Xbox Series`, `PS5`, `Switch`,
`Steam Deck` and `Keyboard & Mouse/Dark` folders. Only the PNGs are included.
