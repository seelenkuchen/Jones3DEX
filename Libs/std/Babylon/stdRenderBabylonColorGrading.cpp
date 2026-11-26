// The color-grading presets, lookup table and tone-map state of the Babylon render adapter.
// Part of the Babylon column's render frame owner; see stdRenderBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>
#include <babylon/platform/Window.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include <std/General/std.h>
#include <std/General/stdConfig.h>

#include "stdBabylon.h"
#include "stdRenderBabylon.h"
#include "stdRenderBabylonInternal.h"

namespace render = babylon::render;
using namespace stdRenderBabylonInternal;

namespace
{
    // Added: one color of a preset, transformed in place. Temperature is a red/blue gain, saturation
    // scales the distance from luma, contrast blends toward a smoothstep S-curve, and the split-tone
    // tints are weighted toward shadows and highlights with weights that vanish at black and white.
    void stdRenderBabylon_TransformGradingColor(const StdRenderBabylonGradingPreset& preset,
        float* pColor)
    {
        pColor[0] *= 1.0f + preset.temperature;
        pColor[2] *= 1.0f - preset.temperature;

        const float luma = 0.2126f * pColor[0] + 0.7152f * pColor[1] + 0.0722f * pColor[2];
        for ( int channel = 0; channel < 3; ++channel )
        {
            pColor[channel] = luma + (pColor[channel] - luma) * preset.saturation;
        }

        for ( int channel = 0; channel < 3; ++channel )
        {
            const float value   = std::clamp(pColor[channel], 0.0f, 1.0f);
            const float smooth  = value * value * (3.0f - 2.0f * value);
            pColor[channel] = value + (smooth - value) * preset.contrast;
        }

        const float tone = std::clamp(luma, 0.0f, 1.0f);
        const float shadowWeight    = 4.0f * tone * (1.0f - tone) * (1.0f - tone);
        const float highlightWeight = 4.0f * tone * tone * (1.0f - tone);
        for ( int channel = 0; channel < 3; ++channel )
        {
            pColor[channel] += preset.aShadowTint[channel] * shadowWeight
                + preset.aHighlightTint[channel] * highlightWeight;
        }
    }

    // Added: fills the 1024 x 32 Rgba8 strip: slice b at x = b * 32, red on x, green on y.
    void stdRenderBabylon_BuildColorGradingLut(uint32_t preset, std::vector<uint8_t>& rData)
    {
        const StdRenderBabylonGradingPreset& grading =
            stdRenderBabylon_aGradingPresets[std::min(preset, stdRenderBabylon_kMaxColorGrading)];
        const float maxIndex = (float)(stdRenderBabylon_kColorGradingSize - 1);
        rData.resize((size_t)stdRenderBabylon_kColorGradingStripWidth
            * stdRenderBabylon_kColorGradingSize * 4u);

        for ( uint32_t blue = 0; blue < stdRenderBabylon_kColorGradingSize; ++blue )
        {
            for ( uint32_t green = 0; green < stdRenderBabylon_kColorGradingSize; ++green )
            {
                for ( uint32_t red = 0; red < stdRenderBabylon_kColorGradingSize; ++red )
                {
                    float color[3] = { (float)red / maxIndex, (float)green / maxIndex,
                        (float)blue / maxIndex };
                    stdRenderBabylon_TransformGradingColor(grading, color);

                    const size_t x  = (size_t)blue * stdRenderBabylon_kColorGradingSize + red;
                    const size_t at = ((size_t)green * stdRenderBabylon_kColorGradingStripWidth + x) * 4u;
                    rData[at + 0] = stdRenderBabylon_ToUnorm8(color[0]);
                    rData[at + 1] = stdRenderBabylon_ToUnorm8(color[1]);
                    rData[at + 2] = stdRenderBabylon_ToUnorm8(color[2]);
                    rData[at + 3] = 255u;
                }
            }
        }
    }

}

namespace stdRenderBabylonInternal
{
    // Added: makes the grading strip and uploads the current preset when it changed. False, logging
    // why, when the texture or upload is refused. The neutral preset is generated too, so the tone
    // map shader always has its second texture slot bound even though it skips the lookup.
    bool stdRenderBabylon_EnsureColorGradingLut()
    {
        if ( stdRenderBabylon_state.bColorGradingLutFailed )
        {
            return false;
        }

        const uint32_t preset = std::min(stdRenderBabylon_state.toneMap.colorGrading,
            stdRenderBabylon_kMaxColorGrading);
        if ( !stdRenderBabylon_state.colorGradingLut.isNull()
            && stdRenderBabylon_state.colorGradingLutPreset == preset )
        {
            return true;
        }

        if ( stdRenderBabylon_state.colorGradingLut.isNull() )
        {
            render::TextureDesc desc;
            desc.width     = stdRenderBabylon_kColorGradingStripWidth;
            desc.height    = stdRenderBabylon_kColorGradingSize;
            desc.mipLevels = 1;
            desc.format    = render::TextureFormat::Rgba8;
            desc.usage     = render::TextureUsage::Sampled;
            stdRenderBabylon_state.colorGradingLut =
                stdRenderBabylon_state.pDevice->createTexture(desc);
            if ( stdRenderBabylon_state.colorGradingLut.isNull() )
            {
                STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u color-grading lookup.\n",
                    desc.width, desc.height);
                stdRenderBabylon_state.bColorGradingLutFailed = true;
                return false;
            }
        }

        std::vector<uint8_t> aData;
        stdRenderBabylon_BuildColorGradingLut(preset, aData);
        if ( !stdRenderBabylon_state.pDevice->uploadTexture(stdRenderBabylon_state.colorGradingLut,
                0, std::as_bytes(std::span<const uint8_t>(aData))) )
        {
            STDLOG_ERROR("stdRenderBabylon: could not upload the color-grading lookup for preset %u.\n",
                preset);
            stdRenderBabylon_state.bColorGradingLutFailed = true;
            return false;
        }

        stdRenderBabylon_state.colorGradingLutPreset = preset;
        return true;
    }

}

void stdRenderBabylon_SetToneMap(const StdRenderBabylonToneMap& params)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetToneMap: not open.\n");
        return;
    }
    if ( !std::isfinite(params.exposure) || params.exposure < 0.0f )
    {
        STDLOG_ERROR("stdRenderBabylon_SetToneMap: exposure %g is not a finite value at or above 0.\n",
            (double)params.exposure);
        return;
    }
    if ( params.oper > 2 )
    {
        STDLOG_ERROR("stdRenderBabylon_SetToneMap: tone-map operator %u is not supported.\n",
            params.oper);
        return;
    }
    if ( !std::isfinite(params.brightness) || params.brightness <= 0.0f )
    {
        STDLOG_ERROR("stdRenderBabylon_SetToneMap: brightness %g is not a finite value above 0.\n",
            (double)params.brightness);
        return;
    }
    if ( params.colorGrading > stdRenderBabylon_kMaxColorGrading )
    {
        STDLOG_ERROR("stdRenderBabylon_SetToneMap: color-grading preset %u is not supported.\n",
            params.colorGrading);
        return;
    }
    stdRenderBabylon_state.toneMap = params;
}

StdRenderBabylonToneMap stdRenderBabylon_GetToneMap()
{
    return stdRenderBabylon_state.toneMap;
}
