# ADR-0033: A Babylon UI render painter

- **Status:** Accepted
- **Date:** 2026-11-14

## Context

The setup wizard has to draw menus before the game and its renderer start. The game's painter
(`JonesUiPainter`) draws through the game's rdroid and `std3D` path, so it needs the game frame and its
fonts. The `ui` module itself only depends on the `Painter` interface, and ADR-0026's amendment left a
render-backed painter for the day another caller needed one. Adding a Babylon module changes the module
allowlist, which is an architecture change (ADR-0015).

## Decision

- A new Babylon module `uirender` depends on `ui`, `render`, `font`, `image` and `log`. It contains
  `RenderPainter`, a `babylon::ui::Painter` that records 2D quads on the CPU and submits them as one
  `render::Device` frame.
- One textured, vertex-colored pipeline draws fills, images and glyphs. A quad carries a pixel position, a
  texture coordinate and a straight-alpha color; fills sample a white 1x1 texture, images sample their own
  RGBA texture, and text samples a baked coverage atlas. Quads that share a texture and a clip become one
  draw, and the clip stack becomes the scissor rectangle.
- `FrameTarget` names the swapchain (a null texture) or an offscreen color target, with its format and load
  operation. Canvas units map to target pixels at `begin()`, as the `ui` module expects. The shaders are
  compiled at build time like the render module's test shaders (ADR-0022).
- The module has no backend: it follows the `render::Device` interface, so the Null device records the
  same command stream and the SDL GPU device rasterizes it. No new `Device` feature was needed.
- The game keeps `JonesUiPainter` for its in-game menus, which stay pixel-identical to the HUD. A caller
  without a game frame, such as the setup wizard, uses `RenderPainter` instead. The two do not replace each
  other.

## Consequences

- Babylon can draw a `ui` screen with its own render device before any game renderer exists.
- `uirender` needs the shader toolchain: with `BABYLON_BUILD_SHADERS=OFF` the module is not built, as the
  render module's shader tests already are.
- Fonts are baked once per pixel height for printable ASCII, Latin-1 and U+2022; there is no runtime glyph
  cache or additional script coverage yet.
- The module is presentation only; retail and simulation behavior are unaffected.
