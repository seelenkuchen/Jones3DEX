// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/ui/Types.h>

#include <cstdint>
#include <string_view>

namespace babylon::ui
{

    // An opaque reference to a drawable image the game owns, such as a
    // panel background, an icon or a save thumbnail. 0 means "no image" and
    // is never passed to a painter by the module. The module never loads,
    // owns or frees images; the game keeps them alive for as long as a
    // screen using them can be drawn.
    using ImageId = std::uint32_t;

    // An opaque reference to a font the game owns. 0 means "no font"; the
    // module only passes ids taken from the Skin, so a Skin must hold valid
    // ids. Fonts stay alive for as long as screens using them can be drawn.
    using FontId = std::uint32_t;

    // Horizontal placement of a line of text inside its line box. The text
    // is vertically centered in that box for every alignment.
    enum class TextAlign : std::uint8_t
    {
        Left,
        Center,
        Right
    };

    // What the ui module uses to draw one frame of a screen. Everything is
    // in canvas units (see Point); a render-backed implementation maps them
    // to pixels itself, so the module never knows the window resolution.
    //
    // All calls are issued from the thread that calls Ui::draw(), in draw
    // order. The painter owns no state the caller can observe beyond the
    // clip stack; it is the implementation's job to honor the current
    // transform and clip. Calls never fail: invalid arguments are logged by
    // the implementation and otherwise ignored.
    class Painter
    {
    public:
        virtual ~Painter() = default;

        // Fills the rectangle with color, replacing what is there. The
        // rectangle is clipped to the current clip region.
        virtual void fillRect(const Rect& rect, const Color& color) = 0;

        // Draws image into rect, stretching it to the rectangle's size.
        // image is a non-zero id the game supplied in a ListItem or Skin;
        // the module never passes 0.
        virtual void drawImage(ImageId image, const Rect& rect) = 0;

        // Draws one unwrapped line of UTF-8 text inside the line box rect.
        // align places the text horizontally and every alignment centers it
        // vertically. The text is clipped to rect and to the current clip
        // region; a line wider than rect is truncated at the right edge
        // rather than wrapped. color is straight alpha.
        virtual void drawText(FontId font, std::string_view text, const Rect& rect,
            TextAlign align, const Color& color) = 0;

        // The size the same text would occupy when drawn with drawText():
        // its advance width and one line's height, in canvas units. Used
        // for layout before drawing. Never negative.
        virtual Size measureText(FontId font, std::string_view text) const = 0;

        // Intersects the current clip region with rect and remembers the
        // previous region. Regions nest; every pushClip must be matched by
        // exactly one popClip. The module balances its own calls, so an
        // implementation may keep a fixed-depth stack sized for its screens.
        virtual void pushClip(const Rect& rect) = 0;

        // Restores the clip region saved by the matching pushClip. Calling
        // it without a matching push is the caller's error.
        virtual void popClip() = 0;
    };

}
