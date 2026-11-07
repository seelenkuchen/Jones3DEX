// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

namespace babylon::ui
{

    // A point in canvas units. Canvas units are independent of window
    // pixels: the canvas is always 480 units high and 480 * aspect units
    // wide. The origin is the top left corner and y grows downwards.
    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;

        bool operator==(const Point&) const = default;
    };

    // A width and a height in canvas units. Never negative for a size the
    // module computes; callers may pass an empty size (0, 0).
    struct Size
    {
        float width = 0.0f;
        float height = 0.0f;

        bool operator==(const Size&) const = default;
    };

    // An axis-aligned rectangle in canvas units: x and y name the top left
    // corner, width and height extend right and down. A rectangle contains
    // the half-open region x <= point.x < x + width and y <= point.y <
    // y + height, so neighbouring rectangles share an edge without both
    // containing it.
    struct Rect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        // True when point lies inside the half-open region described above.
        // A rectangle with a non-positive width or height contains nothing.
        constexpr bool contains(Point point) const
        {
            return width > 0.0f && height > 0.0f
                && point.x >= x && point.x < x + width
                && point.y >= y && point.y < y + height;
        }

        bool operator==(const Rect&) const = default;
    };

    // A straight-alpha color, each component in 0..1. The painter expects
    // components in that range; values outside it are the caller's error.
    struct Color
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        bool operator==(const Color&) const = default;
    };

}
