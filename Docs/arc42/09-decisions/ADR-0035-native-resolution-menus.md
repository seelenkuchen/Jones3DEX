# ADR-0035: Menus render at the window's native resolution

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

The setup wizard and the in-game menus looked pixelated on large displays. The wizard baked its fonts at
canvas units and stretched them to its 960x640 window. The in-game menus drew through `JonesUiPainter` into
the game's back buffer at the game's render resolution (640x480 by default), and the present pass then
scaled that whole frame to the swapchain. Fonts were sharp for the back buffer, but the final upscale made
the menus soft whenever the window or display was larger than the game resolution.

The setup wizard already used `RenderPainter` (ADR-0033), which can draw a `babylon::ui` screen on a
`render::Device` without the game renderer. ADR-0033 kept the game menus on the rdroid painter and said the
two painters did not replace each other. That split no longer serves the owner requirement that menus are
divorced from the in-game render and always sharp.

## Decision

1. **Fonts bake at the target's pixel scale.** `RenderPainter::addFont(font, canvasHeight)` bakes at
   `canvasHeight * pixelsPerCanvasUnit`, and `setPixelScale()`/`begin()` re-bake when the window size
   changes. `measureText()` stays in canvas units. The painter also owns the ui's Choice-arrow and
   scroll-marker fallbacks and snaps glyph quads to whole target pixels when the atlas and target scales
   match.
2. **Menus draw on the swapchain after the game frame.** `stdRenderBabylon` gains a present-overlay callback.
   A presenting submit records the post-processing chain, the present pass and then the overlay pass into
   the same command list, and submits once. The overlay draws the menu background image, the dim layer and
   the `babylon::ui` screen at the window's drawable size. The menu is no longer part of the game frame and
   is not post-processed.
3. **The modal loop presents, the overlay draws.** The blocking `JonesUiModal_Run` loop keeps its event,
   sound and return-value contract but no longer captures a frozen frame or calls `Ui::draw()`. It draws a
   game-renderer background (`background.draw`, the main-menu wallpaper) before `stdDisplay_Update()` and
   lets the overlay draw the screen. The live, paused game frame is the background for the other menus.
4. **One context, one font set.** `JonesUiContext` owns a `RenderPainter` and the shared `Ui`, loads Cinzel
   and Jost once from `Data/ui`, and installs the overlay while it is held. The packaged TTFs are required;
   the missing-font rdFont fallback is dropped and `Acquire()` returns null instead, so the wrappers take
   their existing native/Null path. The `.gcf` font remains the HUD's.
5. **Screenshots keep the menu.** While the overlay is installed, `stdDisplay_SaveScreen()` renders the
   presented frame plus the overlay through `stdRenderBabylon_RenderPresentToTexture()` into a window-sized
   offscreen target and writes that BMP. Gameplay screenshots keep the old back-buffer path and size.
6. **No new menu for the HUD.** Health, item icons and prompts stay in the game frame. The inventory HUD
   menu is unchanged.

## Consequences

- Menus are sharp at the window's real pixel size on every display and at every game render resolution, and
  the layout uses the window's aspect instead of the back buffer's.
- `JonesUiPainter` is removed; `RenderPainter` and `JonesUiAssets` serve both the setup wizard and the game
  menus. ADR-0033's decision that the two painters do not replace each other is superseded.
- The menu look, skins, fonts, prompts, sounds, action ids, call order and return values stay the same.
  A damaged install without `Data/ui/fonts` has no menu text; message screens fall back to the native box
  where one exists.
- Screenshots of menus are window-sized BMPs; gameplay screenshots keep the back-buffer size.
- Presentation only: the simulation, save format and headless traces are unchanged.
- The overlay pass runs after post-processing, so no post-processing applies to menus.
