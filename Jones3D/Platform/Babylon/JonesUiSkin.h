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
// used for the title and the rows. The returned skin owns no resources.
babylon::ui::Skin JonesUiSkin_Create(babylon::ui::FontId menuFont);

#endif // JONES3D_PLATFORM_BABYLON_JONESUISKIN_H
