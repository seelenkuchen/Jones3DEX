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

babylon::ui::Skin JonesUiSkin_CreateMainMenu(babylon::ui::FontId menuFont,
    const babylon::ui::Painter* pPainter)
{
    babylon::ui::Skin skin;

    // No box behind the main menu: the wallpaper shows through. The panel still lays the rows out,
    // but a transparent panel and a zero-width border draw nothing.
    skin.placement   = babylon::ui::PanelPlacement::BottomLeft;
    skin.panel       = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.panelImage  = 0;
    skin.panelBorder = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.borderWidth = 0.0f;

    skin.title       = babylon::ui::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    skin.titleFont   = menuFont;

    // Warm off-white at 90% alpha; the focused row is white and a disabled row fades to 45%. A
    // black shadow keeps the light labels readable on the light load-screen map.
    skin.text             = babylon::ui::Color{ 0.92f, 0.88f, 0.80f, 0.90f };
    skin.textFocused      = babylon::ui::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    skin.textDisabled     = babylon::ui::Color{ 0.92f, 0.88f, 0.80f, 0.45f };
    skin.textShadow       = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.85f };
    skin.textShadowOffset = 1.5f;
    skin.textFont         = menuFont;
    skin.rowAlign         = babylon::ui::TextAlign::Left;

    skin.highlight          = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlightUnfocused = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.dim                = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.55f };

    skin.focusMarker   = "\xE2\x80\xA2"; // U+2022 BULLET
    skin.markerSpacing = 8.0f;

    skin.rowSpacing    = 4.0f;
    skin.panelPadding  = 0.0f;
    skin.minPanelWidth = 0.0f;
    skin.canvasMargin  = 16.0f;

    float lineHeight = 0.0f;
    if ( pPainter && menuFont != 0 )
    {
        lineHeight = pPainter->measureText(menuFont, "").height;
    }

    if ( lineHeight > 0.0f )
    {
        skin.rowHeight   = lineHeight + 8.0f;
        skin.titleHeight = 0.0f;
    }
    else
    {
        skin.rowHeight   = 32.0f;
        skin.titleHeight = 0.0f;
    }

    skin.focusSound  = kJonesUiSoundCueFocus;
    skin.acceptSound = kJonesUiSoundCueAccept;
    skin.backSound   = kJonesUiSoundCueBack;
    return skin;
}

babylon::ui::Skin JonesUiSkin_CreateOptions(babylon::ui::FontId tabFont,
    babylon::ui::FontId rowFont, babylon::ui::FontId helpFont,
    const babylon::ui::Painter* pPainter)
{
    babylon::ui::Skin skin;

    // No box behind the tabbed screen: the darkened menu image shows through. The panel still
    // lays the tabs, rows and help column out.
    skin.placement   = babylon::ui::PanelPlacement::Center;
    skin.panel       = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.panelImage  = 0;
    skin.panelBorder = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.borderWidth = 0.0f;

    skin.title       = babylon::ui::Color{ 0.92f, 0.88f, 0.80f, 1.0f };
    skin.titleFont   = tabFont;
    skin.textFont    = rowFont;
    skin.helpFont    = helpFont != 0 ? helpFont : rowFont;
    skin.text        = babylon::ui::Color{ 0.92f, 0.88f, 0.80f, 0.90f };
    skin.textFocused = babylon::ui::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    skin.textDisabled = babylon::ui::Color{ 0.92f, 0.88f, 0.80f, 0.45f };
    skin.textShadow  = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.85f };
    skin.textShadowOffset = 1.5f;
    skin.rowAlign    = babylon::ui::TextAlign::Left;

    skin.highlight          = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlightUnfocused = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.dim                = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.55f };

    skin.helpColumnWidth = 0.35f;
    skin.rowSpacing      = 4.0f;
    skin.panelPadding    = 12.0f;
    skin.minPanelWidth   = 320.0f;
    skin.canvasMargin    = 16.0f;

    float rowLineHeight = 0.0f;
    if ( pPainter && rowFont != 0 )
    {
        rowLineHeight = pPainter->measureText(rowFont, "").height;
    }

    float tabLineHeight = 0.0f;
    if ( pPainter && tabFont != 0 )
    {
        tabLineHeight = pPainter->measureText(tabFont, "").height;
    }

    skin.rowHeight   = rowLineHeight > 0.0f ? rowLineHeight + 8.0f : 24.0f;
    skin.titleHeight = tabLineHeight > 0.0f ? tabLineHeight + 8.0f : 32.0f;

    skin.focusSound  = kJonesUiSoundCueFocus;
    skin.acceptSound = kJonesUiSoundCueAccept;
    skin.backSound   = kJonesUiSoundCueBack;
    return skin;
}
