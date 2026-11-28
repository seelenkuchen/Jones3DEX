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
        float strength = 0.7f;       // 0 (off) to 1: how dark a fully blocked pixel gets (1 - strength)
        float length = 0.2f;         // ray length in view-space units
        float thickness = 0.04f;     // a depth sample this far in front of the ray still blocks; farther is a gap
        std::uint32_t steps = 16;    // 4..32 ray-march steps
        std::uint32_t receiverClasses = 0x8; // bit i set: pixels whose normals alpha is class i receive shadow
        float fadeStart = 30.0f;     // view distance where shadows start to fade out
        float fadeEnd = 60.0f;       // view distance where they are gone
        float historyHalfLife = 0.0f; // seconds; 0 = no history (the result is exactly the current behaviour, byte for byte)
    };

    // How the camera moved since the previous recorded frame, for the history reprojection.
    struct CameraMotion
    {
        // Maps a view-space point of this frame (x right, y up, z forward) to the previous frame's view space:
        // previous = R * current + t, rows of R in m[0..2], m[4..6], m[8..10], t in m[3], m[7], m[11].
        float currentToPrevious[12] = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
        bool valid = false;   // false: a cut, a teleport or no previous frame; the history is dropped
    };

    // Darkens pixels whose short path toward a directional light is blocked by nearby geometry in
    // the depth buffer. A pixel whose surface faces away from the light does not receive; the
    // surface normal is reconstructed from depth. The normals target's alpha carries one of four
    // classes 0, 1/3, 2/3, 1 (class = round(alpha * 3)); a pixel receives when its bit is in
    // receiverClasses. With receiverClasses 0xF every pixel receives and the normals target is not
    // needed. The light direction is the direction toward the light, in view space. Three Hdr
    // passes when historyHalfLife is 0: a ray-march mask into a scratch target, a depth-aware 4x4
    // blur of that mask (receiver taps only) into a second scratch target, then the darkened
    // composite into the output. With historyHalfLife above 0 a fourth temporal pass blends the
    // blurred mask with the previous frames' mask, reprojected through the camera motion, into a
    // history texture that the composite reads instead. The composite repeats the receiver test,
    // so non-receiver pixels keep their exact input colour. The ordered jitter and the blur make
    // the result smooth; a blocker at the far end of the ray still gives half shadow. An optional
    // ignore mask takes selected pixels out of the effect: they neither cast nor receive contact
    // shadow.
    class ContactShadowEffect final : public Effect
    {
    public:
        // Clamps strength to 0..1, raises length and thickness to a small positive value, steps to
        // 4..32, fadeEnd to no less than fadeStart and historyHalfLife to 0..1.
        void setSettings(const ContactShadowSettings& settings);
        const ContactShadowSettings& settings() const;

        // The direction toward the light in view space (x right, y up, z forward into the screen).
        // Normalized; a zero vector is ignored with one warning, leaving the previous direction.
        void setLightDirection(float x, float y, float z);

        // An optional frame-sized texture whose red channel selects pixels that take no part in
        // contact shadows: red >= 0.5 means the pixel neither casts nor receives one, so a ray
        // sample that lands on it is a gap and the pixel itself keeps its exact input colour. Null
        // means no mask, which leaves the output exactly as without this call. The effect borrows
        // the texture, which must outlive the passes recorded with it. The mask pass marks a
        // masked pixel as a non-receiver, so the blur already skips it as a tap and writes zero
        // there; the blur and composite need no mask texture of their own.
        void setIgnoreMask(render::TextureHandle mask);

        // The camera motion since the previous recorded frame, used by the history reprojection
        // when historyHalfLife is above 0. Set it before every record(); the effect uses the last
        // value given. An invalid motion makes that frame start the history anew.
        void setCameraMotion(const CameraMotion& motion);

        // Drops the history: the next frame uses only its own mask.
        void resetHistory();

        std::string_view name() const override;      // "contact shadows"
        Stage stage() const override;                // Stage::Hdr
        Needs needs() const override;                // depth, and normals when receiverClasses != 0xF
        bool active() const override;                // strength > 0
        void releaseResources() override;

    private:
        bool doRecord(PassContext& ctx) override;
        // Makes the two history targets at the given size; drops the history when the size changes.
        bool ensureHistoryTextures(render::Device& device, std::uint32_t width,
            std::uint32_t height);

        ContactShadowSettings m_settings;
        float m_lightDirection[3] = { 0.0f, 1.0f, 0.0f };
        render::TextureHandle m_ignoreMask;
        CameraMotion m_cameraMotion;
        // one pipeline per pass and output format, made with the Resources' device on first use
        render::Device* m_device = nullptr;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> m_maskPipelines;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> m_blurPipelines;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> m_temporalPipelines;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> m_compositePipelines;
        // The temporal pass's ping-pong history, Rgba16F at the input size, made with the
        // Resources' device on first use. m_historyReadIndex holds the previous frame's mask;
        // m_historyValid is false until that texture holds one.
        render::TextureHandle m_historyTextures[2];
        std::uint32_t m_historyWidth = 0;
        std::uint32_t m_historyHeight = 0;
        std::uint32_t m_historyReadIndex = 0;
        bool m_historyValid = false;
    };

}
