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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <variant>
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
