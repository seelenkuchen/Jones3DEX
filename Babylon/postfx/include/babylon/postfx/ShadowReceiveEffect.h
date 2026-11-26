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

    // What the mask's red >= 0.5 selects. Exclude keeps those pixels' exact input colour, so a
    // caster's own surface receives no shadow; Include lets only those pixels receive, so the
    // same mask can apply a map that contains other casters. Include without a mask selects
    // nothing: the effect is inactive and the frame passes through unchanged.
    enum class MaskMode : std::uint8_t
    {
        Exclude,
        Include,
    };

    // The shadow map for this frame, set before each record(). matrix maps view-space positions
    // (x right, y up, z forward) to (u, v, depth, 1) of the map: row-major float[16], column
    // vectors. towardLight is the view-space direction to the light (used for the normal offset).
    // excludeMask is optional: a frame-sized texture whose red channel selects pixels according
    // to maskMode. The name is historical; in include mode it selects the only pixels that
    // receive, which lets a second map apply to those pixels alone. An invalid map (null texture
    // or sampler, mapSize 0) makes the effect inactive for that frame.
    struct ShadowMapInput
    {
        render::TextureHandle texture;
        render::SamplerHandle compareSampler;
        std::uint32_t mapSize = 0;
        float matrix[16] = {};
        float towardLight[3] = { 0.0f, 1.0f, 0.0f };
        // Optional, any colour format, read at the frame's uv. Null means no mask: no pixel is
        // selected, which leaves Exclude as "everything receives" and Include as "nothing
        // receives" (the effect is then inactive).
        render::TextureHandle excludeMask;
        MaskMode maskMode = MaskMode::Exclude;
    };

    // Darkens receiver pixels where a shadow map says a directional light is blocked. It
    // reconstructs the view-space position from depth like ContactShadowEffect, transforms it
    // through the caller's view-to-shadow-texture matrix and compares it against the map with a
    // comparison sampler over a fixed 12-tap Poisson disc. The receiver test repeats the
    // contact-shadow composite's: pixels whose normals alpha is a class not in receiverClasses
    // keep their exact input colour. With receiverClasses 0xF every pixel receives and the
    // normals target is not needed. An optional mask keeps a shadow caster's own surface out of
    // the receive pass, so a hat's shadow does not fall on the face below it, or in include mode
    // confines a map to those pixels. That lets two maps share one light: the map whose casters
    // are the moving characters excludes them so they do not shadow themselves, while a map
    // whose casters are the static level includes only them. Shadows fade with view distance and
    // near the map border. postfx takes the map as plain inputs and does not depend on the shadow
    // module.
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
        // strength > 0, a valid map and, in include mode, a mask (without one nothing receives).
        bool active() const override;
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
