// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/ui/Painter.h>
#include <babylon/ui/Types.h>

#include <cstdint>
#include <string>

namespace babylon::ui
{

    // Where the panel sits on the canvas. Center is the original layout for
    // dialogs; BottomLeft is the main-menu layout, whose rows read as a list
    // stacked in the lower left corner.
    enum class PanelPlacement : std::uint8_t
    {
        Center,
        BottomLeft
    };

    // The look and sound of every screen: colors, fonts, row metrics and
    // sound cue ids. The game supplies one Skin; the ui module copies it
    // and never owns fonts, images or sounds.
    //
    // Lengths are canvas units. Font ids must be valid for the Painter;
    // image ids of 0 mean "no image". Sound cue ids are opaque values the
    // game chose; 0 means "silent". The module reports a cue as a
    // SoundCue event instead of playing it, so the game decides how and
    // whether to play it.
    struct Skin
    {
        // The panel behind the widget list. panelImage, when non-zero, is
        // drawn stretched over panel first and panel is used behind it and
        // as the fill when the image is absent or has transparent areas.
        Color panel = { 0.06f, 0.06f, 0.08f, 0.95f };
        ImageId panelImage = 0;

        // Where the panel sits. Center keeps the dialog layout. BottomLeft
        // puts the panel's left edge at canvasMargin * 4 from the left and
        // its bottom at canvasMargin * 4 from the bottom, with the rows left
        // aligned in it. A panel whose panel color has alpha 0 draws no fill;
        // a borderWidth of 0 draws no border.
        PanelPlacement placement = PanelPlacement::Center;

        // The panel border, drawn as a rectangle outline of borderWidth
        // inside the panel's rectangle. A borderWidth of 0 draws none.
        Color panelBorder = { 0.45f, 0.45f, 0.55f, 1.0f };
        float borderWidth = 2.0f;

        // Screen title, centered in the title row.
        Color title = { 1.0f, 1.0f, 1.0f, 1.0f };
        FontId titleFont = 0;

        // Normal, focused and disabled widget text.
        Color text = { 0.85f, 0.85f, 0.85f, 1.0f };
        Color textFocused = { 1.0f, 1.0f, 1.0f, 1.0f };
        Color textDisabled = { 0.45f, 0.45f, 0.45f, 1.0f };
        FontId textFont = 0;

        // How a row's own text is aligned inside its line box. Left keeps
        // the original dialog look; a bottom-left menu also uses Left.
        TextAlign rowAlign = TextAlign::Left;

        // A shadow drawn behind every row's text before the text itself, so
        // light text stays readable on a light background. An alpha of 0
        // draws none. textShadowOffset moves it down and right in canvas
        // units; 0 places it exactly behind the text.
        Color textShadow = { 0.0f, 0.0f, 0.0f, 0.0f };
        float textShadowOffset = 1.0f;

        // A marker drawn in textFocused immediately left of the focused
        // row's text, such as a bullet for a menu selection. UTF-8; empty
        // draws none. Every row reserves the marker's width plus
        // markerSpacing, so text does not move when focus moves.
        std::string focusMarker;
        float markerSpacing = 8.0f;

        // The fill behind the focused widget, and the fill behind the
        // selected row of a list that does not have focus.
        Color highlight = { 0.18f, 0.30f, 0.55f, 1.0f };
        Color highlightUnfocused = { 0.14f, 0.14f, 0.20f, 1.0f };

        // The dim layer a screen may draw over whatever is behind it. The
        // game decides whether to use it; the module exposes it here so
        // every screen looks the same.
        Color dim = { 0.0f, 0.0f, 0.0f, 0.55f };

        // Vertical metrics of one widget row, the title row and the space
        // between rows, in canvas units. rowHeight must be positive; the
        // module rejects a Skin whose rowHeight or titleHeight is not.
        float rowHeight = 24.0f;
        float titleHeight = 32.0f;
        float rowSpacing = 2.0f;

        // Space between the panel edge and its contents, and the smallest
        // panel width. The panel grows to fit the widest row and is clamped
        // to the canvas minus canvasMargin on each side.
        float panelPadding = 12.0f;
        float minPanelWidth = 240.0f;
        float canvasMargin = 16.0f;

        // Sound cue ids reported as SoundCue events: focus moved to another
        // widget or row, a button or list row was accepted, and a screen
        // was closed with back. 0 reports nothing for that cue.
        std::uint32_t focusSound = 0;
        std::uint32_t acceptSound = 0;
        std::uint32_t backSound = 0;
    };

}
