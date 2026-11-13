// Added: the first Jones3D skin for babylon::ui (ADR-0026): the HUD's warm menu colors, the menu
// font, metrics that fit at 480 canvas units high and the HUD item menu's own sound cues.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUISKIN_H
#define JONES3D_PLATFORM_BABYLON_JONESUISKIN_H
#ifndef __cplusplus
#error "JonesUiSkin.h is C++ only: it returns a babylon::ui type."
#endif

#include <babylon/ui/Painter.h>
#include <babylon/ui/Skin.h>

#include <cstdint>

// Added: the HUD item menu's own sound cues for the Babylon menus.
// The values are indices of JonesHud_aSoundFxIdxs (JonesHudConstants.h): slot 2 is inv_move.wav,
// which the HUD plays when the selected item moves, and slot 3 is inv_expand.wav, which it plays
// when the menu opens, a system item is accepted and the menu closes.
// JonesHud_PlayMenuSoundCue() turns these into the loaded handles and plays them.
inline constexpr std::uint32_t kJonesUiSoundCueFocus  = 2;
inline constexpr std::uint32_t kJonesUiSoundCueAccept = 3;
inline constexpr std::uint32_t kJonesUiSoundCueBack   = 3;

// The context's default look for every screen that sets no skin of its own (ADR-0028): the same
// centered, boxless table as JonesUiSkin_CreateEndScreen. titleFont is the Cinzel id, rowFont the
// Jost id and promptFont, when non-zero, labels the prompt bar; a missing font id falls back to
// the caller's rdFont. The row and title heights derive from the fonts' line heights at the point
// size the painter draws them, so rows always fit their text; with no font or painter the defaults
// (24 and 32) are kept. Build the skin after the fonts are registered, or rebuild it when a font
// changes, so the metrics follow the fonts. The returned skin owns no resources.
babylon::ui::Skin JonesUiSkin_Create(babylon::ui::FontId titleFont,
    babylon::ui::FontId rowFont, const babylon::ui::Painter* pPainter,
    babylon::ui::FontId promptFont = 0);

// Added: the main menu's skin (ADR-0028): an uppercase list in the lower left, no panel or
// highlight boxes, a bullet before the focused row and a warm light grey for the rest. menuFont
// is the Cinzel TrueType id when it loaded and the shared rdFont id when it did not; the caller
// chooses. The row metrics follow the font's line height, like JonesUiSkin_Create.
babylon::ui::Skin JonesUiSkin_CreateMainMenu(babylon::ui::FontId menuFont,
    const babylon::ui::Painter* pPainter,
    babylon::ui::FontId promptFont = 0);

// Added: the shared skin of the Statistics, Level Completed and Game Over screens (ADR-0028): a
// centered, boxless table with a soft dark backdrop, a Cinzel title and Jost rows. titleFont and
// rowFont are the registered TrueType ids when they loaded and the shared rdFont id otherwise; the
// caller chooses. promptFont, when non-zero, labels the prompt bar. The title and row metrics
// follow each font's line height, like the other skins. The context's default skin is this one.
babylon::ui::Skin JonesUiSkin_CreateEndScreen(babylon::ui::FontId titleFont,
    babylon::ui::FontId rowFont, const babylon::ui::Painter* pPainter,
    babylon::ui::FontId promptFont = 0);

// Added: the soft quadratic shade that runs from the lower-left corner over a background. Many
// small cells make the ramp look continuous; the alpha falls to 0 at the right and top ends, so no
// rectangle edge shows. The main menu draws it over its wallpaper and the pause menu over the
// dimmed game frame.
void JonesUiSkin_DrawBottomLeftShade(babylon::ui::Painter& painter,
    const babylon::ui::Size& canvas);

// Added: the tabbed Options screen's skin (ADR-0028): Cinzel tabs and title, Jost rows and help,
// no panel or highlight boxes, and a darkened background behind it. helpFont defaults to rowFont
// when 0. The metrics follow the row font's line height, like the other skins.
babylon::ui::Skin JonesUiSkin_CreateOptions(babylon::ui::FontId tabFont,
    babylon::ui::FontId rowFont, babylon::ui::FontId helpFont,
    const babylon::ui::Painter* pPainter);

#endif // JONES3D_PLATFORM_BABYLON_JONESUISKIN_H
