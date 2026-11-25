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

    struct FilmGrainSettings
    {
        float amplitude = 0.035f;  // 0 (off) to 0.25: peak noise added to each channel
    };

    // Adds fine, frame-varying luminance noise that is strongest in the
    // mid-tones. The noise is a hash of the pixel position and the frame
    // index, never of the wall clock. One Ldr pass.
    class FilmGrainEffect final : public Effect
    {
    public:
        // Clamps amplitude to 0..0.25.
        void setSettings(const FilmGrainSettings& settings);
        const FilmGrainSettings& settings() const;

        std::string_view name() const override;      // "film grain"
        Stage stage() const override;                // Stage::Ldr
        bool active() const override;                // amplitude > 0
        void releaseResources() override;

    private:
        bool doRecord(PassContext& ctx) override;

        FilmGrainSettings m_settings;
        // one pipeline per output format, made with the Resources' device on first use
        render::Device* m_device = nullptr;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> m_pipelines;
    };

}
