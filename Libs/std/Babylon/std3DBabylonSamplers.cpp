// Sampler state of the Babylon std3D backend.
// Part of the Babylon column's std3D backend; see std3DBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <std/General/stdRenderBackend.h>
#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>

#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdConfig.h>
#include <std/General/stdEffect.h>
#include <std/General/stdMath.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "std3DBabylon.h"
#include "stdBabylon.h"
#include "stdRenderBabylon.h"
#include "std3DBabylonInternal.h"

namespace render = babylon::render;
using namespace std3DBabylonInternal;

namespace std3DBabylonInternal
{
    Std3DMipmapFilterType std3D_mipmapFilter = (Std3DMipmapFilterType)-1;
    // Presentation filtering read when std3D_Open runs (std3DBabylon_Read... below):
    // graphics.anisotropicFilter and engine.renderer.texturePixelated.
    int std3D_anisotropicFilter  = J3D_QOL_VALUE(8, 1);
    bool std3D_bTexturePixelated = false;
    render::SamplerHandle std3D_aSamplers[256];                        // std3D_GetSampler's key
}

namespace
{
    // The sampler cache's index of a level; 0 is off and no filtering.
    uint32_t std3D_AnisotropicFilterIndex(int level)
    {
        switch ( std3D_NormalizeAnisotropicFilter(level) )
        {
            case 16: return 4;
            case 8: return 3;
            case 4: return 2;
            case 2: return 1;
            default: return 0;
        }
    }
}

namespace std3DBabylonInternal
{
    // The anisotropy level snapped down to the offered steps (1, 2, 4, 8, 16), so a hand-edited
    // value outside them still maps to the next lower one the display offers. 1 turns it off.
    int std3D_NormalizeAnisotropicFilter(int level)
    {
        if ( level >= 16 )
        {
            return 16;
        }
        if ( level >= 8 )
        {
            return 8;
        }
        if ( level >= 4 )
        {
            return 4;
        }
        if ( level >= 2 )
        {
            return 2;
        }
        return 1;
    }

    // std3DX9.c's sampler state of a render state: linear filtering with STD3D_RS_TEXFILTER_BILINEAR
    // and with STD3D_RS_TEXFILTER_ANISOTROPIC, else point; the mip filter of std3D_SetMipmapFilter
    // (linear for trilinear, else point: render::SamplerDesc has no D3DTEXF_NONE, and textures
    // allocated with STD3D_MIPMAPFILTER_NONE have one level); clamp or wrap in U and V.
    //
    // Two presentation options layer on top: engine.renderer.texturePixelated forces min and mag to
    // nearest while the mip filter stays as configured (the original look), and
    // graphics.anisotropicFilter sets the anisotropy of draws that ask for it. The key below
    // includes both, so a sampler made under one setting is never reused under another.
    render::SamplerHandle std3D_GetSampler(Std3DRenderState state)
    {
        const bool bNearest          = std3D_bTexturePixelated;
        const bool bAnisotropicAsked = (state & STD3D_RS_TEXFILTER_ANISOTROPIC) != 0;
        const bool bLinear           = !bNearest
            && (state & (STD3D_RS_TEXFILTER_BILINEAR | STD3D_RS_TEXFILTER_ANISOTROPIC)) != 0;
        const bool bMipLinear        = std3D_mipmapFilter == STD3D_MIPMAPFILTER_TRILINEAR;
        const bool bClampU           = (state & STD3D_RS_TEX_CPAMP_U) != 0;
        const bool bClampV           = (state & STD3D_RS_TEX_CPAMP_V) != 0;
        const uint32_t anisoIndex    = (!bNearest && bAnisotropicAsked)
            ? std3D_AnisotropicFilterIndex(std3D_anisotropicFilter) : 0u;

        const uint32_t key = (bLinear ? 1u : 0u) | (bMipLinear ? 2u : 0u) | (bClampU ? 4u : 0u)
            | (bClampV ? 8u : 0u) | (anisoIndex << 4) | (bNearest ? 0x80u : 0u);
        render::SamplerHandle& sampler = std3D_aSamplers[key];
        if ( !sampler.isNull() )
        {
            return sampler;
        }

        render::SamplerDesc desc;
        desc.minFilter = bLinear ? render::Filter::Linear : render::Filter::Nearest;
        desc.magFilter = desc.minFilter;
        desc.mipFilter = bMipLinear ? render::Filter::Linear : render::Filter::Nearest;
        desc.addressU  = bClampU ? render::AddressMode::ClampToEdge : render::AddressMode::Repeat;
        desc.addressV  = bClampV ? render::AddressMode::ClampToEdge : render::AddressMode::Repeat;
        desc.maxAnisotropy = anisoIndex
            ? (float)std3D_NormalizeAnisotropicFilter(std3D_anisotropicFilter) : 1.0f;

        sampler = std3D_pRenderDevice->createSampler(desc);
        if ( sampler.isNull() )
        {
            STDLOG_ERROR("Error creating a sampler.\n");
        }

        return sampler;
    }
}

int J3DAPI std3D_SetMipmapFilter(Std3DMipmapFilterType filter)
{
    // Every render device filters mips linearly and by nearest level (render::SamplerDesc), so the
    // filter is never lowered like std3DX9.c did on devices without the caps; 0 is D3D_OK.
    std3D_mipmapFilter = filter;
    return 0;
}
