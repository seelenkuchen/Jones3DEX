// Added: the first Jones3D skin for babylon::ui (ADR-0026); see JonesUiSkin.h. The colors are the
// HUD's menu palette: a dark panel, a gold border, a warm yellow title and the red selection bar of
// the item menu. The row and title heights follow the registered font's line height so every label
// fits its row.
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

namespace
{
    constexpr float kDefaultRowHeight   = 24.0f;
    constexpr float kDefaultTitleHeight = 32.0f;
    constexpr float kRowPadding         = 6.0f;
    constexpr float kTitlePadding       = 14.0f;
}

babylon::ui::Skin JonesUiSkin_Create(babylon::ui::FontId menuFont,
    const babylon::ui::Painter* pPainter)
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

    skin.rowSpacing   = 2.0f;
    skin.panelPadding = 12.0f;
    skin.minPanelWidth = 240.0f;
    skin.canvasMargin = 16.0f;

    // The font's line height at the point size the painter draws it, so a row always fits its
    // label. Without a font the defaults keep the old layout; rebuilding the skin after the font
    // is registered picks up its metrics.
    float lineHeight = 0.0f;
    if ( pPainter && menuFont != 0 )
    {
        lineHeight = pPainter->measureText(menuFont, "").height;
    }

    if ( lineHeight > 0.0f )
    {
        skin.rowHeight   = lineHeight + kRowPadding;
        skin.titleHeight = lineHeight + kTitlePadding;
    }
    else
    {
        skin.rowHeight   = kDefaultRowHeight;
        skin.titleHeight = kDefaultTitleHeight;
    }

    // Added: the HUD item menu's own cues (JonesUiSkin.h).
    skin.focusSound  = kJonesUiSoundCueFocus;
    skin.acceptSound = kJonesUiSoundCueAccept;
    skin.backSound   = kJonesUiSoundCueBack;
    return skin;
}
