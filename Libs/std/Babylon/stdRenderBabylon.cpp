// Added: the Babylon column's render frame owner (stdRenderBabylon.h). Adapter code: no Fixed/Added
// markers inside.
//
// The back buffer reaches the window through a pass to the swapchain that draws one full-screen
// triangle sampling the color target (Shaders/present.*.hlsl). Babylon's copyTexture can't name the
// swapchain (its null handle means the swapchain only as a pass attachment), so a pass is the only
// way the render interface offers to put an offscreen target on the screen.
//
// A presenting submit runs the post-processing chain first: each pass draws the same full-screen
// triangle with its own fragment shader into one of two targets the chain owns, in the scene color
// format, so an Rgba16F scene keeps HDR values through the passes, reading the output of the pass
// before (the back buffer color for the first) and, as its shader declares, the normal and depth
// targets. The final tone-map pass then samples the last output instead of the back buffer and
// writes the frame (the swapchain, or the target of RenderPresentToTexture). Its shader and
// present.vert are the plain present pass plus exposure and the operator (0 clamp, 1 PBR Neutral,
// 2 ACES fitted); at exposure 1 and operator 0 it clamps to 0..1 and presents the same image as the
// pre-M9.3 present. The back buffer itself is never written by the chain or the final pass.
//
// The scene color target is Rgba16F on a device that can render and sample it, else Bgra8, so
// shaders may write values outside 0..1 for a later tone map. ReadBackBuffer and WriteBackBuffer
// translate between that target and the Bgra8 bytes the game and stdDisplay use.
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

namespace stdRenderBabylonInternal
{
    StdRenderBabylonState stdRenderBabylon_state;
}

namespace
{
    // The render scale's config key (Docs/Jones.cfg.md); read when the frame owner opens.
    constexpr const char* stdRenderBabylon_kRenderScaleKey = "engine.renderer.renderScale";
    constexpr float stdRenderBabylon_kMinRenderScale = 0.5f;
    constexpr float stdRenderBabylon_kMaxRenderScale = 2.0f;

    // TODO: [COMPAT] Screen-space reflections on the water pixels are not built: translucent water
    // does not write depth, so the depth target at a water pixel holds the scene behind it rather
    // than the water plane, and a view-space ray march would start from the wrong depth. Build them
    // once the water plane is available without changing the translucent blend.

    // The scene color target format for this device: Rgba16F where it is a sampled color target,
    // else today's Bgra8.
    render::TextureFormat stdRenderBabylon_ChooseColorFormat(render::Device* pDevice, bool bLegacy)
    {
        const render::TextureUsage usage = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
        if ( !bLegacy && pDevice->supportsFormat(render::TextureFormat::Rgba16F, usage) )
        {
            return render::TextureFormat::Rgba16F;
        }
        if ( !bLegacy )
        {
            STDLOG_STATUS("stdRenderBabylon: the device can't render and sample Rgba16F; the scene color target stays Bgra8.\n");
        }
        return render::TextureFormat::Bgra8;
    }

    // The render scale limited so neither scene dimension exceeds the device's max texture size; the
    // scale the targets are actually made at. Logs once per requested scale when it limits it.
    float stdRenderBabylon_LimitRenderScale(float scale, uint32_t modeWidth, uint32_t modeHeight)
    {
        if ( modeWidth == 0 || modeHeight == 0 )
        {
            return scale;
        }

        const uint32_t maxSize = stdRenderBabylon_state.pDevice->info().limits.maxTextureDimension;
        const float widthScale  = (float)maxSize / (float)modeWidth;
        const float heightScale = (float)maxSize / (float)modeHeight;
        const float limited = std::min(scale, std::min(widthScale, heightScale));
        if ( limited < scale && !stdRenderBabylon_state.bScaleClampLogged )
        {
            STDLOG_WARNING("stdRenderBabylon: render scale %g on a %ux%u display exceeds the device's %u texture limit; using %g.\n",
                (double)scale, modeWidth, modeHeight, maxSize, (double)limited);
            stdRenderBabylon_state.bScaleClampLogged = true;
        }

        return limited;
    }

// Added: rdCamera's model matrix (right, forward, up, position) to the shader camera convention:
// x right, y up, z forward, with each basis column carrying its position component in w.
void stdRenderBabylon_ToShaderCamera(const float aMatrix[12], float* pOut)
{
    pOut[0]  = aMatrix[0];  // right x
    pOut[1]  = aMatrix[1];
    pOut[2]  = aMatrix[2];
    pOut[3]  = aMatrix[9];  // position x
    pOut[4]  = aMatrix[6];  // up x
    pOut[5]  = aMatrix[7];
    pOut[6]  = aMatrix[8];
    pOut[7]  = aMatrix[10]; // position y
    pOut[8]  = aMatrix[3];  // forward x
    pOut[9]  = aMatrix[4];
    pOut[10] = aMatrix[5];
    pOut[11] = aMatrix[11]; // position z
}

}

