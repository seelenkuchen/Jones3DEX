// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/postfx/Effect.h>

#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>

#include <utility>
#include <vector>

namespace babylon::render
{
    class Device;
}

namespace babylon::postfx
{

    struct VignetteSettings
    {
        float strength = 0.35f;  // 0 (off) to 1: how much the far corners lose
        float inner = 0.45f;     // radius where the darkening starts
        float outer = 1.00f;     // radius where it reaches full strength
    };

    // Darkens the image towards the corners. The radius is measured in a
    // normalized space whose corner is 1 at every aspect ratio. One Ldr pass.
    class VignetteEffect final : public Effect
    {
    public:
        // Clamps strength to 0..1 and raises outer to no less than inner + 0.001.
        void setSettings(const VignetteSettings& settings);
        const VignetteSettings& settings() const;

        std::string_view name() const override;      // "vignette"
        Stage stage() const override;                // Stage::Ldr
        bool active() const override;                // strength > 0
        void releaseResources() override;

    private:
        bool doRecord(PassContext& ctx) override;

        VignetteSettings m_settings;
        // one pipeline per output format, made with the Resources' device on first use
        render::Device* m_device = nullptr;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> m_pipelines;
    };

}
