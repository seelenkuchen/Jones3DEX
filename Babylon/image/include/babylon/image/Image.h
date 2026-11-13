// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace babylon::image
{

    // A decoded image in straight-alpha RGBA8: four bytes per pixel, rows
    // from the top, no padding. pixels.size() is always width * height * 4.
    struct Image
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> pixels;
    };

    // Decodes a PNG file held in memory. Returns nullopt for bytes that are
    // not a PNG, are truncated or malformed, or name a width or height above
    // 8192. The caller keeps ownership of the input. Decoding a bad file
    // never crashes the process.
    std::optional<Image> decodePng(std::span<const std::byte> bytes);

}