namespace stdRenderBabylonInternal
{
    // An IEEE 754 half's bits as a float.
    float stdRenderBabylon_HalfToFloat(uint16_t half)
    {
        const uint32_t sign = (uint32_t)(half & 0x8000u) << 16;
        uint32_t exponent  = (half >> 10) & 0x1Fu;
        uint32_t mantissa  = half & 0x3FFu;
        uint32_t bits;
        if ( exponent == 0 )
        {
            if ( mantissa == 0 )
            {
                bits = sign;
            }
            else
            {
                // Subnormal: shift the mantissa up into the float's normal range.
                exponent = 127 - 15 + 1;
                while ( (mantissa & 0x400u) == 0 )
                {
                    mantissa <<= 1;
                    --exponent;
                }
                bits = sign | (exponent << 23) | ((mantissa & 0x3FFu) << 13);
            }
        }
        else if ( exponent == 0x1Fu )
        {
            bits = sign | 0x7F800000u | (mantissa << 13);
        }
        else
        {
            bits = sign | ((exponent + (127 - 15)) << 23) | (mantissa << 13);
        }

        float result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    // A float as an IEEE 754 half's bits, rounding to nearest even.
    uint16_t stdRenderBabylon_FloatToHalf(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        const uint16_t sign      = (uint16_t)((bits >> 16) & 0x8000u);
        const uint32_t exponent  = (bits >> 23) & 0xFFu;
        const uint32_t mantissa  = bits & 0x7FFFFFu;

        if ( exponent == 0xFFu )
        {
            return (uint16_t)(sign | 0x7C00u | (mantissa != 0 ? 0x0200u : 0u));
        }

        const int halfExponent = (int)exponent - 127 + 15;
        if ( halfExponent >= 0x1F )
        {
            return (uint16_t)(sign | 0x7C00u);
        }
        if ( halfExponent <= 0 )
        {
            if ( halfExponent < -10 )
            {
                return sign;
            }
            const uint32_t shift     = (uint32_t)(14 - halfExponent);
            const uint32_t shifted   = (mantissa | 0x800000u) >> shift;
            const uint32_t remainder = (mantissa | 0x800000u) & ((1u << shift) - 1u);
            const uint32_t halfway   = 1u << (shift - 1u);
            uint32_t result          = shifted;
            if ( remainder > halfway || (remainder == halfway && (result & 1u) != 0) )
            {
                ++result;
            }
            return (uint16_t)(sign | result);
        }

        uint32_t result          = ((uint32_t)halfExponent << 10) | (mantissa >> 13);
        const uint32_t remainder = mantissa & 0x1FFFu;
        if ( remainder > 0x1000u || (remainder == 0x1000u && (result & 1u) != 0) )
        {
            ++result;
        }
        return (uint16_t)(sign | result);
    }

    // A float clamped to 0..1 as an 8-bit unorm, rounded to nearest.
    uint8_t stdRenderBabylon_ToUnorm8(float value)
    {
        const float clamped = std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
        return (uint8_t)std::lround(clamped * 255.0f);
    }

    // The final pass's fragment shader: the tone map, or the plain present when a test forces the
    // pre-M9.3 path.
    const render::ShaderDesc& stdRenderBabylon_FinalFrag()
    {
        return stdRenderBabylon_state.bToneMap ? stdBabylon::shaders::tonemapFrag()
                                               : stdBabylon::shaders::presentFrag();
    }

    void stdRenderBabylon_DestroyDeferred()
    {
        for ( const render::TextureHandle texture : stdRenderBabylon_state.aDeferredTextures )
        {
            stdRenderBabylon_state.pDevice->destroyTexture(texture);
        }

        stdRenderBabylon_state.aDeferredTextures.clear();

        for ( const render::BufferHandle buffer : stdRenderBabylon_state.aDeferredBuffers )
        {
            stdRenderBabylon_state.pDevice->destroyBuffer(buffer);
        }

        stdRenderBabylon_state.aDeferredBuffers.clear();
    }

    // The nearest-filtering, clamping sampler of the present and post passes, made on first use.
    bool stdRenderBabylon_EnsurePresentSampler()
    {
        if ( !stdRenderBabylon_state.presentSampler.isNull() )
        {
            return true;
        }

        render::SamplerDesc samplerDesc;
        samplerDesc.minFilter = render::Filter::Nearest;
        samplerDesc.magFilter = render::Filter::Nearest;
        samplerDesc.addressU  = render::AddressMode::ClampToEdge;
        samplerDesc.addressV  = render::AddressMode::ClampToEdge;
        stdRenderBabylon_state.presentSampler = stdRenderBabylon_state.pDevice->createSampler(samplerDesc);
        return !stdRenderBabylon_state.presentSampler.isNull();
    }

    // The linear, clamping sampler of the final pass at a render scale other than 1, made on first use.
    bool stdRenderBabylon_EnsureFinalSampler()
    {
        if ( !stdRenderBabylon_state.finalSampler.isNull() )
        {
            return true;
        }

        render::SamplerDesc samplerDesc;
        samplerDesc.minFilter = render::Filter::Linear;
        samplerDesc.magFilter = render::Filter::Linear;
        samplerDesc.addressU  = render::AddressMode::ClampToEdge;
        samplerDesc.addressV  = render::AddressMode::ClampToEdge;
        stdRenderBabylon_state.finalSampler = stdRenderBabylon_state.pDevice->createSampler(samplerDesc);
        return !stdRenderBabylon_state.finalSampler.isNull();
    }

    // True when list has a pass with the back buffer's color target as an attachment or a copy into it.
    bool stdRenderBabylon_DrawsBackBuffer(const render::CommandList& list)
    {
        const render::TextureHandle colorTarget = stdRenderBabylon_state.colorTarget;
        if ( colorTarget.isNull() )
        {
            return false;
        }

        for ( const render::Command& command : list.commands() )
        {
            if ( const render::BeginPass* pBegin = std::get_if<render::BeginPass>(&command) )
            {
                for ( const render::ColorAttachment& attachment : pBegin->desc.colorTargets )
                {
                    if ( attachment.target == colorTarget )
                    {
                        return true;
                    }
                }
            }
            else if ( const render::CopyTexture* pCopy = std::get_if<render::CopyTexture>(&command) )
            {
                if ( pCopy->destination == colorTarget )
                {
                    return true;
                }
            }
        }

        return false;
    }

}

bool stdRenderBabylon_TakeRenderOption(int* pArgc, char** argv, StdRenderBabylonDeviceType* pType)
{
    if ( !pArgc || !argv || !pType )
    {
        return false;
    }

    // Validate every pair first, so a bad one changes nothing.
    StdRenderBabylonDeviceType type = *pType;
    for ( int i = 1; i < *pArgc; ++i )
    {
        if ( std::strcmp(argv[i], "--render") != 0 )
        {
            continue;
        }

        if ( i + 1 >= *pArgc )
        {
            return false;
        }

        const char* pValue = argv[++i];
        if ( std::strcmp(pValue, "null") == 0 )
        {
            type = StdRenderBabylonDeviceType::Null;
        }
        else if ( std::strcmp(pValue, "gpu") == 0 )
        {
            type = StdRenderBabylonDeviceType::Gpu;
        }
        else
        {
            return false;
        }
    }

    int numKept = 1;
    for ( int i = 1; i < *pArgc; ++i )
    {
        if ( std::strcmp(argv[i], "--render") == 0 )
        {
            ++i;
            continue;
        }

        argv[numKept++] = argv[i];
    }

    argv[numKept] = nullptr;
    *pArgc        = numKept;
    *pType        = type;
    return true;
}

bool stdRenderBabylon_Open(render::Device* pDevice)
{
    if ( !pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_Open: no render device.\n");
        return false;
    }

    if ( stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_Open: already open.\n");
        return false;
    }

    stdRenderBabylon_state             = StdRenderBabylonState{};
    stdRenderBabylon_state.pDevice     = pDevice;
    // Added: the render scale is presentation state read when the display opens; a display restart
    // (the pattern for the other Display options) reopens the frame owner and re-reads it.
    if ( stdConfig_HasStarted() )
    {
        const float scale = stdConfig_GetFloat(stdRenderBabylon_kRenderScaleKey, 1.0f);
        if ( std::isfinite(scale) )
        {
            stdRenderBabylon_state.renderScale = std::clamp(scale,
                stdRenderBabylon_kMinRenderScale, stdRenderBabylon_kMaxRenderScale);
        }

        // Added: SMAA 1x is on by default in Enhanced builds and off in Retail/QOL-off ones.
        stdRenderBabylon_state.bAntiAliasing =
            stdConfig_GetInt(stdRenderBabylon_kAntiAliasingKey, J3D_QOL_VALUE(1, 0)) != 0;

        // Added: sharpening defaults to 0.3 (the Options row's Low) in Enhanced builds and off in
        // Retail/QOL-off ones.
        const float sharpening = stdConfig_GetFloat(stdRenderBabylon_kSharpeningKey,
            J3D_QOL_VALUE(0.3f, 0.0f));
        if ( std::isfinite(sharpening) )
        {
            stdRenderBabylon_state.sharpening = std::clamp(sharpening, 0.0f,
                stdRenderBabylon_kMaxSharpening);
        }

        // Added: Neutral (1) is the Enhanced default, clamp (0) the Retail/QOL-off one. An
        // unknown operator keeps the default rather than failing the display.
        const int toneMapping = stdConfig_GetInt(stdRenderBabylon_kToneMappingKey,
            J3D_QOL_VALUE(1, 0));
        if ( toneMapping >= 0 && toneMapping <= 2 )
        {
            stdRenderBabylon_state.toneMap.oper = (uint32_t)toneMapping;
        }

        const float exposure = stdConfig_GetFloat(stdRenderBabylon_kExposureKey, 1.0f);
        if ( std::isfinite(exposure) )
        {
            stdRenderBabylon_state.toneMap.exposure = std::clamp(exposure,
                stdRenderBabylon_kMinExposure, stdRenderBabylon_kMaxExposure);
        }

        // Added: brightness is the calibration gamma after the operator; 1.0 is the identity and
        // leaves the presented image unchanged.
        const float brightness = stdConfig_GetFloat(stdRenderBabylon_kBrightnessKey, 1.0f);
        if ( std::isfinite(brightness) )
        {
            stdRenderBabylon_state.toneMap.brightness = std::clamp(brightness,
                stdRenderBabylon_kMinBrightness, stdRenderBabylon_kMaxBrightness);
        }

        // Added: color grading is presentation only and Neutral (0) in every build, so the default
        // image is unchanged. An unknown preset keeps Neutral rather than failing the display.
        const int colorGrading = stdConfig_GetInt(stdRenderBabylon_kColorGradingKey, 0);
        if ( colorGrading >= 0 && (uint32_t)colorGrading <= stdRenderBabylon_kMaxColorGrading )
        {
            stdRenderBabylon_state.toneMap.colorGrading = (uint32_t)colorGrading;
        }

        // Added: bloom is Low (1) by default in Enhanced builds and off in Retail/QOL-off ones. An
        // unknown level is clamped to the highest one.
        const int bloom = std::clamp(stdConfig_GetInt(stdRenderBabylon_kBloomKey,
            J3D_QOL_VALUE(1, 0)), 0, stdRenderBabylon_kBloomMaxLevel);
        stdRenderBabylon_state.bloom = stdRenderBabylon_aBloomStrength[bloom];

        // Added: depth of field is on by default in Enhanced builds and off in Retail/QOL-off ones,
        // with a separate row for cutscenes and for gameplay. The strength clamps to 0.5..2.0.
        stdRenderBabylon_state.bDofCutscenes =
            stdConfig_GetBool(stdRenderBabylon_kDofCutscenesKey, J3D_QOL_VALUE(1, 0) != 0);
        stdRenderBabylon_state.bDofGameplay =
            stdConfig_GetBool(stdRenderBabylon_kDofGameplayKey, J3D_QOL_VALUE(1, 0) != 0);
        stdRenderBabylon_state.dofStrength = std::clamp(stdConfig_GetFloat(
            stdRenderBabylon_kDofStrengthKey, 1.0f), stdRenderBabylon_kMinDofStrength,
            stdRenderBabylon_kMaxDofStrength);

        // Added: light shafts are Low (1) by default in Enhanced builds and off in Retail/QOL-off
        // ones. An unknown level is clamped to the highest one.
        stdRenderBabylon_state.lightShafts = std::clamp(stdConfig_GetInt(
            stdRenderBabylon_kLightShaftsKey, J3D_QOL_VALUE(1, 0)),
            0, stdRenderBabylon_kLightShaftsMaxLevel);
        stdRenderBabylon_state.lightShaftStrength =
            stdRenderBabylon_aLightShaftStrength[stdRenderBabylon_state.lightShafts];

        // Added: M9.18. Lens flare is on by default in Enhanced builds and off without QOL; the
        // vignette and film grain are off in every build. All are read when the display opens.
        stdRenderBabylon_state.bLensFlare =
            stdConfig_GetBool(stdRenderBabylon_kLensFlareKey, J3D_QOL_VALUE(1, 0) != 0);
        stdRenderBabylon_state.bVignette =
            stdConfig_GetBool(stdRenderBabylon_kVignetteKey, false);
        stdRenderBabylon_state.bFilmGrain =
            stdConfig_GetBool(stdRenderBabylon_kFilmGrainKey, false);

        // Added: water ripples are on by default in Enhanced builds and off in Retail/QOL-off ones.
        stdRenderBabylon_state.bWaterRipples =
            stdConfig_GetBool(stdRenderBabylon_kWaterRipplesKey, J3D_QOL_VALUE(1, 0) != 0);

        // Added: contact shadows are on by default in Enhanced builds and off in Retail/QOL-off
        // ones, after the water ripples and before ambient occlusion.
        stdRenderBabylon_state.bContactShadows =
            stdConfig_GetBool(stdRenderBabylon_kContactShadowsKey, J3D_QOL_VALUE(1, 0) != 0);

        // Added: character shadows are on by default in Enhanced builds and off in Retail/QOL-off
        // ones, after the contact shadows and before ambient occlusion.
        stdRenderBabylon_state.bShadows =
            stdConfig_GetBool(stdRenderBabylon_kShadowsKey, J3D_QOL_VALUE(1, 0) != 0);

        // Added: ambient occlusion is Low (1) by default in Enhanced builds and off in
        // Retail/QOL-off ones. An unknown level is clamped to the highest one.
        stdRenderBabylon_state.ssao = std::clamp(stdConfig_GetInt(
            stdRenderBabylon_kSsaoKey, J3D_QOL_VALUE(1, 0)), 0,
            stdRenderBabylon_kSsaoMaxLevel);

        // Added: camera motion blur is Low (1) by default in Enhanced builds and off in
        // Retail/QOL-off ones. An unknown level is clamped to the highest one.
        stdRenderBabylon_state.motionBlur = std::clamp(stdConfig_GetInt(
            stdRenderBabylon_kMotionBlurKey, J3D_QOL_VALUE(1, 0)), 0,
            stdRenderBabylon_kMotionBlurMaxLevel);
    }
#ifdef J3D_TEST
    stdRenderBabylon_state.bToneMap = !stdRenderBabylon_bLegacyPresentationForTest;
    stdRenderBabylon_bLegacyPresentationForTest = false;
    if ( !stdRenderBabylon_state.bToneMap )
    {
        // The pre-M9.3 path has no LDR image for the anti-aliasing stage to read.
        stdRenderBabylon_state.bAntiAliasing = false;
    }
#endif
    stdRenderBabylon_state.colorFormat = stdRenderBabylon_ChooseColorFormat(pDevice,
        !stdRenderBabylon_state.bToneMap);
    stdRenderBabylon_state.depthFormat = pDevice->supportsFormat(render::TextureFormat::D24S8, render::TextureUsage::DepthTarget)
        ? render::TextureFormat::D24S8
        : render::TextureFormat::D32F;

    // The depth format stays the one the scene always used; it is only also sampled where it can be.
    stdRenderBabylon_state.bDepthSampled = pDevice->supportsFormat(stdRenderBabylon_state.depthFormat,
        render::TextureUsage::DepthTarget | render::TextureUsage::Sampled);

    // R10G10B10A2 holds a normal's components with 10 bits each in 4 bytes; Rgba8 where the device
    // can't render to it.
    const render::TextureUsage normalUsage = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
    stdRenderBabylon_state.normalFormat = pDevice->supportsFormat(render::TextureFormat::R10G10B10A2, normalUsage)
        ? render::TextureFormat::R10G10B10A2
        : render::TextureFormat::Rgba8;

    const render::DeviceInfo& info = pDevice->info();
    if ( info.hasSwapchain )
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdRenderBabylon_FinalFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ info.swapchainFormat, {} } };

        stdRenderBabylon_state.presentPipeline = pDevice->createPipeline(desc);
        if ( !stdRenderBabylon_state.presentPipeline.isNull() )
        {
            stdRenderBabylon_state.hudPipeline =
                stdRenderBabylon_CreateHudPipeline(info.swapchainFormat);
        }
        if ( stdRenderBabylon_state.presentPipeline.isNull() || stdRenderBabylon_state.hudPipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler() || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon_Open: could not create the final pass pipeline.\n");
            pDevice->destroyPipeline(stdRenderBabylon_state.presentPipeline);
            pDevice->destroyPipeline(stdRenderBabylon_state.hudPipeline);
            pDevice->destroySampler(stdRenderBabylon_state.presentSampler);
            pDevice->destroySampler(stdRenderBabylon_state.finalSampler);
            stdRenderBabylon_state = StdRenderBabylonState{};
            return false;
        }
    }

    // Added: the Babylon postfx objects the vignette and film grain draw with. A failure logs and
    // leaves the effects unavailable; it does not fail the open.
    static_cast<void>(stdRenderBabylon_OpenPostfx());

    return true;
}

