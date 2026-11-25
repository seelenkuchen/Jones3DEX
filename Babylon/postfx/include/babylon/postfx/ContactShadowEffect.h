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

    struct ContactShadowSettings
    {
        float strength = 0.6f;       // 0 (off) to 1: how dark a fully blocked pixel gets (1 - strength)
        float length = 0.12f;        // ray length in view-space units
        float thickness = 0.03f;     // a depth sample this far in front of the ray still blocks; farther is a gap
        std::uint32_t steps = 16;    // 4..32 ray-march steps
        std::uint32_t receiverClasses = 0x8; // bit i set: pixels whose normals alpha is class i receive shadow
        float fadeStart = 30.0f;     // view distance where shadows start to fade out
        float fadeEnd = 60.0f;       // view distance where they are gone
    };

    // Darkens pixels whose short path toward a directional light is blocked by nearby geometry in
    // the depth buffer. The normals target's alpha carries one of four classes 0, 1/3, 2/3, 1
    // (class = round(alpha * 3)); a pixel receives when its bit is in receiverClasses. With
    // receiverClasses 0xF every pixel receives and the normals target is not needed. The light
    // direction is the direction toward the light, in view space. One Hdr pass.
    class ContactShadowEffect final : public Effect
    {
    public:
        // Clamps strength to 0..1, raises length and thickness to a small positive value, steps to
        // 4..32 and fadeEnd to no less than fadeStart.
        void setSettings(const ContactShadowSettings& settings);
        const ContactShadowSettings& settings() const;

        // The direction toward the light in view space (x right, y up, z forward into the screen).
        // Normalized; a zero vector is ignored with one warning, leaving the previous direction.
        void setLightDirection(float x, float y, float z);

        std::string_view name() const override;      // "contact shadows"
        Stage stage() const override;                // Stage::Hdr
        Needs needs() const override;                // depth, and normals when receiverClasses != 0xF
        bool active() const override;                // strength > 0
        void releaseResources() override;

    private:
        bool doRecord(PassContext& ctx) override;

        ContactShadowSettings m_settings;
        float m_lightDirection[3] = { 0.0f, 1.0f, 0.0f };
        // one pipeline per output format, made with the Resources' device on first use
        render::Device* m_device = nullptr;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> m_pipelines;
    };

}
