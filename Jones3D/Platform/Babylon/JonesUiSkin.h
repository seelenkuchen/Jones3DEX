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

// The HUD's look for every screen. menuFont is a font id registered with the JonesUiPainter and is
// used for the title and the rows. The row and title heights derive from that font's line height
// at the point size the painter draws it, so rows always fit their text; with no font or painter
// the defaults (24 and 32) are kept. Build the skin after the font is registered, or rebuild it
// when the font changes, so the metrics follow the font. The returned skin owns no resources.
babylon::ui::Skin JonesUiSkin_Create(babylon::ui::FontId menuFont,
    const babylon::ui::Painter* pPainter);

// Added: the main menu's skin (ADR-0028): an uppercase list in the lower left, no panel or
// highlight boxes, a bullet before the focused row and a warm light grey for the rest. menuFont
// is the Cinzel TrueType id when it loaded and the shared rdFont id when it did not; the caller
// chooses. The row metrics follow the font's line height, like JonesUiSkin_Create.
babylon::ui::Skin JonesUiSkin_CreateMainMenu(babylon::ui::FontId menuFont,
    const babylon::ui::Painter* pPainter);

// Added: the tabbed Options screen's skin (ADR-0028): Cinzel tabs and title, Jost rows and help,
// no panel or highlight boxes, and a darkened background behind it. helpFont defaults to rowFont
// when 0. The metrics follow the row font's line height, like the other skins.
babylon::ui::Skin JonesUiSkin_CreateOptions(babylon::ui::FontId tabFont,
    babylon::ui::FontId rowFont, babylon::ui::FontId helpFont,
    const babylon::ui::Painter* pPainter);

#endif // JONES3D_PLATFORM_BABYLON_JONESUISKIN_H
