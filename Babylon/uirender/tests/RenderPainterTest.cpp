// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/uirender/RenderPainter.h>

#include <babylon/font/Font.h>
#include <babylon/image/Image.h>
#include <babylon/log/CaptureSink.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <utility>
#include <vector>

#ifndef BABYLON_UIRENDER_TEST_FONT
#error "BABYLON_UIRENDER_TEST_FONT must name the committed Jost font"
#endif

namespace
{
    using babylon::log::CaptureSink;
    using namespace babylon::uirender;
    using babylon::render::BeginPass;
    using babylon::render::BindTexture;
    using babylon::render::Draw;
    using babylon::render::EndPass;
    using babylon::render::FrameSubmitted;
    using babylon::render::NullDevice;
    using babylon::render::PipelineCreated;
    using babylon::render::RecordedCommand;
    using babylon::render::ScissorRect;
    using babylon::render::SetScissor;
    using babylon::render::TextureHandle;

    std::string joined(const CaptureSink& sink)
    {
        std::string text;
        for ( const auto& record : sink.records() )
        {
            text += text.empty() ? "" : "; ";
            text += record.message;
        }
        return text;
    }

    std::vector<std::byte> readFont()
    {
        std::FILE* pFile = nullptr;
#ifdef _MSC_VER
        REQUIRE(fopen_s(&pFile, BABYLON_UIRENDER_TEST_FONT, "rb") == 0);
#else
        pFile = std::fopen(BABYLON_UIRENDER_TEST_FONT, "rb");
#endif
        REQUIRE(pFile);
        REQUIRE(std::fseek(pFile, 0, SEEK_END) == 0);
        const long size = std::ftell(pFile);
        REQUIRE(size >= 0);
        REQUIRE(std::fseek(pFile, 0, SEEK_SET) == 0);

        std::vector<std::byte> data(static_cast<std::size_t>(size));
        if ( !data.empty() )
        {
            REQUIRE(std::fread(data.data(), 1, data.size(), pFile) == data.size());
        }
        std::fclose(pFile);
        return data;
    }

    std::optional<babylon::font::Font> loadFont()
    {
        return babylon::font::Font::load(readFont());
    }

    std::vector<char32_t> bakedCodepoints()
    {
        std::vector<char32_t> codepoints;
        for ( char32_t codepoint = 0x20; codepoint <= 0x7E; ++codepoint )
        {
            codepoints.push_back(codepoint);
        }
        for ( char32_t codepoint = 0xA0; codepoint <= 0xFF; ++codepoint )
        {
            codepoints.push_back(codepoint);
        }
        codepoints.push_back(0x2022);
        return codepoints;
    }

    template <typename T>
    std::size_t countOf(const std::vector<RecordedCommand>& record)
    {
        std::size_t count = 0;
        for ( const RecordedCommand& command : record )
        {
            if ( std::holds_alternative<T>(command) )
            {
                ++count;
            }
        }
        return count;
    }

    template <typename T>
    const T* firstOf(const std::vector<RecordedCommand>& record)
    {
        for ( const RecordedCommand& command : record )
        {
            if ( const T* pCommand = std::get_if<T>(&command) )
            {
                return pCommand;
            }
        }
        return nullptr;
    }

    std::vector<TextureHandle> boundTextures(const std::vector<RecordedCommand>& record)
    {
        std::vector<TextureHandle> textures;
        for ( const RecordedCommand& command : record )
        {
            const BindTexture* pBind = std::get_if<BindTexture>(&command);
            if ( pBind == nullptr )
            {
                continue;
            }
            if ( std::find(textures.begin(), textures.end(), pBind->texture) == textures.end() )
            {
                textures.push_back(pBind->texture);
            }
        }
        return textures;
    }

    const babylon::ui::Color kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };
    const babylon::ui::Color kRed   = { 1.0f, 0.0f, 0.0f, 1.0f };
}

