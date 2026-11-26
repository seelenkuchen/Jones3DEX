// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/postfx/FrameInputs.h>

#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>

#include <cstdint>
#include <string_view>

namespace babylon::render
{
    class CommandList;
}

namespace babylon::postfx
{

    class Resources;
    class TargetPool;

    enum class Stage : std::uint8_t
    {
        Hdr,      // before the tone map, on the scene's HDR color, at scene size
        Resolve,  // the tone map: HDR scene size in, LDR output size out
        Ldr,      // after the tone map, at output size
    };

    struct Needs
    {
        bool depth = false;
        bool normals = false;
    };

    // What record() gets. input is sampled; output is written (null = swapchain). For Hdr and Ldr
    // effects the input and output sizes are equal; for Resolve they may differ.
    struct PassContext
    {
        render::CommandList& list;
        Resources& resources;
        TargetPool& targets;
        const FrameInputs& frame;
        render::TextureHandle input;
        render::TextureFormat inputFormat = render::TextureFormat::Rgba16F;
        std::uint32_t inputWidth = 0;
        std::uint32_t inputHeight = 0;
        render::TextureHandle output;
        render::TextureFormat outputFormat = render::TextureFormat::Bgra8;  // never "swapchain": resolved
        std::uint32_t outputWidth = 0;
        std::uint32_t outputHeight = 0;
    };

    // One post-processing effect: records its passes from ctx.input into ctx.output. Effects own
    // their pipelines (made on first use, per output format) and release them in
    // releaseResources(), which the owner calls before the device goes away; it is a safe no-op
    // when nothing was made. An effect is used by one chain or caller at a time.
    class Effect
    {
    public:
        virtual ~Effect() = default;

        virtual std::string_view name() const = 0;   // for logs
        virtual Stage stage() const = 0;
        virtual Needs needs() const { return {}; }

        // False when the effect would leave the image unchanged with its current settings (for
        // example strength 0); the chain then skips it without a pass.
        virtual bool active() const = 0;

        // Checks the context, then calls doRecord(). Returns false, with one warning and nothing
        // recorded, when input is null, a size is 0, the list is inside a pass, or an Hdr/Ldr
        // context has different input and output sizes; otherwise doRecord()'s result.
        [[nodiscard]] bool record(PassContext& ctx);

        virtual void releaseResources() = 0;

    private:
        // Records the passes. False when a resource can't be made; it may have recorded some passes
        // into ctx.output, which the caller then ignores.
        virtual bool doRecord(PassContext& ctx) = 0;
    };

}