void stdRenderBabylon_Close()
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        return;
    }

    // Added: release the camera effects' postfx objects before the device goes.
    stdRenderBabylon_ClosePostfx();

    stdRenderBabylon_state.frame.reset();
    stdRenderBabylon_ClearPostPasses();
    stdRenderBabylon_FreeBackBuffer();
    stdRenderBabylon_DestroyDeferred();

    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.presentPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.capturePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.hudPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.hudCapturePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.smaaToneMapPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.smaaEdgePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.smaaWeightsPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.smaaBlendPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.smaaBlendCapturePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.smaaBlendCasPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.casToneMapPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.casPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.casCapturePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.bloomPrefilterPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.bloomDownsamplePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.bloomUpsamplePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.bloomCompositePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.dofGatherPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.dofCompositePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.shaftMaskPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.shaftReducePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.shaftStrengthPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.shaftBlurPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.shaftCompositePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.flarePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.fxToneMapPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.waterRipplesPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.ssaoPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.ssaoBlurPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.ssaoCompositePipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.motionBlurPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.shadowMaskPipeline);
    stdRenderBabylon_state.pDevice->destroyPipeline(stdRenderBabylon_state.shadowMaskLitPipeline);
    stdRenderBabylon_state.pDevice->destroyTexture(stdRenderBabylon_state.smaaAreaTexture);
    stdRenderBabylon_state.pDevice->destroyTexture(stdRenderBabylon_state.smaaSearchTexture);
    stdRenderBabylon_state.pDevice->destroyTexture(stdRenderBabylon_state.colorGradingLut);
    stdRenderBabylon_state.pDevice->destroySampler(stdRenderBabylon_state.presentSampler);
    stdRenderBabylon_state.pDevice->destroySampler(stdRenderBabylon_state.finalSampler);
    stdRenderBabylon_state = StdRenderBabylonState{};
}

