// Added: the first Jones3D skin for babylon::ui (ADR-0026): the HUD's warm menu colors, the menu
// font and metrics that fit at 480 canvas units high. Sound cue ids stay 0 until the M7 audio path.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUISKIN_H
#define JONES3D_PLATFORM_BABYLON_JONESUISKIN_H
#ifndef __cplusplus
#error "JonesUiSkin.h is C++ only: it returns a babylon::ui type."
#endif

#include <babylon/ui/Painter.h>
#include <babylon/ui/Skin.h>

// The HUD's look for every screen. menuFont is a font id registered with the JonesUiPainter and is
// used for the title and the rows. The returned skin owns no resources.
babylon::ui::Skin JonesUiSkin_Create(babylon::ui::FontId menuFont);

#endif // JONES3D_PLATFORM_BABYLON_JONESUISKIN_H
