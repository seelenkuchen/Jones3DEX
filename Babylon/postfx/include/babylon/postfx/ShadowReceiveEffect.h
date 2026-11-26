// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/postfx/Effect.h>

#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace babylon::render
{
    class Device;
}

namespace babylon::postfx
{

    struct ShadowReceiveSettings
    {
        float strength = 0.55f;       // 0 (off) to 1: darkness of a fully shadowed pixel is 1 - strength
        float normalOffset = 0.004f;  // view-space units: push the receiver toward the light (acne)
        float depthBias = 0.0015f;    // shadow-map depth units, subtracted from the receiver depth
        float filterRadius = 1.5f;    // PCF radius in shadow-map texels (0.5..4)
        std::uint32_t receiverClasses = 0x8; // bit i set: pixels whose normals alpha is class i
        float fadeStart = 20.0f;      // view distance where shadows start to fade
        float fadeEnd = 40.0f;        // view distance where they are gone
    };

    // The shadow map for this frame, set before each record(). matrix maps view-space positions
    // (x right, y up, z forward) to (u, v, depth, 1) of the map: row-major float[16], column
    // vectors. towardLight is the view-space direction to the light (used for the normal offset).
    // An invalid map (null texture or sampler, mapSize 0) makes the effect inactive for that frame.
    struct ShadowMapInput
    {
        render::TextureHandle texture;
        render::SamplerHandle compareSampler;
        std::uint32_t mapSize = 0;
        float matrix[16] = {};
        float towardLight[3] = { 0.0f, 1.0f, 0.0f };
    };

    // Darkens receiver pixels where a shadow map says a directional light is blocked. It
    // reconstructs the view-space position from depth like ContactShadowEffect, transforms it
    // through the caller's view-to-shadow-texture matrix and compares it against the map with a
    // comparison sampler over a fixed 12-tap Poisson disc. The receiver test repeats the
    // contact-shadow composite's: pixels whose normals alpha is a class not in receiverClasses
    // keep their exact input colour. With receiverClasses 0xF every pixel receives and the
    // normals target is not needed. Shadows fade with view distance and near the map border.
    // postfx takes the map as plain inputs and does not depend on the shadow module.
    class ShadowReceiveEffect final : public Effect
    {
    public:
        // Clamps strength to 0..1, normalOffset and depthBias to at least 0, filterRadius to
        // 0.5..4 and fadeEnd to no less than fadeStart.
        void setSettings(const ShadowReceiveSettings& settings);
        const ShadowReceiveSettings& settings() const;

        // Copies the map description; the effect borrows the texture and sampler, which must
        // outlive the passes recorded with them.
        void setShadowMap(const ShadowMapInput& shadowMap);
        // Makes the effect inactive until the next setShadowMap().
        void clearShadowMap();

        std::string_view name() const override;      // "shadow receive"
        Stage stage() const override;                // Stage::Hdr
        Needs needs() const override;                // depth, and normals when receiverClasses != 0xF
        bool active() const override;                // strength > 0 and a valid map
        void releaseResources() override;

    private:
        bool doRecord(PassContext& ctx) override;

        ShadowReceiveSettings m_settings;
        ShadowMapInput m_map;
        // one pipeline per output format, made with the Resources' device on first use
        render::Device* m_device = nullptr;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> m_pipelines;
    };

}
