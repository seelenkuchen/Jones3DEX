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

    class StaticCasterSet;

    // How a DirectionalShadowMap is made.
    struct ShadowMapDesc
    {
        std::uint32_t size = 2048;           // square, 256..8192
        float depthBiasConstant = 1.0f;      // pipeline depth bias for casters
        float depthBiasSlope = 1.5f;

        // View units the light-space depth range reaches toward the light
        // beyond the dynamic casters, so a static caster between the light and
        // them (a ledge above a character) is not clipped. It applies only to
        // a frame that has at least one static batch; a dynamic-only frame
        // keeps the tighter range for the receive effect's normalized-depth
        // bias. The static casters do not enlarge the x/y footprint. The extra
        // range spends depth precision over a larger span, so a D32F map is
        // recommended.
        float staticReach = 20.0f;

        // True: a caster nearer the light than the fitted near plane still
        // occludes, clamped to depth 0. That suits moving casters that must
        // never pop. False: such a caster is clipped, so staticReach is a hard
        // limit on how far toward the light a static caster may be and still
        // cast. That suits a whole level uploaded as static casters, where
        // only geometry close above the characters should count.
        bool clampCastersToNearPlane = true;

        // World-space up axis used by the world-anchored fit (see
        // setViewToWorld()), default +Z. The x/y light basis is built around
        // this fixed axis, so a camera rotation no longer turns the texel
        // grid. It need not be normalized; when it is parallel to the light,
        // another world axis is used. Unused without setViewToWorld().
        float worldUp[3] = { 0.0f, 0.0f, 1.0f };
    };

    // One triangle list of casters: three float3 positions per triangle, no
    // indices. A per-frame batch streams its positions this frame; a static
    // batch names a set already on the GPU and is not uploaded again.
    //
    // A per-frame fitOnly batch contributes its vertices to the light fit but
    // is neither uploaded nor drawn. That fits a map around a set of objects
    // without adding them as casters: for two maps from one light, the map
    // whose casters are the moving characters is fitted around them, while
    // the map of static level geometry is fitted around the characters through
    // a fit-only batch and draws only the static set. A static batch cannot be
    // fit-only.
    struct CasterBatch
    {
        const float* pPositions = nullptr;        // per-frame positions, or
        std::size_t numVertices = 0;
        const StaticCasterSet* pStatic = nullptr; // a static set (pPositions must then be null)
        Matrix4 toView = identity();              // transform of the batch's positions into view space
        bool fitOnly = false;                     // per-frame: fit only, never upload or draw
    };

    // A depth map rendered from a directional light around a set of casters,
    // all in the camera's view space (x right, y up, z forward);
    // setViewToWorld() anchors the light fit in the world instead. open()
    // makes the depth texture, the depth-only caster pipeline, the comparison
    // sampler and the vertex buffer; record() fits the light around the union
    // of the per-frame batches (fit-only ones included), uploads the drawn
    // per-frame ones once and draws every drawn batch (the static ones from
    // their own buffers) into the map. Receivers sample
    // texture() with compareSampler() at the coordinates viewToShadowTexture()
    // maps their view-space position to.
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
        // fits the light around the union of every per-frame batch's vertices
        // (plus margin), including fit-only ones. It uploads only the drawn
        // per-frame vertices (growing the vertex buffer as needed, streamed
        // into the list with updateBuffer; none when every per-frame batch is
        // fit-only), clears the map to 1 and draws every drawn batch depth-only
        // with its own fit * toView uniform, binding a static batch's set
        // buffer without uploading it. A frame whose per-frame batches are all
        // fit-only holds no drawn per-frame vertex: with a static set it draws
        // just that set, and without one it clears the map and records an empty
        // pass. The depth range reaches toward the light by desc.staticReach
        // only when at least one batch is static; a dynamic-only frame keeps
        // the tight range. Returns false and records nothing (one warning only
        // when it starts failing) when not open, the light is invalid, the list
        // is inside a pass, there are no per-frame vertices, or a batch is
        // malformed (including a static batch that is fit-only); the map's
        // contents are then undefined and isValid() is false until the next
        // successful record.
        [[nodiscard]] bool record(render::CommandList& list, const float towardLight[3],
            std::span<const CasterBatch> casters, float margin = 0.05f);

        // Switches record() to the world-anchored fit. viewToWorld maps the
        // view space of record()'s casters and towardLight into the world: the
        // light basis is built from the world-space light direction and
        // desc.worldUp, the casters' view-space bounds are transformed through
        // viewToWorld, and the light-space origin is snapped to whole texels
        // in that fixed basis. A pure camera rotation therefore leaves a fixed
        // caster's light fit unchanged. viewToShadowTexture() and the caster
        // transforms still map from view space, so callers keep passing
        // view-space positions. viewToWorld must be affine (w = 1); its
        // rotation part converts towardLight. Without this call (or after
        // clearViewToWorld()), record() keeps the original view-relative fit
        // exactly.
        void setViewToWorld(const Matrix4& viewToWorld);

        // Returns to the view-relative fit. A safe no-op when not
        // world-anchored.
        void clearViewToWorld();

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
        Matrix4 m_viewToWorld{};
        float m_worldUp[3] = { 0.0f, 0.0f, 1.0f };
        std::uint32_t m_size = 0;
        float m_staticReach = 0.0f;
        bool m_worldAnchored = false;
        bool m_open = false;
        bool m_valid = false;
        bool m_failureWarned = false;
    };

}
