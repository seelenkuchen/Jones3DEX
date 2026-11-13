// Added: the Jones3D skins for babylon::ui (ADR-0026, ADR-0028); see JonesUiSkin.h. Every screen
// that does not set its own skin gets the centered, boxless table of the end screens: a Cinzel
// title, Jost rows and a translucent dark backdrop. The row and title heights follow the registered
// fonts' line heights so every label fits its row.
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

namespace
{
    constexpr float kDefaultRowHeight   = 24.0f;
    constexpr float kDefaultTitleHeight = 32.0f;
    constexpr float kRowPadding         = 6.0f;
    constexpr float kTitlePadding       = 14.0f;
}

babylon::ui::Skin JonesUiSkin_Create(babylon::ui::FontId titleFont,
    babylon::ui::FontId rowFont, const babylon::ui::Painter* pPainter,
    babylon::ui::FontId promptFont)
{
    // The default is the end screens' table skin, so a screen that sets no skin of its own matches
    // the styled screens (ADR-0028).
    return JonesUiSkin_CreateEndScreen(titleFont, rowFont, pPainter, promptFont);
}

babylon::ui::Skin JonesUiSkin_CreateMainMenu(babylon::ui::FontId menuFont,
    const babylon::ui::Painter* pPainter, babylon::ui::FontId promptFont)
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
    skin.promptFont       = promptFont;
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
    skin.promptFont  = rowFont;
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

babylon::ui::Skin JonesUiSkin_CreateEndScreen(babylon::ui::FontId titleFont,
    babylon::ui::FontId rowFont, const babylon::ui::Painter* pPainter,
    babylon::ui::FontId promptFont)
{
    babylon::ui::Skin skin;

    // A centered table: no border and a soft dark backdrop instead of a box, so the game frame
    // still shows while the rows stay readable over it.
    skin.placement   = babylon::ui::PanelPlacement::Center;
    skin.panel       = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.60f };
    skin.panelImage  = 0;
    skin.panelBorder = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.borderWidth = 0.0f;

    skin.title       = babylon::ui::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    skin.titleFont   = titleFont;

    skin.text             = babylon::ui::Color{ 0.92f, 0.88f, 0.80f, 0.95f };
    skin.textFocused      = babylon::ui::Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    skin.textDisabled     = babylon::ui::Color{ 0.92f, 0.88f, 0.80f, 0.45f };
    skin.textShadow       = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.85f };
    skin.textShadowOffset = 1.5f;
    skin.textFont         = rowFont;
    skin.promptFont       = promptFont;
    skin.rowAlign         = babylon::ui::TextAlign::Left;

    // No highlight box; the bullet marks the focused row. A list that does not have focus keeps a
    // very faint fill so the selected row (the save a Load would open) stays visible.
    skin.highlight          = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.highlightUnfocused = babylon::ui::Color{ 1.0f, 1.0f, 1.0f, 0.12f };
    skin.dim                = babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.55f };

    skin.focusMarker   = "\xE2\x80\xA2"; // U+2022 BULLET
    skin.markerSpacing = 8.0f;

    skin.rowSpacing    = 4.0f;
    skin.panelPadding  = 18.0f;
    skin.minPanelWidth = 260.0f;
    skin.canvasMargin  = 16.0f;

    float titleLineHeight = 0.0f;
    if ( pPainter && titleFont != 0 )
    {
        titleLineHeight = pPainter->measureText(titleFont, "").height;
    }

    float rowLineHeight = 0.0f;
    if ( pPainter && rowFont != 0 )
    {
        rowLineHeight = pPainter->measureText(rowFont, "").height;
    }

    skin.titleHeight = titleLineHeight > 0.0f ? titleLineHeight + kTitlePadding
                                              : kDefaultTitleHeight;
    skin.rowHeight   = rowLineHeight > 0.0f ? rowLineHeight + kRowPadding
                                            : kDefaultRowHeight;

    skin.focusSound  = kJonesUiSoundCueFocus;
    skin.acceptSound = kJonesUiSoundCueAccept;
    skin.backSound   = kJonesUiSoundCueBack;
    return skin;
}

void JonesUiSkin_DrawBottomLeftShade(babylon::ui::Painter& painter,
    const babylon::ui::Size& canvas)
{
    constexpr int kColumns = 32;
    constexpr int kRows    = 24;
    const float width  = canvas.width * 0.45f;
    const float height = canvas.height * 0.55f;
    const float cellWidth  = width / (float)kColumns;
    const float cellHeight = height / (float)kRows;
    for ( int column = 0; column < kColumns; ++column )
    {
        const float u = (float)column / (float)(kColumns - 1);
        const float horizontal = (1.0f - u) * (1.0f - u);
        for ( int row = 0; row < kRows; ++row )
        {
            const float v = (float)row / (float)(kRows - 1);
            const float vertical = (1.0f - v) * (1.0f - v);
            const babylon::ui::Color shade = {
                0.0f, 0.0f, 0.0f, 0.55f * horizontal * vertical };
            painter.fillRect(babylon::ui::Rect{ cellWidth * (float)column,
                canvas.height - cellHeight * (float)(row + 1),
                cellWidth, cellHeight }, shade);
        }
    }
}