bool stdRenderBabylon_IsOpen()
{
    return stdRenderBabylon_state.pDevice != nullptr;
}

render::Device* stdRenderBabylon_GetDevice()
{
    return stdRenderBabylon_state.pDevice;
}

void stdRenderBabylon_FreeBackBuffer()
{
    stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.colorTarget);
    stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.depthTarget);
    stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.normalTarget);
    stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.hudColorTarget);
    stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.hudNormalTarget);
    stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.hudDepthTarget);
    stdRenderBabylon_FreePostTargets();
    stdRenderBabylon_FreeSmaaTargets();
    stdRenderBabylon_FreeCasTarget();
    stdRenderBabylon_FreeBloomTargets();
    stdRenderBabylon_FreeDoFTargets();
    stdRenderBabylon_FreeWaterTargets();
    stdRenderBabylon_FreeContactShadowTargets();
    stdRenderBabylon_FreeShadowTargets();
    stdRenderBabylon_FreeSsaoTargets();
    stdRenderBabylon_FreeMotionBlurTarget();
    stdRenderBabylon_FreeShaftTargets();
    stdRenderBabylon_FreeFlareTargets();
    stdRenderBabylon_FreeFxTarget();
    stdRenderBabylon_state.colorTarget     = {};
    stdRenderBabylon_state.depthTarget     = {};
    stdRenderBabylon_state.normalTarget    = {};
    stdRenderBabylon_state.hudColorTarget  = {};
    stdRenderBabylon_state.hudNormalTarget = {};
    stdRenderBabylon_state.hudDepthTarget  = {};
    stdRenderBabylon_state.bHudLayerDrawn  = false;
    stdRenderBabylon_state.modeWidth    = 0;
    stdRenderBabylon_state.modeHeight   = 0;
    stdRenderBabylon_state.width        = 0;
    stdRenderBabylon_state.height       = 0;
    ++stdRenderBabylon_state.backBufferVersion;
}