TEST_CASE("Quads of one texture and clip are batched into one draw")
{
    NullDevice device;
    RenderPainter painter(device);

    REQUIRE(painter.begin(480, 480));
    for ( std::uint32_t index = 0; index < 5; ++index )
    {
        const float offset = static_cast<float>(index) * 10.0f;
        painter.fillRect(babylon::ui::Rect{ offset, 10.0f, 8.0f, 8.0f },
            babylon::ui::Color{ offset / 50.0f, 0.0f, 0.0f, 1.0f });
    }
    REQUIRE(painter.end());

    const std::vector<RecordedCommand>& record = device.recorded();
    CHECK(countOf<Draw>(record) == 1);
    CHECK(countOf<PipelineCreated>(record) == 1);
    CHECK(countOf<BeginPass>(record) == 1);
    CHECK(countOf<EndPass>(record) == 1);
    CHECK(countOf<FrameSubmitted>(record) == 1);

    const Draw* pDraw = firstOf<Draw>(record);
    REQUIRE(pDraw != nullptr);
    CHECK(pDraw->firstVertex == 0);
    CHECK(pDraw->vertexCount == 5 * 6);
}

TEST_CASE("The clip stack becomes the scissor rectangle")
{
    NullDevice device;
    RenderPainter painter(device);

    REQUIRE(painter.begin(480, 480));
    painter.pushClip(babylon::ui::Rect{ 10.0f, 10.0f, 100.0f, 100.0f });
    painter.pushClip(babylon::ui::Rect{ 50.0f, 50.0f, 100.0f, 100.0f });
    painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, 480.0f, 480.0f }, kRed);
    painter.popClip();
    painter.popClip();
    REQUIRE(painter.end());

    const std::vector<RecordedCommand>& record = device.recorded();
    const SetScissor* pScissor = firstOf<SetScissor>(record);
    REQUIRE(pScissor != nullptr);
    CHECK(pScissor->rect == ScissorRect{ 50, 50, 60, 60 });
    CHECK(countOf<SetScissor>(record) == 1);
    CHECK(countOf<Draw>(record) == 1);
}

TEST_CASE("A baked font is created once and measures as it draws")
{
    const std::optional<babylon::font::Font> font = loadFont();
    REQUIRE(font.has_value());

    NullDevice device;
    RenderPainter painter(device);

    const babylon::ui::FontId id = painter.addFont(*font, 32.0f);
    REQUIRE(id != 0);

    const std::vector<char32_t> codepoints = bakedCodepoints();
    const std::optional<babylon::font::FontAtlas> atlas = font->bake(32.0f, codepoints);
    REQUIRE(atlas.has_value());
    const auto glyphIt = std::find(codepoints.begin(), codepoints.end(), U'A');
    REQUIRE(glyphIt != codepoints.end());
    const babylon::font::Glyph& glyph =
        atlas->glyphs[static_cast<std::size_t>(glyphIt - codepoints.begin())];

    const babylon::ui::Size size = painter.measureText(id, "A");
    CHECK(size.width == doctest::Approx(glyph.advance));
    const float lineHeight = atlas->ascent - atlas->descent + atlas->lineGap;
    CHECK(size.height == doctest::Approx(lineHeight));
    CHECK(painter.measureText(id, "AB").width > size.width);
    CHECK(painter.measureText(id, "").width == doctest::Approx(0.0f));

    for ( int frame = 0; frame < 2; ++frame )
    {
        REQUIRE(painter.begin(480, 480));
        painter.drawText(id, "Hello", babylon::ui::Rect{ 10.0f, 10.0f, 300.0f, 60.0f },
            babylon::ui::TextAlign::Left, kWhite);
        REQUIRE(painter.end());
    }

    const std::vector<RecordedCommand>& record = device.recorded();
    CHECK(countOf<PipelineCreated>(record) == 1);
    const std::vector<TextureHandle> textures = boundTextures(record);
    REQUIRE(textures.size() == 1);
    CHECK_FALSE(textures.front().isNull());
    CHECK(countOf<Draw>(record) == 2);
}

TEST_CASE("Images are registered once and switch the batch texture")
{
    babylon::image::Image image;
    image.width  = 2;
    image.height = 2;
    image.pixels = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 255, 255,
    };

    NullDevice device;
    RenderPainter painter(device);

    const babylon::ui::ImageId id = painter.addImage(image);
    REQUIRE(id != 0);

    babylon::image::Image empty;
    CHECK(painter.addImage(empty) == 0);

    REQUIRE(painter.begin(480, 480));
    painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, 10.0f, 10.0f }, kRed);
    painter.drawImage(id, babylon::ui::Rect{ 20.0f, 0.0f, 10.0f, 10.0f });
    painter.fillRect(babylon::ui::Rect{ 40.0f, 0.0f, 10.0f, 10.0f }, kRed);
    REQUIRE(painter.end());

    const std::vector<RecordedCommand>& record = device.recorded();
    CHECK(countOf<Draw>(record) == 3);
    const std::vector<TextureHandle> textures = boundTextures(record);
    CHECK(textures.size() == 2);
}

