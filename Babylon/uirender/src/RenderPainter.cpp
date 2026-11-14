// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/uirender/RenderPainter.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/shaders/babylon_uirender.h>

#include <babylon/log/Log.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace babylon::uirender
{
    namespace
    {
        constexpr float kCanvasHeight = 480.0f;

        // The vertex of ui_text.vert.hlsl: a pixel position from the top left,
        // a texture coordinate and a straight-alpha color.
        struct Vertex
        {
            float x;
            float y;
            float u;
            float v;
            std::uint8_t r;
            std::uint8_t g;
            std::uint8_t b;
            std::uint8_t a;
        };
        static_assert(sizeof(Vertex) == 20);

        // One contiguous run of quads with the same texture and scissor.
        struct Batch
        {
            std::uint32_t firstVertex = 0;
            std::uint32_t vertexCount = 0;
            render::TextureHandle texture;
            render::ScissorRect scissor;
        };

        bool isEmpty(const ui::Rect& rect)
        {
            return !(rect.width > 0.0f) || !(rect.height > 0.0f);
        }

        ui::Rect intersect(const ui::Rect& a, const ui::Rect& b)
        {
            const float x0 = std::max(a.x, b.x);
            const float y0 = std::max(a.y, b.y);
            const float x1 = std::min(a.x + a.width, b.x + b.width);
            const float y1 = std::min(a.y + a.height, b.y + b.height);

            ui::Rect result;
            if ( x1 <= x0 || y1 <= y0 )
            {
                return result;
            }

            result.x      = x0;
            result.y      = y0;
            result.width  = x1 - x0;
            result.height = y1 - y0;
            return result;
        }

        bool intersects(const ui::Rect& a, const ui::Rect& b)
        {
            return !isEmpty(intersect(a, b));
        }

        std::uint8_t toByte(float value)
        {
            value = std::clamp(value, 0.0f, 1.0f);
            return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
        }

        std::uint32_t clampIndex(float value, std::uint32_t maximum)
        {
            if ( !(value > 0.0f) )
            {
                return 0;
            }
            const float limit = static_cast<float>(maximum);
            if ( value >= limit )
            {
                return maximum;
            }
            return static_cast<std::uint32_t>(value);
        }

        // The pixel rectangle a canvas clip covers, rounded outwards, or
        // nullopt when it covers no pixel of a targetWidth x targetHeight
        // target.
        std::optional<render::ScissorRect> scissorFor(const ui::Rect& clip, float scale,
            std::uint32_t targetWidth, std::uint32_t targetHeight)
        {
            if ( isEmpty(clip) )
            {
                return std::nullopt;
            }

            const float left   = std::floor(clip.x * scale);
            const float top    = std::floor(clip.y * scale);
            const float right  = std::ceil((clip.x + clip.width) * scale);
            const float bottom = std::ceil((clip.y + clip.height) * scale);

            render::ScissorRect scissor;
            scissor.x      = clampIndex(left, targetWidth);
            scissor.y      = clampIndex(top, targetHeight);
            scissor.width  = clampIndex(right, targetWidth);
            scissor.height = clampIndex(bottom, targetHeight);
            if ( scissor.width > scissor.x )
            {
                scissor.width -= scissor.x;
            }
            else
            {
                scissor.width = 0;
            }
            if ( scissor.height > scissor.y )
            {
                scissor.height -= scissor.y;
            }
            else
            {
                scissor.height = 0;
            }
            if ( scissor.width == 0 || scissor.height == 0 )
            {
                return std::nullopt;
            }
            return scissor;
        }

        // Decodes UTF-8 into codepoints, replacing each malformed byte with
        // U+FFFD. Drawing and measuring both use this, so they agree.
        std::vector<char32_t> decodeUtf8(std::string_view text)
        {
            std::vector<char32_t> codepoints;
            codepoints.reserve(text.size());

            std::size_t index = 0;
            while ( index < text.size() )
            {
                const unsigned char first = static_cast<unsigned char>(text[index]);
                if ( first < 0x80 )
                {
                    codepoints.push_back(static_cast<char32_t>(first));
                    ++index;
                    continue;
                }

                std::size_t length = 0;
                char32_t codepoint = 0;
                char32_t minimum   = 0;
                if ( (first & 0xE0u) == 0xC0u )
                {
                    length    = 2;
                    codepoint = first & 0x1Fu;
                    minimum   = 0x80;
                }
                else if ( (first & 0xF0u) == 0xE0u )
                {
                    length    = 3;
                    codepoint = first & 0x0Fu;
                    minimum   = 0x800;
                }
                else if ( (first & 0xF8u) == 0xF0u )
                {
                    length    = 4;
                    codepoint = first & 0x07u;
                    minimum   = 0x10000;
                }
                else
                {
                    codepoints.push_back(0xFFFD);
                    ++index;
                    continue;
                }

                bool valid = index + length <= text.size();
                for ( std::size_t cur = 1; valid && cur < length; ++cur )
                {
                    const unsigned char next =
                        static_cast<unsigned char>(text[index + cur]);
                    if ( (next & 0xC0u) != 0x80u )
                    {
                        valid = false;
                        break;
                    }
                    codepoint = (codepoint << 6) | (next & 0x3Fu);
                }

                if ( !valid || codepoint < minimum || codepoint > 0x10FFFF
                    || (codepoint >= 0xD800 && codepoint <= 0xDFFF) )
                {
                    codepoints.push_back(0xFFFD);
                    ++index;
                    continue;
                }

                codepoints.push_back(codepoint);
                index += length;
            }

            return codepoints;
        }

        const font::Glyph* findGlyph(const std::vector<char32_t>& codepoints,
            const std::vector<font::Glyph>& glyphs, char32_t codepoint)
        {
            const auto it = std::find(codepoints.begin(), codepoints.end(), codepoint);
            if ( it == codepoints.end() )
            {
                return nullptr;
            }
            return &glyphs[static_cast<std::size_t>(it - codepoints.begin())];
        }

        std::vector<char32_t> bakedCodepoints()
        {
            std::vector<char32_t> codepoints;
            codepoints.reserve(0x7F - 0x20 + 0x100 - 0xA0 + 1);
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

        // A symbol outside the baked set draws '?'.
        void normalizeCodepoints(const std::vector<char32_t>& baked,
            const std::vector<font::Glyph>& glyphs, std::vector<char32_t>& codepoints)
        {
            for ( char32_t& codepoint : codepoints )
            {
                if ( !findGlyph(baked, glyphs, codepoint) )
                {
                    codepoint = U'?';
                }
            }
        }
    }

    struct RenderPainter::State
    {
        struct FontEntry
        {
            std::optional<font::Font> font;
            float pixelHeight = 0.0f;
            std::vector<char32_t> codepoints;
            std::vector<font::Glyph> glyphs;
            float ascent  = 0.0f;
            float descent = 0.0f;
            float lineGap = 0.0f;
            render::TextureHandle atlas;
            std::uint32_t atlasWidth  = 0;
            std::uint32_t atlasHeight = 0;
        };

        struct ImageEntry
        {
            render::TextureHandle texture;
        };

        explicit State(render::Device& deviceRef) : device(deviceRef) {}

        render::Device& device;
        render::SamplerHandle sampler;
        render::TextureHandle whiteTexture;
        render::BufferHandle vertexBuffer;
        std::uint32_t vertexBufferBytes = 0;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> pipelines;
        std::vector<FontEntry> fonts;
        std::vector<ImageEntry> images;

        bool active = false;
        std::uint32_t targetWidth  = 0;
        std::uint32_t targetHeight = 0;
        float scale       = 1.0f;
        float canvasWidth = 0.0f;
        FrameTarget target;
        render::TextureFormat currentPipelineFormat = render::TextureFormat::Bgra8;
        std::vector<ui::Rect> clips;
        std::vector<Vertex> vertices;
        std::vector<Batch> batches;

        bool ensureShared()
        {
            if ( sampler.isNull() )
            {
                sampler = device.createSampler(render::SamplerDesc{ render::Filter::Linear,
                    render::Filter::Linear, render::Filter::Nearest,
                    render::AddressMode::ClampToEdge, render::AddressMode::ClampToEdge });
                if ( sampler.isNull() )
                {
                    log::warning("RenderPainter: the sampler could not be created");
                    return false;
                }
            }

            if ( whiteTexture.isNull() )
            {
                whiteTexture = device.createTexture(render::TextureDesc{ 1, 1, 1,
                    render::TextureFormat::Rgba8, render::TextureUsage::Sampled });
                if ( whiteTexture.isNull() )
                {
                    log::warning("RenderPainter: the white texture could not be created");
                    return false;
                }
                const std::uint8_t pixel[4] = { 255, 255, 255, 255 };
                if ( !device.uploadTexture(whiteTexture, 0,
                    std::as_bytes(std::span<const std::uint8_t>(pixel, 4))) )
                {
                    device.destroyTexture(whiteTexture);
                    whiteTexture = {};
                    log::warning("RenderPainter: the white texture could not be uploaded");
                    return false;
                }
            }

            return true;
        }

        render::PipelineHandle ensurePipeline(render::TextureFormat format)
        {
            for ( const auto& [cachedFormat, pipeline] : pipelines )
            {
                if ( cachedFormat == format )
                {
                    return pipeline;
                }
            }

            render::BlendState blend;
            blend.enabled    = true;
            blend.srcColor   = render::BlendFactor::SrcAlpha;
            blend.dstColor   = render::BlendFactor::OneMinusSrcAlpha;
            blend.colorOp    = render::BlendOp::Add;
            blend.srcAlpha   = render::BlendFactor::One;
            blend.dstAlpha   = render::BlendFactor::OneMinusSrcAlpha;
            blend.alphaOp    = render::BlendOp::Add;

            render::PipelineDesc desc;
            desc.vertexShader   = shaders::uiTextVert();
            desc.fragmentShader = shaders::uiTextFrag();
            desc.vertexLayout.bufferStrides = { sizeof(Vertex) };
            desc.vertexLayout.attributes = {
                render::VertexAttribute{ 0, 0, render::VertexFormat::Float2, 0 },
                render::VertexAttribute{ 1, 0, render::VertexFormat::Float2, 8 },
                render::VertexAttribute{ 2, 0, render::VertexFormat::UByte4Norm, 16 },
            };
            desc.primitive    = render::PrimitiveType::TriangleList;
            desc.colorTargets = { render::ColorTargetDesc{ format, blend } };

            const render::PipelineHandle pipeline = device.createPipeline(desc);
            if ( pipeline.isNull() )
            {
                log::warning("RenderPainter: the pipeline could not be created");
                return pipeline;
            }

            pipelines.emplace_back(format, pipeline);
            return pipeline;
        }

        const ui::Rect& currentClip() const
        {
            return clips.back();
        }

        void appendQuad(const ui::Rect& rect, float u0, float v0, float u1, float v1,
            render::TextureHandle texture, const ui::Color& color,
            const render::ScissorRect& scissor)
        {
            if ( isEmpty(rect) )
            {
                return;
            }

            const float x0 = rect.x * scale;
            const float y0 = rect.y * scale;
            const float x1 = (rect.x + rect.width) * scale;
            const float y1 = (rect.y + rect.height) * scale;

            const float width  = static_cast<float>(targetWidth);
            const float height = static_cast<float>(targetHeight);
            const float left   = x0 * 2.0f / width - 1.0f;
            const float right  = x1 * 2.0f / width - 1.0f;
            const float top    = 1.0f - y0 * 2.0f / height;
            const float bottom = 1.0f - y1 * 2.0f / height;

            const std::uint8_t r = toByte(color.r);
            const std::uint8_t g = toByte(color.g);
            const std::uint8_t b = toByte(color.b);
            const std::uint8_t a = toByte(color.a);

            const std::uint32_t first = static_cast<std::uint32_t>(vertices.size());
            const auto addVertex = [this, r, g, b, a](float x, float y, float u, float v)
            {
                vertices.push_back(Vertex{ x, y, u, v, r, g, b, a });
            };
            addVertex(left, top, u0, v0);
            addVertex(right, top, u1, v0);
            addVertex(left, bottom, u0, v1);
            addVertex(left, bottom, u0, v1);
            addVertex(right, top, u1, v0);
            addVertex(right, bottom, u1, v1);

            if ( !batches.empty() && batches.back().texture == texture
                && batches.back().scissor == scissor )
            {
                batches.back().vertexCount += 6;
            }
            else
            {
                batches.push_back(Batch{ first, 6, texture, scissor });
            }
        }

        void fillRect(const ui::Rect& rect, const ui::Color& color)
        {
            if ( !active || isEmpty(rect) )
            {
                return;
            }
            const std::optional<render::ScissorRect> scissor =
                scissorFor(currentClip(), scale, targetWidth, targetHeight);
            if ( !scissor )
            {
                return;
            }
            appendQuad(rect, 0.5f, 0.5f, 0.5f, 0.5f, whiteTexture, color, *scissor);
        }

        void drawImage(ui::ImageId image, const ui::Rect& rect)
        {
            if ( image == 0 || image > images.size() )
            {
                log::warning("RenderPainter::drawImage: unknown image {}", image);
                return;
            }
            if ( !active || isEmpty(rect) )
            {
                return;
            }
            const std::optional<render::ScissorRect> scissor =
                scissorFor(currentClip(), scale, targetWidth, targetHeight);
            if ( !scissor )
            {
                return;
            }
            const ui::Color white = { 1.0f, 1.0f, 1.0f, 1.0f };
            appendQuad(rect, 0.0f, 0.0f, 1.0f, 1.0f, images[image - 1].texture, white,
                *scissor);
        }

        float fontLineHeight(const FontEntry& entry) const
        {
            float lineHeight = entry.ascent - entry.descent + entry.lineGap;
            if ( !(lineHeight > 0.0f) )
            {
                lineHeight = entry.pixelHeight;
            }
            return lineHeight;
        }

        float textWidth(const FontEntry& entry, const std::vector<char32_t>& codepoints) const
        {
            if ( !entry.font )
            {
                return 0.0f;
            }

            float width = 0.0f;
            for ( std::size_t index = 0; index < codepoints.size(); ++index )
            {
                if ( index > 0 )
                {
                    width += entry.font->kerning(codepoints[index - 1],
                        codepoints[index], entry.pixelHeight);
                }
                if ( const font::Glyph* pGlyph =
                    findGlyph(entry.codepoints, entry.glyphs, codepoints[index]) )
                {
                    width += pGlyph->advance;
                }
            }
            return std::max(0.0f, width);
        }

        std::vector<char32_t> normalCodepoints(const FontEntry& entry,
            std::string_view text) const
        {
            std::vector<char32_t> codepoints = decodeUtf8(text);
            normalizeCodepoints(entry.codepoints, entry.glyphs, codepoints);
            return codepoints;
        }

        void drawText(ui::FontId font, std::string_view text, const ui::Rect& rect,
            ui::TextAlign align, const ui::Color& color)
        {
            if ( font == 0 || font > fonts.size() )
            {
                log::warning("RenderPainter::drawText: unknown font {}", font);
                return;
            }
            if ( !active || text.empty() || isEmpty(rect) )
            {
                return;
            }

            const FontEntry& entry = fonts[font - 1];
            const ui::Rect visible = intersect(rect, currentClip());
            const std::optional<render::ScissorRect> scissor =
                scissorFor(visible, scale, targetWidth, targetHeight);
            if ( !scissor )
            {
                return;
            }

            const std::vector<char32_t> codepoints = normalCodepoints(entry, text);
            const float drawnWidth = textWidth(entry, codepoints);
            float penX = rect.x;
            if ( align == ui::TextAlign::Center )
            {
                penX = rect.x + (rect.width - drawnWidth) * 0.5f;
            }
            else if ( align == ui::TextAlign::Right )
            {
                penX = rect.x + rect.width - drawnWidth;
            }

            const float lineHeight = fontLineHeight(entry);
            const float lineTop    = rect.y + (rect.height - lineHeight) * 0.5f;
            const float baseline   = lineTop + entry.ascent;
            const float atlasWidth  = static_cast<float>(entry.atlasWidth);
            const float atlasHeight = static_cast<float>(entry.atlasHeight);

            for ( std::size_t index = 0; index < codepoints.size(); ++index )
            {
                if ( index > 0 && entry.font )
                {
                    penX += entry.font->kerning(codepoints[index - 1],
                        codepoints[index], entry.pixelHeight);
                }

                const font::Glyph* pGlyph =
                    findGlyph(entry.codepoints, entry.glyphs, codepoints[index]);
                if ( !pGlyph )
                {
                    continue;
                }

                if ( pGlyph->width > 0 && pGlyph->height > 0 )
                {
                    const ui::Rect destination = { penX + pGlyph->offsetX,
                        baseline + pGlyph->offsetY, static_cast<float>(pGlyph->width),
                        static_cast<float>(pGlyph->height) };
                    if ( intersects(destination, visible) )
                    {
                        const float u0 = static_cast<float>(pGlyph->x) / atlasWidth;
                        const float v0 = static_cast<float>(pGlyph->y) / atlasHeight;
                        const float u1 =
                            static_cast<float>(pGlyph->x + pGlyph->width) / atlasWidth;
                        const float v1 =
                            static_cast<float>(pGlyph->y + pGlyph->height) / atlasHeight;
                        appendQuad(destination, u0, v0, u1, v1, entry.atlas, color,
                            *scissor);
                    }
                }

                penX += pGlyph->advance;
            }
        }

        ui::Size measureText(ui::FontId font, std::string_view text) const
        {
            ui::Size size;
            if ( font == 0 || font > fonts.size() )
            {
                return size;
            }

            const FontEntry& entry = fonts[font - 1];
            const std::vector<char32_t> codepoints = normalCodepoints(entry, text);
            size.width  = textWidth(entry, codepoints);
            size.height = fontLineHeight(entry);
            return size;
        }

        void resetFrame()
        {
            vertices.clear();
            batches.clear();
            clips.clear();
            target = FrameTarget{};
        }

        bool submitFrame()
        {
            render::CommandList list = device.beginFrame();

            if ( vertices.size()
                > std::numeric_limits<std::uint32_t>::max() / sizeof(Vertex) )
            {
                log::warning("RenderPainter: the frame has too many vertices");
                return false;
            }
            const std::uint32_t vertexBytes =
                static_cast<std::uint32_t>(vertices.size() * sizeof(Vertex));
            if ( vertexBytes > 0 )
            {
                if ( vertexBuffer.isNull() || vertexBufferBytes < vertexBytes )
                {
                    if ( !vertexBuffer.isNull() )
                    {
                        device.destroyBuffer(vertexBuffer);
                    }
                    vertexBuffer = device.createBuffer(
                        render::BufferDesc{ vertexBytes, render::BufferUsage::Vertex });
                    vertexBufferBytes = vertexBytes;
                    if ( vertexBuffer.isNull() )
                    {
                        log::warning("RenderPainter: the vertex buffer could not be created");
                        return false;
                    }
                }
                list.updateBuffer(vertexBuffer, 0,
                    std::as_bytes(std::span<const Vertex>(vertices.data(), vertices.size())));
            }

            render::PassDesc pass;
            render::ColorAttachment attachment;
            attachment.target = target.texture;
            attachment.mip    = 0;
            attachment.load   = target.load;
            attachment.store  = render::StoreOp::Store;
            attachment.clear  = target.clear;
            pass.colorTargets = { attachment };
            if ( !list.beginPass(pass) )
            {
                return false;
            }

            list.setViewport(render::Viewport{ 0.0f, 0.0f,
                static_cast<float>(targetWidth), static_cast<float>(targetHeight),
                0.0f, 1.0f });

            if ( !batches.empty() )
            {
                bool pipelineSet = false;
                for ( const auto& [format, pipeline] : pipelines )
                {
                    if ( format == currentPipelineFormat )
                    {
                        list.setPipeline(pipeline);
                        pipelineSet = true;
                        break;
                    }
                }
                if ( !pipelineSet )
                {
                    log::warning("RenderPainter: the frame's pipeline is missing");
                    return false;
                }
                list.bindVertexBuffer(0, vertexBuffer);

                render::TextureHandle boundTexture;
                for ( const Batch& batch : batches )
                {
                    list.setScissor(batch.scissor);
                    if ( batch.texture != boundTexture )
                    {
                        list.bindTexture(render::ShaderStage::Fragment, 0, batch.texture,
                            sampler);
                        boundTexture = batch.texture;
                    }
                    list.draw(batch.vertexCount, batch.firstVertex);
                }
            }

            if ( !list.endPass() )
            {
                return false;
            }
            return device.submit(list);
        }
    };

    RenderPainter::RenderPainter(render::Device& device)
        : m_state(std::make_unique<State>(device))
    {
    }

    RenderPainter::~RenderPainter()
    {
        for ( State::FontEntry& entry : m_state->fonts )
        {
            if ( !entry.atlas.isNull() )
            {
                m_state->device.destroyTexture(entry.atlas);
            }
        }
        for ( State::ImageEntry& entry : m_state->images )
        {
            if ( !entry.texture.isNull() )
            {
                m_state->device.destroyTexture(entry.texture);
            }
        }
        for ( const auto& entry : m_state->pipelines )
        {
            m_state->device.destroyPipeline(entry.second);
        }
        if ( !m_state->vertexBuffer.isNull() )
        {
            m_state->device.destroyBuffer(m_state->vertexBuffer);
        }
        if ( !m_state->whiteTexture.isNull() )
        {
            m_state->device.destroyTexture(m_state->whiteTexture);
        }
        if ( !m_state->sampler.isNull() )
        {
            m_state->device.destroySampler(m_state->sampler);
        }
    }

    ui::FontId RenderPainter::addFont(const font::Font& font, float pixelHeight)
    {
        if ( !(pixelHeight > 0.0f) || !std::isfinite(pixelHeight) )
        {
            log::warning("RenderPainter::addFont: bad pixel height");
            return 0;
        }

        const std::vector<char32_t> codepoints = bakedCodepoints();
        const std::optional<font::FontAtlas> atlas = font.bake(pixelHeight, codepoints);
        if ( !atlas )
        {
            log::warning("RenderPainter::addFont: the font did not bake");
            return 0;
        }

        const render::DeviceLimits& limits = m_state->device.info().limits;
        if ( atlas->width == 0 || atlas->height == 0
            || atlas->width > limits.maxTextureDimension
            || atlas->height > limits.maxTextureDimension )
        {
            log::warning("RenderPainter::addFont: the atlas is too large for the device");
            return 0;
        }

        const std::size_t pixelCount =
            static_cast<std::size_t>(atlas->width) * atlas->height;
        std::vector<std::uint8_t> rgba(pixelCount * 4u);
        for ( std::size_t index = 0; index < pixelCount; ++index )
        {
            rgba[index * 4u + 0u] = 255;
            rgba[index * 4u + 1u] = 255;
            rgba[index * 4u + 2u] = 255;
            rgba[index * 4u + 3u] = atlas->coverage[index];
        }

        const render::TextureHandle texture = m_state->device.createTexture(
            render::TextureDesc{ atlas->width, atlas->height, 1,
                render::TextureFormat::Rgba8, render::TextureUsage::Sampled });
        if ( texture.isNull() )
        {
            log::warning("RenderPainter::addFont: the atlas texture could not be created");
            return 0;
        }
        if ( !m_state->device.uploadTexture(texture, 0, std::as_bytes(std::span(rgba))) )
        {
            m_state->device.destroyTexture(texture);
            log::warning("RenderPainter::addFont: the atlas texture could not be uploaded");
            return 0;
        }

        State::FontEntry entry;
        entry.font        = font;
        entry.pixelHeight = pixelHeight;
        entry.codepoints  = codepoints;
        entry.glyphs      = atlas->glyphs;
        entry.ascent      = atlas->ascent;
        entry.descent     = atlas->descent;
        entry.lineGap     = atlas->lineGap;
        entry.atlas       = texture;
        entry.atlasWidth  = atlas->width;
        entry.atlasHeight = atlas->height;
        m_state->fonts.push_back(std::move(entry));
        return static_cast<ui::FontId>(m_state->fonts.size());
    }

    ui::ImageId RenderPainter::addImage(const image::Image& image)
    {
        if ( image.width == 0 || image.height == 0 || image.pixels.empty() )
        {
            log::warning("RenderPainter::addImage: empty image");
            return 0;
        }

        const std::uint64_t expected =
            static_cast<std::uint64_t>(image.width) * image.height * 4u;
        if ( expected != static_cast<std::uint64_t>(image.pixels.size()) )
        {
            log::warning("RenderPainter::addImage: image size does not match its dimensions");
            return 0;
        }

        const render::DeviceLimits& limits = m_state->device.info().limits;
        if ( image.width > limits.maxTextureDimension
            || image.height > limits.maxTextureDimension )
        {
            log::warning("RenderPainter::addImage: the image is too large for the device");
            return 0;
        }

        const render::TextureHandle texture = m_state->device.createTexture(
            render::TextureDesc{ image.width, image.height, 1, render::TextureFormat::Rgba8,
                render::TextureUsage::Sampled });
        if ( texture.isNull() )
        {
            log::warning("RenderPainter::addImage: the texture could not be created");
            return 0;
        }
        if ( !m_state->device.uploadTexture(texture, 0, std::as_bytes(std::span(image.pixels))) )
        {
            m_state->device.destroyTexture(texture);
            log::warning("RenderPainter::addImage: the texture could not be uploaded");
            return 0;
        }

        m_state->images.push_back(State::ImageEntry{ texture });
        return static_cast<ui::ImageId>(m_state->images.size());
    }

    bool RenderPainter::begin(std::uint32_t targetWidth, std::uint32_t targetHeight,
        const FrameTarget& target)
    {
        if ( m_state->active )
        {
            log::warning("RenderPainter::begin: a frame is already active");
            return false;
        }
        if ( targetWidth == 0 || targetHeight == 0 )
        {
            log::warning("RenderPainter::begin: bad target size {}x{}", targetWidth,
                targetHeight);
            return false;
        }

        const render::TextureFormat format = target.texture.isNull()
            ? m_state->device.info().swapchainFormat : target.format;
        if ( !m_state->device.supportsFormat(format, render::TextureUsage::ColorTarget) )
        {
            log::warning("RenderPainter::begin: the target format cannot be rendered to");
            return false;
        }
        if ( !m_state->ensureShared() )
        {
            return false;
        }
        if ( m_state->ensurePipeline(format).isNull() )
        {
            return false;
        }

        m_state->target       = target;
        m_state->targetWidth  = targetWidth;
        m_state->targetHeight = targetHeight;
        m_state->scale        = static_cast<float>(targetHeight) / kCanvasHeight;
        m_state->canvasWidth  = static_cast<float>(targetWidth) / m_state->scale;
        m_state->clips.clear();
        m_state->clips.push_back(ui::Rect{ 0.0f, 0.0f, m_state->canvasWidth, kCanvasHeight });
        m_state->vertices.clear();
        m_state->batches.clear();
        m_state->currentPipelineFormat = format;
        m_state->active = true;
        return true;
    }

    bool RenderPainter::end()
    {
        if ( !m_state->active )
        {
            log::warning("RenderPainter::end: no frame is active");
            return false;
        }

        m_state->active = false;
        const bool submitted = m_state->submitFrame();
        m_state->resetFrame();
        return submitted;
    }

    void RenderPainter::fillRect(const ui::Rect& rect, const ui::Color& color)
    {
        m_state->fillRect(rect, color);
    }

    void RenderPainter::drawImage(ui::ImageId image, const ui::Rect& rect)
    {
        m_state->drawImage(image, rect);
    }

    void RenderPainter::drawText(ui::FontId font, std::string_view text, const ui::Rect& rect,
        ui::TextAlign align, const ui::Color& color)
    {
        m_state->drawText(font, text, rect, align, color);
    }

    ui::Size RenderPainter::measureText(ui::FontId font, std::string_view text) const
    {
        return m_state->measureText(font, text);
    }

    void RenderPainter::pushClip(const ui::Rect& rect)
    {
        if ( !m_state->active )
        {
            return;
        }
        m_state->clips.push_back(intersect(rect, m_state->currentClip()));
    }

    void RenderPainter::popClip()
    {
        if ( !m_state->active )
        {
            return;
        }
        if ( m_state->clips.size() <= 1 )
        {
            log::warning("RenderPainter::popClip: no matching pushClip");
            return;
        }
        m_state->clips.pop_back();
    }

}
