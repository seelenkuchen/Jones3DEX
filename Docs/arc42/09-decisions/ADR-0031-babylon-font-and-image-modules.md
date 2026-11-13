# ADR-0031: Babylon font and image modules for the menu assets

- **Status:** Accepted
- **Date:** 2026-11-13

## Context

ADR-0028 gives the menus two OFL fonts (Cinzel, Jost) and Xelu's CC0 button prompt PNGs. Until now every
picture and glyph the menus drew came from game data (`rdFont` GCF fonts, `rdMaterial` cels), so Babylon has
no way to read a TrueType font or a PNG file. Changing the module allowlist is an architecture change
(ADR-0015).

## Decision

- Two new Babylon modules, both CPU-only (no device, so no Null backend is needed):
  - `font`: loads a TrueType font from bytes and bakes the glyphs a caller asks for into an 8-bit coverage
    atlas with per-glyph metrics (advance, bearing, box) and kerning, at a given pixel height. Depends on
    `core`, `log` and the vendored `stb_truetype` (public domain / MIT).
  - `image`: decodes PNG bytes into RGBA8 pixels. Depends on `core` and the vendored `stb_image`
    (public domain / MIT).
- Reading the files stays with the caller (the `files` module), so neither module touches the file system.
- The adapter (`JonesUiPainter`) uploads atlases and images as textures and draws them; game code never sees
  them.
- Runtime assets live in the repository under `Data/ui/fonts/` and `Data/ui/prompts/<set>/`, each with its
  license text. The build copies `Data/` next to the executable and the install rules ship it. The game finds
  it relative to the executable's directory.

## Consequences

- The first committed runtime assets that are not game data: OFL and CC0 license texts ship with them.
- Fonts are rasterized once per pixel height at startup or on resize; there is no runtime glyph cache yet.
- Retail and simulation are unaffected: presentation only.