bool stdRenderBabylon_SetBackBufferSize(uint32_t width, uint32_t height, bool* pbRecreated)
{
    if ( pbRecreated )
    {
        *pbRecreated = false;
    }

    render::Device* pDevice = stdRenderBabylon_state.pDevice;
    if ( !pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetBackBufferSize: not open.\n");
        return false;
    }

    const uint32_t maxSize = pDevice->info().limits.maxTextureDimension;
    if ( width == 0 || height == 0 || width > maxSize || height > maxSize )
    {
        stdRenderBabylon_FreeBackBuffer();
        STDLOG_ERROR("stdRenderBabylon_SetBackBufferSize: invalid back buffer size %ux%u (limit %u).\n",
            width, height, maxSize);
        return false;
    }

    const float renderScale = stdRenderBabylon_LimitRenderScale(stdRenderBabylon_state.renderScale,
        width, height);
    const uint32_t renderWidth  = std::max(1u, (uint32_t)std::lround((float)width * renderScale));
    const uint32_t renderHeight = std::max(1u, (uint32_t)std::lround((float)height * renderScale));

    if ( !stdRenderBabylon_state.colorTarget.isNull()
        && stdRenderBabylon_state.modeWidth == width
        && stdRenderBabylon_state.modeHeight == height
        && stdRenderBabylon_state.width == renderWidth
        && stdRenderBabylon_state.height == renderHeight )
    {
        return true;
    }

    stdRenderBabylon_FreeBackBuffer();

    render::TextureDesc colorDesc;
    colorDesc.width     = renderWidth;
    colorDesc.height    = renderHeight;
    colorDesc.mipLevels = 1;
    colorDesc.format    = stdRenderBabylon_state.colorFormat;
    colorDesc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

    render::TextureDesc depthDesc;
    depthDesc.width     = renderWidth;
    depthDesc.height    = renderHeight;
    depthDesc.mipLevels = 1;
    depthDesc.format    = stdRenderBabylon_state.depthFormat;
    depthDesc.usage     = stdRenderBabylon_state.bDepthSampled ? render::TextureUsage::DepthTarget | render::TextureUsage::Sampled : render::TextureUsage::DepthTarget;

    render::TextureDesc normalDesc = colorDesc;
    normalDesc.format = stdRenderBabylon_state.normalFormat;

    // Added: the HUD layer stays at the display size, so the HUD is sharp at any render scale. Its
    // color is sampled by the composite and its normal target is a same-format dummy the HUD draws
    // write because their shaders output a normal.
    render::TextureDesc hudColorDesc = colorDesc;
    hudColorDesc.width  = width;
    hudColorDesc.height = height;
    hudColorDesc.usage  = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
    render::TextureDesc hudNormalDesc = hudColorDesc;
    hudNormalDesc.format = stdRenderBabylon_state.normalFormat;
    hudNormalDesc.usage  = render::TextureUsage::ColorTarget;
    render::TextureDesc hudDepthDesc = depthDesc;
    hudDepthDesc.width  = width;
    hudDepthDesc.height = height;
    hudDepthDesc.usage  = render::TextureUsage::DepthTarget;

    const render::TextureHandle colorTarget  = pDevice->createTexture(colorDesc);
    const render::TextureHandle depthTarget  = pDevice->createTexture(depthDesc);
    const render::TextureHandle normalTarget = pDevice->createTexture(normalDesc);
    const render::TextureHandle hudColorTarget  = pDevice->createTexture(hudColorDesc);
    const render::TextureHandle hudNormalTarget = pDevice->createTexture(hudNormalDesc);
    const render::TextureHandle hudDepthTarget  = pDevice->createTexture(hudDepthDesc);
    if ( colorTarget.isNull() || depthTarget.isNull() || normalTarget.isNull()
        || hudColorTarget.isNull() || hudNormalTarget.isNull() || hudDepthTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_SetBackBufferSize: could not create the %ux%u back buffer (render scale %g).\n",
            width, height, (double)renderScale);
        pDevice->destroyTexture(colorTarget);
        pDevice->destroyTexture(depthTarget);
        pDevice->destroyTexture(normalTarget);
        pDevice->destroyTexture(hudColorTarget);
        pDevice->destroyTexture(hudNormalTarget);
        pDevice->destroyTexture(hudDepthTarget);
        return false;
    }

    stdRenderBabylon_state.colorTarget     = colorTarget;
    stdRenderBabylon_state.depthTarget     = depthTarget;
    stdRenderBabylon_state.normalTarget    = normalTarget;
    stdRenderBabylon_state.hudColorTarget  = hudColorTarget;
    stdRenderBabylon_state.hudNormalTarget = hudNormalTarget;
    stdRenderBabylon_state.hudDepthTarget  = hudDepthTarget;
    stdRenderBabylon_state.bHudLayerDrawn  = false;
    stdRenderBabylon_state.modeWidth     = width;
    stdRenderBabylon_state.modeHeight    = height;
    stdRenderBabylon_state.width         = renderWidth;
    stdRenderBabylon_state.height        = renderHeight;
    ++stdRenderBabylon_state.backBufferVersion; // undefined until something clears or uploads it
    if ( pbRecreated )
    {
        *pbRecreated = true;
    }

    return true;
}

