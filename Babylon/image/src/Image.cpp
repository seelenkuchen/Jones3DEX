// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/image/Image.h>

#include <stb_image.h>

#include <limits>

namespace babylon::image
{

namespace
{

    constexpr int kMaxDimension = 8192;

}

std::optional<Image> decodePng(std::span<const std::byte> bytes)
{
    if ( bytes.empty()
        || bytes.size() > (std::size_t)std::numeric_limits<int>::max() )
    {
        return std::nullopt;
    }

    const stbi_uc* pInput = reinterpret_cast<const stbi_uc*>(bytes.data());
    const int length      = (int)bytes.size();

    int width = 0;
    int height = 0;
    int channels = 0;
    if ( !stbi_info_from_memory(pInput, length, &width, &height, &channels) )
    {
        return std::nullopt;
    }
    if ( width <= 0 || height <= 0 || width > kMaxDimension || height > kMaxDimension )
    {
        return std::nullopt;
    }

    int decodedChannels = 0;
    stbi_uc* pPixels = stbi_load_from_memory(pInput, length, &width, &height,
        &decodedChannels, 4);
    if ( !pPixels )
    {
        return std::nullopt;
    }

    const std::size_t texels = (std::size_t)width * (std::size_t)height;
    if ( texels > std::numeric_limits<std::size_t>::max() / 4u )
    {
        stbi_image_free(pPixels);
        return std::nullopt;
    }

    Image image;
    image.width  = (std::uint32_t)width;
    image.height = (std::uint32_t)height;
    image.pixels.assign(pPixels, pPixels + texels * 4u);
    stbi_image_free(pPixels);
    return image;
}

}