TEST_CASE("The SDL GPU device rasterizes fills and text into an offscreen target")
{
    babylon::render::test::keepGpuDriverLoaded();
    CaptureSink sink;
    const std::unique_ptr<babylon::render::Device> device =
        babylon::render::createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }

    constexpr std::uint32_t kSize = 480;
    const babylon::render::TextureHandle target = device->createTexture(
        babylon::render::TextureDesc{ kSize, kSize, 1, babylon::render::TextureFormat::Rgba8,
            babylon::render::TextureUsage::ColorTarget });
    REQUIRE_FALSE(target.isNull());

    const std::optional<babylon::font::Font> font = loadFont();
    REQUIRE(font.has_value());

    RenderPainter painter(*device);
    const babylon::ui::FontId id = painter.addFont(*font, 32.0f);
    REQUIRE(id != 0);

    FrameTarget frameTarget;
    frameTarget.texture = target;
    frameTarget.format  = babylon::render::TextureFormat::Rgba8;
    frameTarget.load    = babylon::render::LoadOp::Clear;
    frameTarget.clear   = babylon::render::ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

    REQUIRE(painter.begin(kSize, kSize, frameTarget));
    painter.fillRect(babylon::ui::Rect{ 10.0f, 10.0f, 100.0f, 100.0f }, kRed);
    painter.drawText(id, "Hello", babylon::ui::Rect{ 150.0f, 20.0f, 300.0f, 60.0f },
        babylon::ui::TextAlign::Left, kWhite);
    REQUIRE(painter.end());

    const std::vector<std::byte> pixels = device->readTexture(target, 0);
    REQUIRE(pixels.size() == static_cast<std::size_t>(kSize) * kSize * 4u);
    const auto pixelAt = [&pixels](std::uint32_t x, std::uint32_t y)
    {
        const std::size_t index =
            (static_cast<std::size_t>(y) * kSize + x) * 4u;
        return std::array<int, 4>{ static_cast<int>(pixels[index + 0u]),
            static_cast<int>(pixels[index + 1u]), static_cast<int>(pixels[index + 2u]),
            static_cast<int>(pixels[index + 3u]) };
    };

    CHECK(pixelAt(60, 60) == std::array<int, 4>{ 255, 0, 0, 255 });
    CHECK(pixelAt(5, 5) == std::array<int, 4>{ 0, 0, 0, 255 });

    bool anyTextPixel = false;
    for ( std::uint32_t y = 20; y < 80 && !anyTextPixel; ++y )
    {
        for ( std::uint32_t x = 150; x < 450; ++x )
        {
            const std::array<int, 4> pixel = pixelAt(x, y);
            if ( pixel[0] > 64 || pixel[1] > 64 || pixel[2] > 64 )
            {
                anyTextPixel = true;
                break;
            }
        }
    }
    CHECK(anyTextPixel);
}

TEST_CASE("A font measures in canvas units across pixel scales and re-bakes")
{
    const std::optional<babylon::font::Font> font = loadFont();
    REQUIRE(font.has_value());

    NullDevice device;
    RenderPainter painter(device);

    const babylon::ui::FontId id = painter.addFont(*font, 16.0f);
    REQUIRE(id != 0);
    const babylon::ui::Size before = painter.measureText(id, "Hamburgefonts");
    REQUIRE(before.width > 0.0f);
    REQUIRE(before.height > 0.0f);

    REQUIRE(painter.begin(480, 480));
    painter.drawText(id, "Hamburgefonts", babylon::ui::Rect{ 10.0f, 10.0f, 460.0f, 40.0f },
        babylon::ui::TextAlign::Left, kWhite);
    REQUIRE(painter.end());

    const std::vector<TextureHandle> firstTextures = boundTextures(device.recorded());
    REQUIRE(firstTextures.size() == 1u);
    const std::size_t firstAtlasBytes = device.readTexture(firstTextures.front(), 0).size();
    REQUIRE(firstAtlasBytes > 0u);

    REQUIRE(painter.setPixelScale(2.0f));
    const babylon::ui::Size after = painter.measureText(id, "Hamburgefonts");
    CHECK(after.width == doctest::Approx(before.width).epsilon(0.02));
    CHECK(std::abs(after.height - before.height) <= 0.5f);

    device.clearRecorded();
    REQUIRE(painter.begin(960, 960));
    painter.drawText(id, "Hamburgefonts", babylon::ui::Rect{ 20.0f, 20.0f, 920.0f, 80.0f },
        babylon::ui::TextAlign::Left, kWhite);
    REQUIRE(painter.end());

    const std::vector<TextureHandle> secondTextures = boundTextures(device.recorded());
    REQUIRE(secondTextures.size() == 1u);
    const std::size_t secondAtlasBytes = device.readTexture(secondTextures.front(), 0).size();
    // The old behavior baked the same atlas and stretched it in the vertex
    // shader, so its byte counts were equal. A 2x bake must be larger.
    CHECK(secondAtlasBytes > firstAtlasBytes);
}

