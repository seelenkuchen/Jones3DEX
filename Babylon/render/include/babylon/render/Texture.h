// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>

namespace babylon::render
{

    // Texel formats. Byte layouts are what uploadTexture() takes and
    // readTexture() returns, the same on every backend. The 16-bit formats
    // are one little-endian word per texel with the first-named component in
    // the lowest bits: B5G6R5 has blue in bits 0-4, green in 5-10 and red in
    // 11-15. Not every device can sample or render every format; ask
    // Device::supportsFormat(). Depth formats are only depth targets (and
    // sampled where supported); they are never uploaded, read back or copied.
    enum class TextureFormat : std::uint8_t
    {
        Rgba8,      // bytes R, G, B, A
        Bgra8,      // bytes B, G, R, A
        B5G6R5,     // blue 0-4, green 5-10, red 11-15
        B5G5R5A1,   // blue 0-4, green 5-9, red 10-14, alpha 15
        B4G4R4A4,   // blue 0-3, green 4-7, red 8-11, alpha 12-15
        D24S8,      // 24-bit depth, 8-bit stencil
        D32F,       // 32-bit float depth
    };

    // Every format, in declaration order, for code that walks them all.
    inline constexpr TextureFormat kAllTextureFormats[] = {
        TextureFormat::Rgba8, TextureFormat::Bgra8, TextureFormat::B5G6R5,
        TextureFormat::B5G5R5A1, TextureFormat::B4G4R4A4, TextureFormat::D24S8,
        TextureFormat::D32F,
    };

    // True for D24S8 and D32F.
    constexpr bool isDepthFormat(TextureFormat format)
    {
        return format == TextureFormat::D24S8 || format == TextureFormat::D32F;
    }

    // Bytes per texel in uploadTexture() and readTexture() data: 4 for the
    // 8-bit and depth formats, 2 for the 16-bit ones.
    constexpr std::uint32_t bytesPerTexel(TextureFormat format)
    {
        switch ( format )
        {
            case TextureFormat::B5G6R5:
            case TextureFormat::B5G5R5A1:
            case TextureFormat::B4G4R4A4:
                return 2;
            default:
                return 4;
        }
    }

    // What a texture may be used for, combined with |. A texture is either a
    // color target or a depth target, never both, and its usage must name at
    // least one flag.
    enum class TextureUsage : std::uint8_t
    {
        None = 0,
        Sampled = 1 << 0,       // read by shaders through bindTexture()
        ColorTarget = 1 << 1,   // color attachment of a pass
        DepthTarget = 1 << 2,   // depth attachment of a pass
    };

    constexpr TextureUsage operator|(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    // True when usage contains every flag in flags.
    constexpr bool hasUsage(TextureUsage usage, TextureUsage flags)
    {
        return (static_cast<std::uint8_t>(usage) & static_cast<std::uint8_t>(flags))
            == static_cast<std::uint8_t>(flags);
    }

    // One 2D texture with a full or partial mip chain. Mip 0 is width x
    // height; each further level halves both, rounding down, but never below
    // 1. Device::createTexture() refuses a size of 0, a dimension above
    // DeviceInfo::limits.maxTextureDimension, mipLevels of 0 or above
    // maxMipLevels(), and a format the device doesn't support for the usage.
    struct TextureDesc
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t mipLevels = 1;
        TextureFormat format = TextureFormat::Rgba8;
        TextureUsage usage = TextureUsage::Sampled;

        bool operator==(const TextureDesc&) const = default;
    };

    // A rectangle of one mip level, in texels of that level.
    struct TextureRegion
    {
        std::uint32_t mip = 0;
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool operator==(const TextureRegion&) const = default;
    };

    // The number of levels in a full mip chain of a width x height texture:
    // floor(log2(max(width, height))) + 1, and 0 if either is 0.
    constexpr std::uint32_t maxMipLevels(std::uint32_t width, std::uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return 0;
        }
        std::uint32_t largest = width > height ? width : height;
        std::uint32_t levels = 1;
        while ( largest > 1 )
        {
            largest >>= 1;
            ++levels;
        }
        return levels;
    }

    // The width or height of one mip level of a dimension: max(1, size >> mip).
    constexpr std::uint32_t mipDimension(std::uint32_t size, std::uint32_t mip)
    {
        const std::uint32_t shifted = mip < 32 ? (size >> mip) : 0;
        return shifted == 0 ? 1 : shifted;
    }

    // The whole of one mip level of a texture as a region.
    constexpr TextureRegion wholeMip(const TextureDesc& desc, std::uint32_t mip)
    {
        return TextureRegion{ mip, 0, 0, mipDimension(desc.width, mip),
            mipDimension(desc.height, mip) };
    }

}