bool stdRenderBabylon_ReadBackBuffer(void* pOut, uint32_t* pWidth, uint32_t* pHeight)
{
    if ( pWidth )
    {
        *pWidth = 0;
    }

    if ( pHeight )
    {
        *pHeight = 0;
    }

    if ( !pOut )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: no destination buffer.\n");
        return false;
    }

    if ( !stdRenderBabylon_state.pDevice || stdRenderBabylon_state.colorTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: no back buffer.\n");
        return false;
    }

    const uint32_t renderWidth  = stdRenderBabylon_state.width;
    const uint32_t renderHeight = stdRenderBabylon_state.height;
    const uint32_t modeWidth    = stdRenderBabylon_state.modeWidth;
    const uint32_t modeHeight   = stdRenderBabylon_state.modeHeight;
    const uint64_t numRenderPixels = (uint64_t)renderWidth * (uint64_t)renderHeight;
    const uint64_t readSize        = numRenderPixels * render::bytesPerTexel(stdRenderBabylon_state.colorFormat);
    const uint64_t size            = (uint64_t)modeWidth * (uint64_t)modeHeight * 4u;
    if ( size > (uint64_t)SIZE_MAX || readSize > (uint64_t)SIZE_MAX )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: the %ux%u back buffer is too large.\n", modeWidth, modeHeight);
        return false;
    }

    if ( !stdRenderBabylon_FlushFrame() )
    {
        return false;
    }

    const std::vector<std::byte> pixels = stdRenderBabylon_state.pDevice->readTexture(stdRenderBabylon_state.colorTarget, 0);
    if ( pixels.size() != (size_t)readSize )
    {
        STDLOG_ERROR("stdRenderBabylon_ReadBackBuffer: the device returned %zu bytes for a %ux%u buffer in its scene format.\n",
            pixels.size(), renderWidth, renderHeight);
        return false;
    }

    // The scene target as Bgra8 at its own size.
    std::vector<uint8_t> aScaled;
    const uint8_t* pScaled = nullptr;
    if ( stdRenderBabylon_state.colorFormat == render::TextureFormat::Bgra8 )
    {
        pScaled = reinterpret_cast<const uint8_t*>(pixels.data());
    }
    else
    {
        // Rgba16F: half floats R, G, B, A to Bgra8, clamping to the displayable range.
        aScaled.resize((size_t)numRenderPixels * 4u);
        for ( uint64_t i = 0; i < numRenderPixels; ++i )
        {
            const std::byte* pTexel = pixels.data() + i * 8u;
            uint16_t aHalf[4];
            for ( size_t channel = 0; channel < 4u; ++channel )
            {
                aHalf[channel] = (uint16_t)((uint8_t)pTexel[channel * 2u]
                    | ((uint16_t)(uint8_t)pTexel[channel * 2u + 1u] << 8));
            }
            const float aChannel[4] = { stdRenderBabylon_HalfToFloat(aHalf[0]),
                stdRenderBabylon_HalfToFloat(aHalf[1]),
                stdRenderBabylon_HalfToFloat(aHalf[2]),
                stdRenderBabylon_HalfToFloat(aHalf[3]) };
            aScaled[i * 4u + 0u] = stdRenderBabylon_ToUnorm8(aChannel[2]);
            aScaled[i * 4u + 1u] = stdRenderBabylon_ToUnorm8(aChannel[1]);
            aScaled[i * 4u + 2u] = stdRenderBabylon_ToUnorm8(aChannel[0]);
            aScaled[i * 4u + 3u] = stdRenderBabylon_ToUnorm8(aChannel[3]);
        }
        pScaled = aScaled.data();
    }

    uint8_t* const pDest = static_cast<uint8_t*>(pOut);
    stdRenderBabylon_ResampleBgra(pScaled, renderWidth, renderHeight, pDest, modeWidth, modeHeight);

    if ( pWidth )
    {
        *pWidth = modeWidth;
    }

    if ( pHeight )
    {
        *pHeight = modeHeight;
    }

    return true;
}

render::TextureHandle stdRenderBabylon_GetColorTarget()
{
    return stdRenderBabylon_state.colorTarget;
}

render::TextureHandle stdRenderBabylon_GetDepthTarget()
{
    return stdRenderBabylon_state.depthTarget;
}

render::TextureFormat stdRenderBabylon_GetDepthFormat()
{
    return stdRenderBabylon_state.depthFormat;
}

render::TextureFormat stdRenderBabylon_GetColorFormat()
{
    return stdRenderBabylon_state.colorFormat;
}

render::TextureHandle stdRenderBabylon_GetNormalTarget()
{
    return stdRenderBabylon_state.normalTarget;
}

render::TextureFormat stdRenderBabylon_GetNormalFormat()
{
    return stdRenderBabylon_state.normalFormat;
}

render::TextureHandle stdRenderBabylon_GetHudColorTarget()
{
    return stdRenderBabylon_state.hudColorTarget;
}

