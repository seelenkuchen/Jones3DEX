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

    // The plain copy: samples input with linear filtering over the whole output, so it also scales.
    // As the Resolve stage it clamps HDR values to 0..1. Always active.
    class CopyEffect final : public Effect
    {
    public:
        std::string_view name() const override;      // "copy"
        Stage stage() const override;                // Stage::Resolve
        bool active() const override;                // true
        void releaseResources() override;

    private:
        bool doRecord(PassContext& ctx) override;

        // one pipeline per output format, made with the Resources' device on first use
        render::Device* m_device = nullptr;
        std::vector<std::pair<render::TextureFormat, render::PipelineHandle>> m_pipelines;
    };

}
