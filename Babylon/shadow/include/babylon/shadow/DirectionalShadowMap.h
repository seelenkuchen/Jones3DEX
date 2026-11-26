// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/shadow/ShadowMath.h>

#include <babylon/render/Handle.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace babylon::render
{
    class CommandList;
    class Device;
}

namespace babylon::shadow
{

    // How a DirectionalShadowMap is made.
    struct ShadowMapDesc
    {
        std::uint32_t size = 2048;           // square, 256..8192
        float depthBiasConstant = 1.0f;      // pipeline depth bias for casters
        float depthBiasSlope = 1.5f;
    };

    // One triangle list of casters in view space: three float3 positions per
    // triangle, no indices.
    struct CasterBatch
    {
        const float* pPositions = nullptr;
        std::size_t numVertices = 0;
    };

    // A depth map rendered from a directional light around a set of casters,
    // all in the camera's view space (x right, y up, z forward). open() makes
    // the depth texture, the depth-only caster pipeline, the comparison
    // sampler and the vertex buffer; record() fits the light around the union
    // of the batches and draws them into the map. Receivers sample texture()
    // with compareSampler() at the coordinates viewToShadowTexture() maps
    // their view-space position to.
    //
    // Borrows the device, which must outlive it and must already be open for
    // drawing. Not thread-safe.
    class DirectionalShadowMap
    {
    public:
        explicit DirectionalShadowMap(render::Device& device);
        ~DirectionalShadowMap();             // calls close()
        DirectionalShadowMap(const DirectionalShadowMap&) = delete;
        DirectionalShadowMap& operator=(const DirectionalShadowMap&) = delete;

        // Makes the depth texture (D32F, else D24S8, DepthTarget|Sampled), the
        // pipeline, the compare sampler and the vertex buffer. False and one
        // warning when a size is out of range or a resource can't be made.
        [[nodiscard]] bool open(const ShadowMapDesc& desc);

        // Destroys every resource; a safe no-op when not open.
        void close();

        bool isOpen() const;

        // Records the shadow pass for this frame into list (outside a pass):
        // fits the light around the union of all batches' vertices (plus
        // margin), uploads them (growing the vertex buffer as needed, streamed
        // into the list with updateBuffer), clears the map to 1 and draws them
        // depth-only. Returns false and records nothing (one warning only when
        // it starts failing) when not open, the light is invalid, the list is
        // inside a pass, or there are no vertices; the map's contents are then
        // undefined and isValid() is false until the next successful record.
        [[nodiscard]] bool record(render::CommandList& list, const float towardLight[3],
            std::span<const CasterBatch> casters, float margin = 0.05f);

        bool isValid() const;                        // last record() succeeded
        render::TextureHandle texture() const;       // the depth map
        render::SamplerHandle compareSampler() const; // linear comparison sampler (LessOrEqual), clamp
        Matrix4 viewToShadowTexture() const;         // view space -> (u, v, depth) of the map
        std::uint32_t size() const;

    private:
        // Grows the vertex buffer to at least bytes and returns true, or
        // returns false when the device can't hold it.
        [[nodiscard]] bool ensureVertexCapacity(std::uint64_t bytes);

        // Sets isValid() false and logs reason only when the map was not
        // already failing.
        bool failRecord(const char* reason);

        render::Device& m_device;
        render::TextureHandle m_texture;
        render::PipelineHandle m_pipeline;
        render::SamplerHandle m_sampler;
        render::BufferHandle m_vertexBuffer;
        std::uint64_t m_vertexCapacityBytes = 0;
        Matrix4 m_viewToShadowClip{};
        Matrix4 m_viewToShadowTexture{};
        std::uint32_t m_size = 0;
        bool m_open = false;
        bool m_valid = false;
        bool m_failureWarned = false;
    };

}