render::TextureHandle stdRenderBabylon_GetHudNormalTarget()
{
    return stdRenderBabylon_state.hudNormalTarget;
}

render::TextureHandle stdRenderBabylon_GetHudDepthTarget()
{
    return stdRenderBabylon_state.hudDepthTarget;
}

uint32_t stdRenderBabylon_GetBackBufferWidth()
{
    return stdRenderBabylon_state.modeWidth;
}

uint32_t stdRenderBabylon_GetBackBufferHeight()
{
    return stdRenderBabylon_state.modeHeight;
}

uint32_t stdRenderBabylon_GetRenderWidth()
{
    return stdRenderBabylon_state.width;
}

uint32_t stdRenderBabylon_GetRenderHeight()
{
    return stdRenderBabylon_state.height;
}

bool stdRenderBabylon_SetRenderScale(float scale)
{
    if ( !std::isfinite(scale) )
    {
        STDLOG_ERROR("stdRenderBabylon_SetRenderScale: render scale %g is not finite.\n", (double)scale);
        return false;
    }

    const float clamped = std::clamp(scale, stdRenderBabylon_kMinRenderScale,
        stdRenderBabylon_kMaxRenderScale);
    if ( clamped != scale )
    {
        STDLOG_STATUS("stdRenderBabylon: render scale %g is outside %g..%g; using %g.\n",
            (double)scale, (double)stdRenderBabylon_kMinRenderScale,
            (double)stdRenderBabylon_kMaxRenderScale, (double)clamped);
    }

    if ( stdRenderBabylon_state.renderScale == clamped )
    {
        return true;
    }

    stdRenderBabylon_state.renderScale        = clamped;
    stdRenderBabylon_state.bScaleClampLogged  = false;
    if ( stdRenderBabylon_state.pDevice && !stdRenderBabylon_state.colorTarget.isNull()
        && !stdRenderBabylon_state.frame )
    {
        // Recreate at the new scale now; while a frame is being recorded the next SetBackBufferSize
        // applies it, so the frame's recorded targets stay valid until it is submitted.
        stdRenderBabylon_SetBackBufferSize(stdRenderBabylon_state.modeWidth,
            stdRenderBabylon_state.modeHeight, nullptr);
    }

    return true;
}

float stdRenderBabylon_GetRenderScale()
{
    return stdRenderBabylon_state.renderScale;
}

void stdRenderBabylon_SetCameraTransform(const float aMatrix[12], bool bCut)
{
    if ( !stdRenderBabylon_state.pDevice || !aMatrix )
    {
        return;
    }

    for ( int i = 0; i < 12; ++i )
    {
        if ( !std::isfinite(aMatrix[i]) )
        {
            return;
        }
    }

    float aNew[12];
    stdRenderBabylon_ToShaderCamera(aMatrix, aNew);

    bool bReset = bCut || !stdRenderBabylon_state.bHasCameraTransform
        || stdRenderBabylon_state.shaftDeltaTime <= 0.0f;
    if ( !bReset )
    {
        const float dx = aNew[3] - stdRenderBabylon_state.aLastCameraMatrix[3];
        const float dy = aNew[7] - stdRenderBabylon_state.aLastCameraMatrix[7];
        const float dz = aNew[11] - stdRenderBabylon_state.aLastCameraMatrix[11];
        if ( dx * dx + dy * dy + dz * dz
            > stdRenderBabylon_kMotionBlurTeleportDistance * stdRenderBabylon_kMotionBlurTeleportDistance )
        {
            bReset = true;
        }
        else
        {
            // Trace of the previous basis transposed times the current one, clamped for acos.
            float trace = 0.0f;
            for ( int column = 0; column < 3; ++column )
            {
                const float* pCur = &aNew[column * 4];
                const float* pLast = &stdRenderBabylon_state.aLastCameraMatrix[column * 4];
                trace += pCur[0] * pLast[0] + pCur[1] * pLast[1] + pCur[2] * pLast[2];
            }

            const float cosine = std::clamp((trace - 1.0f) * 0.5f, -1.0f, 1.0f);
            const float angle = std::acos(cosine) * (180.0f / 3.14159265358979323846f);
            if ( angle > stdRenderBabylon_kMotionBlurMaxRotation )
            {
                bReset = true;
            }
        }
    }

    std::memcpy(stdRenderBabylon_state.aMotionBlurCurrent, aNew,
        sizeof(stdRenderBabylon_state.aMotionBlurCurrent));
    if ( bReset )
    {
        std::memcpy(stdRenderBabylon_state.aMotionBlurPrevious, aNew,
            sizeof(stdRenderBabylon_state.aMotionBlurPrevious));
        stdRenderBabylon_state.bMotionBlurActive = false;
    }
    else
    {
        std::memcpy(stdRenderBabylon_state.aMotionBlurPrevious,
            stdRenderBabylon_state.aLastCameraMatrix,
            sizeof(stdRenderBabylon_state.aMotionBlurPrevious));
        stdRenderBabylon_state.bMotionBlurActive = true;
    }

    std::memcpy(stdRenderBabylon_state.aLastCameraMatrix, aNew,
        sizeof(stdRenderBabylon_state.aLastCameraMatrix));
    stdRenderBabylon_state.bHasCameraTransform = true;
}

void stdRenderBabylon_SetProjection(float fovDegrees, float nearPlane, float farPlane)
{
    if ( !std::isfinite(fovDegrees) || fovDegrees <= 1.0f || fovDegrees >= 179.0f
        || !std::isfinite(nearPlane) || !std::isfinite(farPlane)
        || nearPlane <= 0.0f || farPlane <= nearPlane )
    {
        return;
    }

    stdRenderBabylon_state.projectionFovDegrees = fovDegrees;
    stdRenderBabylon_state.projectionNearPlane  = nearPlane;
    stdRenderBabylon_state.projectionFarPlane   = farPlane;
}

