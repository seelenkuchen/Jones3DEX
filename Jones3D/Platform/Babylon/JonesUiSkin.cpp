// Added: the first Jones3D skin for babylon::ui (ADR-0026); see JonesUiSkin.h. The colors are the
// HUD's menu palette: a dark panel, a gold border, a warm yellow title and the red selection bar of
// the item menu. Layout metrics keep a row at 24 units and the title at 32 at a 480-unit canvas.
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

babylon::ui::Skin JonesUiSkin_Create(babylon::ui::FontId menuFont)
{
    babylon::ui::Skin skin;

    skin.panel        = babylon::ui::Color{ 0.05f, 0.06f, 0.09f, 1.0f };
    skin.panelImage   = 0;
    skin.panelBorder  = babylon::ui::Color{ 0.72f, 0.62f, 0.30f, 1.0f };
    skin.borderWidth  = 2.0f;

    skin.title        = babylon::ui::Color{ 1.0f, 0.85f, 0.15f, 1.0f };
    skin.titleFont    = menuFont;

    skin.text         = babylon::ui::Color{ 0.93f, 0.93f, 0.88f, 1.0f };
    skin.textFocused  = babylon::ui::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    skin.textDisabled = babylon::ui::Color{ 0.45f, 0.45f, 0.42f, 1.0f };
    skin.textFont     = menuFont;

    skin.highlight           = babylon::ui::Color{ 0.42f, 0.12f, 0.06f, 1.0f };
    skin.highlightUnfocused  = babylon::ui::Color{ 0.18f, 0.20f, 0.30f, 1.0f };
    skin.dim                 = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.55f };

    skin.rowHeight    = 24.0f;
    skin.titleHeight  = 32.0f;
    skin.rowSpacing   = 2.0f;
    skin.panelPadding = 12.0f;
    skin.minPanelWidth = 240.0f;
    skin.canvasMargin = 16.0f;

    skin.focusSound  = 0;
    skin.acceptSound = 0;
    skin.backSound   = 0;
    return skin;
}
