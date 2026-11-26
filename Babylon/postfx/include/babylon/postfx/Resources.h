// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>

#include <cstddef>
#include <span>

namespace babylon::render
{
    class CommandList;
    class Device;
}

namespace babylon::postfx
{

    // Shared per-device objects every effect uses: the clamping samplers and the full-screen
    // vertex shader. One per device; the chain owns one, a caller using effects alone makes its own.
    // Samplers are made by open(); close() destroys them and is a safe no-op when not open.
    // Borrows the device, which must outlive it. Not thread-safe.
    class Resources
    {
    public:
        explicit Resources(render::Device& device);
        ~Resources();                     // calls close()
        Resources(const Resources&) = delete;
        Resources& operator=(const Resources&) = delete;

        [[nodiscard]] bool open();        // false, with one warning, when a sampler can't be made
        void close();
        bool isOpen() const;

        render::Device& device() const;
        render::SamplerHandle linearClamp() const;   // linear min/mag, clamp to edge
        render::SamplerHandle nearestClamp() const;  // nearest min/mag, clamp to edge

        // The vertex shader that draws one triangle covering the target: no vertex buffer, three
        // vertices, texture coordinate (0,0) top left to (1,1) bottom right in TEXCOORD0.
        static const render::ShaderDesc& fullscreenVertexShader();

        // Records one full-screen pass into list: begins a pass on target (null = swapchain) with
        // LoadOp::DontCare, sets pipeline, binds each texture in order to fragment slots 0..n-1
        // with sampler, pushes uniforms (when not empty) to fragment uniform slot 0, draws 3
        // vertices and ends the pass. False, recording nothing, when list is inside a pass.
        static bool recordFullscreenPass(render::CommandList& list, render::TextureHandle target,
            render::PipelineHandle pipeline, std::span<const render::TextureHandle> textures,
            render::SamplerHandle sampler, std::span<const std::byte> uniforms = {});

    private:
        render::Device& m_device;
        render::SamplerHandle m_linearClamp;
        render::SamplerHandle m_nearestClamp;
        bool m_open = false;
    };

}