void stdRenderBabylon_SetFrameTime(float deltaTime)
{
    if ( !stdRenderBabylon_state.pDevice || !std::isfinite(deltaTime) )
    {
        return;
    }

    // Added: the frame's game time drives the light-shaft fade and the water ripple phase,
    // presentation only. A long step (loading, a stalled frame) still settles within the fade time;
    // the growth is clamped so it cannot overflow the exponential.
    const float step = std::clamp(deltaTime, 0.0f, 0.25f);
    stdRenderBabylon_state.shaftDeltaTime = step;

    // Added: accumulate the water phase in game time, so it pauses with the game and never runs on
    // wall-clock time. Wrapped to keep the float precise over a long session.
    stdRenderBabylon_state.waterTime += step;
    if ( stdRenderBabylon_state.waterTime > 1000.0f )
    {
        stdRenderBabylon_state.waterTime = std::fmod(stdRenderBabylon_state.waterTime, 1000.0f);
    }

    // Added: the film grain's frame counter follows game time, so a paused game holds the grain
    // still and the same counter always produces the same noise.
    if ( step > 0.0f )
    {
        ++stdRenderBabylon_state.grainFrame;
    }
}

render::CommandList& stdRenderBabylon_GetFrame()
{
    STD_ASSERTREL(stdRenderBabylon_state.pDevice);
    if ( !stdRenderBabylon_state.frame )
    {
        stdRenderBabylon_state.frame.emplace(stdRenderBabylon_state.pDevice->beginFrame());
    }

    return *stdRenderBabylon_state.frame;
}

bool stdRenderBabylon_HasFrame()
{
    return stdRenderBabylon_state.frame.has_value();
}

uint64_t stdRenderBabylon_GetBackBufferVersion()
{
    const bool bPendingDraws = stdRenderBabylon_state.frame && stdRenderBabylon_DrawsBackBuffer(*stdRenderBabylon_state.frame);
    return stdRenderBabylon_state.backBufferVersion + (bPendingDraws ? 1u : 0u);
}

bool stdRenderBabylon_WriteBackBuffer(const void* pData)
{
    if ( !pData )
    {
        STDLOG_ERROR("stdRenderBabylon_WriteBackBuffer: no source pixels.\n");
        return false;
    }

    if ( !stdRenderBabylon_state.pDevice || stdRenderBabylon_state.colorTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_WriteBackBuffer: no back buffer.\n");
        return false;
    }

    if ( !stdRenderBabylon_FlushFrame() )
    {
        return false;
    }

    const uint32_t modeWidth    = stdRenderBabylon_state.modeWidth;
    const uint32_t modeHeight   = stdRenderBabylon_state.modeHeight;
    const uint32_t renderWidth  = stdRenderBabylon_state.width;
    const uint32_t renderHeight = stdRenderBabylon_state.height;
    const size_t numPixels      = (size_t)renderWidth * renderHeight;
    const render::TextureFormat format = stdRenderBabylon_state.colorFormat;

    // The resampled scene-sized Bgra8 source; at render scale 1 it is the caller's own bytes.
    const uint8_t* pSource = static_cast<const uint8_t*>(pData);
    std::vector<uint8_t> aResampled;
    if ( renderWidth != modeWidth || renderHeight != modeHeight )
    {
        aResampled.resize(numPixels * 4u);
        stdRenderBabylon_ResampleBgra(pSource, modeWidth, modeHeight, aResampled.data(),
            renderWidth, renderHeight);
        pSource = aResampled.data();
    }

    std::vector<std::byte> converted;
    std::span<const std::byte> data(reinterpret_cast<const std::byte*>(pSource), numPixels * 4u);
    if ( format == render::TextureFormat::Rgba16F )
    {
        // Bgra8 to Rgba16F: half floats R, G, B, A in the 0..1 the bytes hold.
        converted.resize(numPixels * 8u);
        for ( size_t i = 0; i < numPixels; ++i )
        {
            const float aChannel[4] = { pSource[i * 4u + 2u] / 255.0f,
                pSource[i * 4u + 1u] / 255.0f, pSource[i * 4u + 0u] / 255.0f,
                pSource[i * 4u + 3u] / 255.0f };
            std::byte* pTexel = converted.data() + i * 8u;
            for ( size_t channel = 0; channel < 4u; ++channel )
            {
                const uint16_t half = stdRenderBabylon_FloatToHalf(aChannel[channel]);
                pTexel[channel * 2u + 0u] = (std::byte)(half & 0xFFu);
                pTexel[channel * 2u + 1u] = (std::byte)(half >> 8);
            }
        }
        data = converted;
    }

    if ( !stdRenderBabylon_state.pDevice->uploadTexture(stdRenderBabylon_state.colorTarget, 0, data) )
    {
        STDLOG_ERROR("stdRenderBabylon_WriteBackBuffer: the device refused the %ux%u upload.\n",
            renderWidth, renderHeight);
        return false;
    }

    ++stdRenderBabylon_state.backBufferVersion;
    return true;
}

bool stdRenderBabylon_ClearBackBuffer(const render::ClearColor& color)
{
    if ( !stdRenderBabylon_state.pDevice || stdRenderBabylon_state.colorTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_ClearBackBuffer: no back buffer.\n");
        return false;
    }

    render::CommandList& list = stdRenderBabylon_GetFrame();
    render::PassDesc pass;
    pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_state.colorTarget, 0,
        render::LoadOp::Clear, render::StoreOp::Store, color });
    if ( !list.beginPass(pass) || !list.endPass() )
    {
        STDLOG_ERROR("stdRenderBabylon_ClearBackBuffer: the frame refused the clear pass.\n");
        return false;
    }

    return true;
}

void stdRenderBabylon_DestroyTexture(render::TextureHandle texture)
{
    if ( texture.isNull() || !stdRenderBabylon_state.pDevice )
    {
        return;
    }

    if ( stdRenderBabylon_state.frame )
    {
        stdRenderBabylon_state.aDeferredTextures.push_back(texture);
        return;
    }

    stdRenderBabylon_state.pDevice->destroyTexture(texture);
}

void stdRenderBabylon_DestroyBuffer(render::BufferHandle buffer)
{
    if ( buffer.isNull() || !stdRenderBabylon_state.pDevice )
    {
        return;
    }

    if ( stdRenderBabylon_state.frame )
    {
        stdRenderBabylon_state.aDeferredBuffers.push_back(buffer);
        return;
    }

    stdRenderBabylon_state.pDevice->destroyBuffer(buffer);
}
