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
    // end() every Painter call only records quads on the CPU; the standalone
    // begin() mode ends by recording one command list and submitting it, so a
    // frame is one Device::submit() no matter how many calls it holds.
    //
    // Fonts are baked at the pixel height given to addFont() times the
    // painter's current pixel scale, for printable ASCII, Latin-1, U+2022 and
    // the Choice arrows and scroll markers the ui draws; a codepoint outside
    // the set draws '?'. setPixelScale() and the next begin() re-bake them at
    // another scale. addImage() uploads straight-alpha RGBA8 pixels once.
    // Both return an id of at least 1, or 0 with one warning. Font ids stay
    // valid for the painter's lifetime, including across re-bakes.
    //
    // A painter can record into a frame another module owns: the begin() that
    // takes a CommandList appends to that list, and its end() does not submit;
    // the caller submits the list exactly once. The standalone begin() owns
    // and submits its frame and presents it when the target is the swapchain.
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

        // Bakes font at pixelHeight canvas units times the painter's current
        // pixel scale (see setPixelScale) and returns the id to draw and
        // measure it with. The font is copied, so the caller may release it.
        // The scaling keeps one atlas texel per target pixel when the frame is
        // drawn at that scale. A bake failure, a non-positive or non-finite
        // height, or an atlas the device cannot hold returns 0.
        [[nodiscard]] ui::FontId addFont(const font::Font& font, float pixelHeight);

        // Sets the target's pixel scale: device pixels per canvas unit, that
        // is targetHeight / 480. Re-bakes every font whose atlas was baked at
        // another scale. Font ids stay valid and measureText() keeps returning
        // canvas units; only the atlas resolution changes. begin() calls it
        // with the frame height, so a caller that knows its target should call
        // it before measuring and building a screen; begin() then does nothing.
        // Returns false, with one warning, for a non-positive or non-finite
        // scale or when a font cannot be re-baked; the previous atlases of the
        // failed fonts are kept. Call only outside begin()/end().
        [[nodiscard]] bool setPixelScale(float pixelsPerCanvasUnit);

        // Uploads image's RGBA8 pixels and returns the id to draw it with. An
        // empty image, or a texture the device cannot hold, returns 0.
        [[nodiscard]] ui::ImageId addImage(const image::Image& image);

        // Releases an image returned by addImage(). Its texture is destroyed
        // and the id is invalid afterwards; drawImage() ignores it. Safe for 0
        // and for an id that was already released. Call only outside
        // begin()/end().
        void releaseImage(ui::ImageId image);

        // Starts a frame of targetWidth x targetHeight pixels. The dimensions
        // must be positive and match the target texture when one is given.
        // Returns false and changes nothing, with one warning, for invalid
        // dimensions, a target the device cannot render to, or a pipeline
        // that could not be created; a stale frame is discarded.
        [[nodiscard]] bool begin(std::uint32_t targetWidth, std::uint32_t targetHeight,
            const FrameTarget& target = {});

        // Records this frame into list, which must be the current frame of the
        // painter's device, instead of beginning and submitting a frame of its
        // own. end() then records into list and does not submit or present; the
        // caller submits list exactly once. This is how a module draws a pass
        // on top of a frame another module owns. list must not be inside a pass
        // and must stay valid until end(). The target rules are those of the
        // other begin(). Nothing about the target is submitted by the painter.
        [[nodiscard]] bool begin(render::CommandList& list, std::uint32_t targetWidth,
            std::uint32_t targetHeight, const FrameTarget& target = {});

        // Records the frame: into its own command list and a Device::submit()
        // in the standalone mode, into the caller's list without submitting in
        // the list mode. Returns false, with one warning, when no frame was
        // active or the device refused the standalone frame.
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
