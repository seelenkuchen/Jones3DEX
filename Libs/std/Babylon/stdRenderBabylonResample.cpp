// The CPU Bgra8 resampling helpers of the Babylon render adapter.
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
    // A channel of a Bgra8 texel, 0..255, as a float.
    float stdRenderBabylon_Channel(const uint8_t* pTexel, size_t channel)
    {
        return (float)pTexel[channel];
    }

    // Resamples a Bgra8 image horizontally from width srcWidth to dstWidth. Downscaling box-filters
    // each destination pixel's source span; upscaling samples bilinearly between texel centers.
    void stdRenderBabylon_ResampleHoriz(const uint8_t* pSrc, uint32_t srcWidth, uint32_t height,
        uint32_t dstWidth, uint8_t* pDst)
    {
        for ( uint32_t y = 0; y < height; ++y )
        {
            const uint8_t* pSrcRow = pSrc + (size_t)y * srcWidth * 4u;
            uint8_t* pDstRow       = pDst + (size_t)y * dstWidth * 4u;
            for ( uint32_t x = 0; x < dstWidth; ++x )
            {
                if ( srcWidth >= dstWidth )
                {
                    uint32_t x0 = (uint32_t)((uint64_t)x * srcWidth / dstWidth);
                    uint32_t x1 = (uint32_t)(((uint64_t)x + 1u) * srcWidth / dstWidth);
                    if ( x1 <= x0 )
                    {
                        x1 = x0 + 1u;
                    }
                    x1 = std::min(x1, srcWidth);

                    uint32_t sum[4] = { 0u, 0u, 0u, 0u };
                    for ( uint32_t sx = x0; sx < x1; ++sx )
                    {
                        const uint8_t* pTexel = pSrcRow + (size_t)sx * 4u;
                        for ( size_t channel = 0; channel < 4u; ++channel )
                        {
                            sum[channel] += (uint32_t)stdRenderBabylon_Channel(pTexel, channel);
                        }
                    }

                    const uint32_t count = x1 - x0;
                    for ( size_t channel = 0; channel < 4u; ++channel )
                    {
                        pDstRow[(size_t)x * 4u + channel] = (uint8_t)((sum[channel] + count / 2u) / count);
                    }
                }
                else
                {
                    float fx = ((float)x + 0.5f) * (float)srcWidth / (float)dstWidth - 0.5f;
                    fx = std::clamp(fx, 0.0f, (float)(srcWidth - 1u));
                    const uint32_t x0 = (uint32_t)fx;
                    const uint32_t x1 = std::min(x0 + 1u, srcWidth - 1u);
                    const float frac  = fx - (float)x0;
                    const uint8_t* pLeft  = pSrcRow + (size_t)x0 * 4u;
                    const uint8_t* pRight = pSrcRow + (size_t)x1 * 4u;
                    for ( size_t channel = 0; channel < 4u; ++channel )
                    {
                        const float value = stdRenderBabylon_Channel(pLeft, channel) * (1.0f - frac)
                            + stdRenderBabylon_Channel(pRight, channel) * frac;
                        pDstRow[(size_t)x * 4u + channel] = (uint8_t)std::lround(value);
                    }
                }
            }
        }
    }

    // Resamples a Bgra8 image vertically from height srcHeight to dstHeight, the same filters as
    // the horizontal pass.
    void stdRenderBabylon_ResampleVert(const uint8_t* pSrc, uint32_t width, uint32_t srcHeight,
        uint32_t dstHeight, uint8_t* pDst)
    {
        for ( uint32_t y = 0; y < dstHeight; ++y )
        {
            uint8_t* pDstRow = pDst + (size_t)y * width * 4u;
            if ( srcHeight >= dstHeight )
            {
                uint32_t y0 = (uint32_t)((uint64_t)y * srcHeight / dstHeight);
                uint32_t y1 = (uint32_t)(((uint64_t)y + 1u) * srcHeight / dstHeight);
                if ( y1 <= y0 )
                {
                    y1 = y0 + 1u;
                }
                y1 = std::min(y1, srcHeight);

                for ( uint32_t x = 0; x < width; ++x )
                {
                    uint32_t sum[4] = { 0u, 0u, 0u, 0u };
                    for ( uint32_t sy = y0; sy < y1; ++sy )
                    {
                        const uint8_t* pTexel = pSrc + ((size_t)sy * width + x) * 4u;
                        for ( size_t channel = 0; channel < 4u; ++channel )
                        {
                            sum[channel] += (uint32_t)stdRenderBabylon_Channel(pTexel, channel);
                        }
                    }

                    const uint32_t count = y1 - y0;
                    for ( size_t channel = 0; channel < 4u; ++channel )
                    {
                        pDstRow[(size_t)x * 4u + channel] = (uint8_t)((sum[channel] + count / 2u) / count);
                    }
                }
            }
            else
            {
                float fy = ((float)y + 0.5f) * (float)srcHeight / (float)dstHeight - 0.5f;
                fy = std::clamp(fy, 0.0f, (float)(srcHeight - 1u));
                const uint32_t y0 = (uint32_t)fy;
                const uint32_t y1 = std::min(y0 + 1u, srcHeight - 1u);
                const float frac  = fy - (float)y0;
                const uint8_t* pTop    = pSrc + (size_t)y0 * width * 4u;
                const uint8_t* pBottom = pSrc + (size_t)y1 * width * 4u;
                for ( uint32_t x = 0; x < width; ++x )
                {
                    for ( size_t channel = 0; channel < 4u; ++channel )
                    {
                        const float value = stdRenderBabylon_Channel(pTop + (size_t)x * 4u, channel) * (1.0f - frac)
                            + stdRenderBabylon_Channel(pBottom + (size_t)x * 4u, channel) * frac;
                        pDstRow[(size_t)x * 4u + channel] = (uint8_t)std::lround(value);
                    }
                }
            }
        }
    }

}

namespace stdRenderBabylonInternal
{
    // Resamples a Bgra8 image on the CPU: box filtering where an axis shrinks, bilinear where it
    // grows. pDst holds dstWidth * dstHeight * 4 bytes.
    void stdRenderBabylon_ResampleBgra(const uint8_t* pSrc, uint32_t srcWidth, uint32_t srcHeight,
        uint8_t* pDst, uint32_t dstWidth, uint32_t dstHeight)
    {
        if ( srcWidth == dstWidth && srcHeight == dstHeight )
        {
            std::memcpy(pDst, pSrc, (size_t)srcWidth * srcHeight * 4u);
            return;
        }

        std::vector<uint8_t> aTemp((size_t)dstWidth * srcHeight * 4u);
        stdRenderBabylon_ResampleHoriz(pSrc, srcWidth, srcHeight, dstWidth, aTemp.data());
        stdRenderBabylon_ResampleVert(aTemp.data(), dstWidth, srcHeight, dstHeight, pDst);
    }

}
