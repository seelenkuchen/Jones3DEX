// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/font/Font.h>
#include <babylon/image/Image.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>
#include <babylon/ui/Painter.h>
#include <babylon/ui/Types.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace babylon::render
{
    class Device;
}

namespace babylon::uirender
{

    // Where a frame goes. A null texture names the device's swapchain, so a
    // windowed device presents the frame; format is then ignored and the
    // device's swapchain format is used. A texture target is drawn without
    // presenting and must be a color target of the given format, its size
    // matching the begin() dimensions.
    struct FrameTarget
    {
        render::TextureHandle texture;
        render::TextureFormat format = render::TextureFormat::Bgra8;
        render::LoadOp load = render::LoadOp::Clear;
        render::ClearColor clear;
    };

    // Painter that draws babylon::ui onto a babylon::render::Device through
    // its own batched 2D pipeline, without the game's renderer. One textured
    // and vertex-colored pipeline serves fills (a white 1x1 texture), images
    // and font glyphs; quads that share a texture and a clip form one draw.
    // Colors are straight alpha and are blended over the target.
    //
    // Canvas units are the ui's: 480 units high and 480 * aspect units wide,
    // origin at the top left. begin() maps them to the target's pixels, so a
    // frame is always drawn at the target's current size. Between begin() and
    // end() every Painter call only records quads on the CPU; end() records
    // one command list and submits it, so a frame is one Device::submit() no
    // matter how many calls it holds.
    //
    // Fonts are baked once, at the pixel height given to addFont(), for
    // printable ASCII, Latin-1 and U+2022; a codepoint outside the set draws
    // '?'. addImage() uploads straight-alpha
    // RGBA8 pixels once. Both return an id of at least 1, or 0 with one
    // warning. Ids stay valid until the painter is destroyed, which also
    // destroys every texture, buffer, sampler and pipeline it created.
    //
    // Calls outside begin() and end() are ignored; begin() and end() log one
    // warning and change nothing when their arguments are invalid. The
    // painter borrows the device and must be destroyed before it. Not
    // thread-safe.
    class RenderPainter final : public ui::Painter
    {
    public:
        explicit RenderPainter(render::Device& device);
        ~RenderPainter() override;

        RenderPainter(const RenderPainter&) = delete;
        RenderPainter& operator=(const RenderPainter&) = delete;

        // Bakes font at pixelHeight, which is in canvas units, and returns
        // the id to draw and measure it with. The font is copied. A bake
        // failure, a non-positive or non-finite height, or an atlas the
        // device cannot hold returns 0.
        [[nodiscard]] ui::FontId addFont(const font::Font& font, float pixelHeight);

        // Uploads image's RGBA8 pixels and returns the id to draw it with. An
        // empty image, or a texture the device cannot hold, returns 0.
        [[nodiscard]] ui::ImageId addImage(const image::Image& image);

        // Starts a frame of targetWidth x targetHeight pixels. The dimensions
        // must be positive and match the target texture when one is given.
        // Returns false and changes nothing, with one warning, for invalid
        // dimensions, a target the device cannot render to, or a pipeline
        // that could not be created; a stale frame is discarded.
        [[nodiscard]] bool begin(std::uint32_t targetWidth, std::uint32_t targetHeight,
            const FrameTarget& target = {});

        // Records and submits the frame's command list and presents it when
        // it drew to a swapchain. Returns false, with one warning, when no
        // frame was active or the device refused the list.
        [[nodiscard]] bool end();

        void fillRect(const ui::Rect& rect, const ui::Color& color) override;
        void drawImage(ui::ImageId image, const ui::Rect& rect) override;
        void drawText(ui::FontId font, std::string_view text, const ui::Rect& rect,
            ui::TextAlign align, const ui::Color& color) override;
        [[nodiscard]] ui::Size measureText(ui::FontId font, std::string_view text) const override;
        void pushClip(const ui::Rect& rect) override;
        void popClip() override;

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };

}