TEST_CASE("Choice arrows and scroll markers use the font's fallback glyphs")
{
    const std::optional<babylon::font::Font> font = loadFont();
    REQUIRE(font.has_value());

    NullDevice device;
    RenderPainter painter(device);

    const babylon::ui::FontId id = painter.addFont(*font, 32.0f);
    REQUIRE(id != 0);

    const auto chosen = [&font](char32_t first, char32_t second, char32_t ascii)
    {
        return font->hasGlyph(first) ? first : (font->hasGlyph(second) ? second : ascii);
    };
    const auto utf8 = [](char32_t codepoint)
    {
        std::string text;
        if ( codepoint < 0x80 )
        {
            text.push_back(static_cast<char>(codepoint));
        }
        else if ( codepoint < 0x800 )
        {
            text.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else
        {
            text.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            text.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        return text;
    };

    const std::pair<char32_t, char32_t> cases[] = {
        { 0x25C0, chosen(0x25C0, 0x2039, U'<') },
        { 0x25B6, chosen(0x25B6, 0x203A, U'>') },
        { 0x25B2, chosen(0x25B2, U'^', U'^') },
        { 0x25BC, chosen(0x25BC, U'v', U'v') },
    };
    for ( const auto& [symbol, fallback] : cases )
    {
        const babylon::ui::Size drawn = painter.measureText(id, utf8(symbol));
        const babylon::ui::Size expected = painter.measureText(id, utf8(fallback));
        CHECK(drawn.width == expected.width);
        CHECK(drawn.height == expected.height);
    }
}

TEST_CASE("A list-mode frame records into the caller's list without submitting")
{
    NullDevice device;
    RenderPainter painter(device);

    babylon::render::CommandList list = device.beginFrame();
    REQUIRE(painter.begin(list, 480, 480));
    painter.fillRect(babylon::ui::Rect{ 10.0f, 10.0f, 10.0f, 10.0f }, kRed);
    REQUIRE(painter.end());

    CHECK(countOf<FrameSubmitted>(device.recorded()) == 0u);
    CHECK(countOf<BeginPass>(device.recorded()) == 0u);

    REQUIRE(device.submit(list));
    CHECK(countOf<FrameSubmitted>(device.recorded()) == 1u);
    const BeginPass* pPass = firstOf<BeginPass>(device.recorded());
    REQUIRE(pPass != nullptr);
    REQUIRE(pPass->desc.colorTargets.size() == 1u);
    CHECK(pPass->desc.colorTargets.front().target.isNull());
}

TEST_CASE("A released image draws nothing and its id is not reused")
{
    babylon::image::Image image;
    image.width  = 2;
    image.height = 2;
    image.pixels = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };

    NullDevice device;
    RenderPainter painter(device);

    const babylon::ui::ImageId id = painter.addImage(image);
    REQUIRE(id != 0);
    painter.releaseImage(id);

    REQUIRE(painter.begin(480, 480));
    painter.drawImage(id, babylon::ui::Rect{ 10.0f, 10.0f, 10.0f, 10.0f });
    REQUIRE(painter.end());
    CHECK(countOf<Draw>(device.recorded()) == 0u);

    const babylon::ui::ImageId second = painter.addImage(image);
    REQUIRE(second != 0);
    CHECK(second > id);
}

TEST_CASE("A 2x frame bakes a 2x font, matching a 1x 2x-size reference (GPU)")
{
    babylon::render::test::keepGpuDriverLoaded();
    CaptureSink sink;
    const std::unique_ptr<babylon::render::Device> device =
        babylon::render::createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }

    const std::optional<babylon::font::Font> font = loadFont();
    REQUIRE(font.has_value());

    struct Rendered
    {
        std::vector<std::uint8_t> pixels;
        std::uint32_t size = 0;
        std::uint32_t minX = 0;
        std::uint32_t minY = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    const auto render = [&device, &font](std::uint32_t size, float fontHeight,
        const babylon::ui::Rect& rect)
    {
        const babylon::render::TextureHandle target = device->createTexture(
            babylon::render::TextureDesc{ size, size, 1,
                babylon::render::TextureFormat::Rgba8,
                babylon::render::TextureUsage::ColorTarget });
        REQUIRE_FALSE(target.isNull());

        FrameTarget frameTarget;
        frameTarget.texture = target;
        frameTarget.format  = babylon::render::TextureFormat::Rgba8;
        frameTarget.load    = babylon::render::LoadOp::Clear;
        frameTarget.clear   = babylon::render::ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

        RenderPainter painter(*device);
        const babylon::ui::FontId id = painter.addFont(*font, fontHeight);
        REQUIRE(id != 0);
        REQUIRE(painter.begin(size, size, frameTarget));
        painter.drawText(id, "H", rect, babylon::ui::TextAlign::Left, kWhite);
        REQUIRE(painter.end());

        const std::vector<std::byte> bytes = device->readTexture(target, 0);
        device->destroyTexture(target);
        REQUIRE(bytes.size() == static_cast<std::size_t>(size) * size * 4u);

        Rendered rendered;
        rendered.size   = size;
        rendered.pixels.resize(bytes.size());
        std::memcpy(rendered.pixels.data(), bytes.data(), bytes.size());

        std::uint32_t minX = size;
        std::uint32_t minY = size;
        std::uint32_t maxX = 0;
        std::uint32_t maxY = 0;
        for ( std::uint32_t y = 0; y < size; ++y )
        {
            for ( std::uint32_t x = 0; x < size; ++x )
            {
                const std::size_t index = (static_cast<std::size_t>(y) * size + x) * 4u;
                if ( rendered.pixels[index] > 8u )
                {
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
        }
        REQUIRE(minX < maxX);
        REQUIRE(minY < maxY);
        rendered.minX   = minX;
        rendered.minY   = minY;
        rendered.width  = maxX - minX + 1u;
        rendered.height = maxY - minY + 1u;
        return rendered;
    };

    const Rendered reference = render(480u, 64.0f,
        babylon::ui::Rect{ 100.0f, 100.0f, 200.0f, 100.0f });
    const Rendered scaled = render(960u, 32.0f,
        babylon::ui::Rect{ 50.0f, 50.0f, 100.0f, 50.0f });

    CHECK(std::abs(static_cast<int>(reference.width) - static_cast<int>(scaled.width)) <= 2);
    CHECK(std::abs(static_cast<int>(reference.height) - static_cast<int>(scaled.height)) <= 2);

    const std::uint32_t width  = std::min(reference.width, scaled.width);
    const std::uint32_t height = std::min(reference.height, scaled.height);
    std::uint64_t difference = 0;
    std::uint64_t samples    = 0;
    for ( std::uint32_t y = 0; y < height; ++y )
    {
        for ( std::uint32_t x = 0; x < width; ++x )
        {
            const std::size_t a = ((static_cast<std::size_t>(reference.minY + y)
                * reference.size) + reference.minX + x) * 4u;
            const std::size_t b = ((static_cast<std::size_t>(scaled.minY + y)
                * scaled.size) + scaled.minX + x) * 4u;
            difference += static_cast<std::uint64_t>(std::abs(
                static_cast<int>(reference.pixels[a]) - static_cast<int>(scaled.pixels[b])));
            ++samples;
        }
    }
    REQUIRE(samples > 0u);
    const double meanDifference = static_cast<double>(difference) / static_cast<double>(samples);
    // The same 64-pixel raster is drawn in both frames; a 32-pixel atlas
    // stretched to 64 differs much more than this margin.
    CHECK(meanDifference < 6.0);
}
