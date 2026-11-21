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

#include "SmaaTextures.h"
#include "stdBabylon.h"
#include "stdRenderBabylon.h"

namespace
{
    namespace render = babylon::render;

    // The render scale's config key (Docs/Jones.cfg.md); read when the frame owner opens.
    constexpr const char* stdRenderBabylon_kRenderScaleKey = "engine.renderer.renderScale";
    constexpr float stdRenderBabylon_kMinRenderScale = 0.5f;
    constexpr float stdRenderBabylon_kMaxRenderScale = 2.0f;

    // Added: SMAA 1x (0 off, 1 on) of the 3D scene, read when the frame owner opens.
    constexpr const char* stdRenderBabylon_kAntiAliasingKey = "engine.renderer.antiAliasing";

    // Added: contrast-adaptive sharpening strength (0 off to 1 strongest) of the presented 3D
    // image, read when the frame owner opens.
    constexpr const char* stdRenderBabylon_kSharpeningKey = "engine.renderer.sharpening";
    constexpr float stdRenderBabylon_kMaxSharpening = 1.0f;

    // Added: the final tone map's operator (0 clamp, 1 Neutral, 2 ACES fitted) and exposure, read
    // when the frame owner opens.
    constexpr const char* stdRenderBabylon_kToneMappingKey = "engine.renderer.toneMapping";
    constexpr const char* stdRenderBabylon_kExposureKey    = "engine.renderer.exposure";
    constexpr float stdRenderBabylon_kMinExposure = 0.5f;
    constexpr float stdRenderBabylon_kMaxExposure = 2.0f;

    struct StdRenderBabylonState
    {
        render::Device* pDevice = nullptr;
        std::optional<render::CommandList> frame;

        render::TextureHandle colorTarget;
        render::TextureHandle depthTarget;
        render::TextureHandle normalTarget;

        // Added: the display-sized HUD layer std3D draws between BeginHudLayer and EndHudLayer. It is
        // composited over the tone-mapped scene before the menu and prompt overlays.
        render::TextureHandle hudColorTarget;
        render::TextureHandle hudNormalTarget;
        render::TextureHandle hudDepthTarget;
        bool bHudLayerDrawn = false; // the last submitted frame recorded a HUD pass

        render::TextureFormat colorFormat  = render::TextureFormat::Bgra8;
        render::TextureFormat depthFormat  = render::TextureFormat::D24S8;
        render::TextureFormat normalFormat = render::TextureFormat::R10G10B10A2;
        bool bDepthSampled = false; // the depth target is also Sampled, so post passes can read it
        uint32_t modeWidth  = 0;    // the display/video-mode size the game sees
        uint32_t modeHeight = 0;
        uint32_t width      = 0;    // the scene targets' size: mode size * render scale
        uint32_t height     = 0;
        float renderScale   = 1.0f;
        bool bScaleClampLogged = false; // one line per requested scale when the device limit clamps it

        // Added: SMAA 1x on the tone-mapped LDR image, before the HUD composite. Its three targets
        // are at the presented size (the window's drawable size, or the display size without one)
        // and are recreated when that size changes. bSmaaSetupFailed stops repeated log lines when
        // a pipeline or the precomputed textures can't be made.
        bool bAntiAliasing = false;
        bool bSmaaSetupFailed = false;
        render::TextureHandle smaaColorTarget;
        render::TextureHandle smaaEdgesTarget;
        render::TextureHandle smaaWeightsTarget;
        uint32_t smaaWidth  = 0;
        uint32_t smaaHeight = 0;
        render::PipelineHandle smaaToneMapPipeline;
        render::PipelineHandle smaaEdgePipeline;
        render::PipelineHandle smaaWeightsPipeline;
        render::PipelineHandle smaaBlendPipeline;        // to the swapchain
        render::PipelineHandle smaaBlendCapturePipeline; // to a Bgra8 capture target
        render::TextureHandle smaaAreaTexture;
        render::TextureHandle smaaSearchTexture;

        // Added: contrast-adaptive sharpening on the presented-size LDR image, after SMAA or the
        // tone map and before the HUD composite. casSourceTarget is that Rgba8 image; the CAS pass
        // reads it and writes the presented frame. bCasSetupFailed stops repeated log lines.
        float sharpening = 0.0f;
        bool bCasSetupFailed = false;
        render::TextureHandle casSourceTarget;
        uint32_t casWidth  = 0;
        uint32_t casHeight = 0;
        render::PipelineHandle casToneMapPipeline;    // the tone map into casSourceTarget
        render::PipelineHandle smaaBlendCasPipeline;  // SMAA's blend into casSourceTarget
        render::PipelineHandle casPipeline;           // to the swapchain
        render::PipelineHandle casCapturePipeline;    // to a Bgra8 capture target

        // The final pass's tone map, and whether it runs the tone-map shader (false only when a test
        // forced the pre-M9.3 path). Opening reads the operator and exposure from the config.
        StdRenderBabylonToneMap toneMap;
        bool bToneMap = true;

        // Changes whenever the back buffer's contents may change (stdRenderBabylon_GetBackBufferVersion).
        uint64_t backBufferVersion = 0;

        render::PipelineHandle presentPipeline;
        render::PipelineHandle capturePipeline; // Bgra8, for RenderPresentToTexture
        render::PipelineHandle hudPipeline;     // Added: HUD composite to the swapchain
        render::PipelineHandle hudCapturePipeline; // Added: HUD composite to a Bgra8 capture target
        render::SamplerHandle presentSampler; // also the post passes' sampler; nearest, clamping
        render::SamplerHandle finalSampler;   // linear, clamping: the final pass at a render scale != 1

        // The pass a caller records on top of the presented frame, and the
        // offscreen target of stdRenderBabylon_RenderPresentToTexture.
        StdRenderBabylonPresentOverlay presentOverlay = nullptr;
        void* presentOverlayUser = nullptr;

        // Added: the gameplay HUD overlay, drawn before the present overlay so a menu can cover it.
        StdRenderBabylonPresentOverlay hudOverlay = nullptr;
        void* hudOverlayUser = nullptr;

        // The post-processing chain: one pipeline per pass, in order, with the number of inputs its
        // shader reads, and the two targets the passes write in turn (made at the first presenting
        // submit that needs them, freed with the back buffer).
        struct PostPass
        {
            render::PipelineHandle pipeline;
            uint32_t numInputs = 0;
        };
        std::vector<PostPass> aPostPasses;
        render::TextureHandle aPostTargets[2];

        std::vector<render::TextureHandle> aDeferredTextures;
        std::vector<render::BufferHandle> aDeferredBuffers;
    };

    StdRenderBabylonState stdRenderBabylon_state;

#ifdef J3D_TEST
    bool stdRenderBabylon_bLegacyPresentationForTest = false;
#endif

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

    // Added: frees the anti-aliasing stage's presented-size targets. Recreated by the next present
    // that anti-aliases after a display or window size change.
    void stdRenderBabylon_FreeSmaaTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.smaaColorTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.smaaEdgesTarget);
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.smaaWeightsTarget);
        stdRenderBabylon_state.smaaColorTarget   = {};
        stdRenderBabylon_state.smaaEdgesTarget   = {};
        stdRenderBabylon_state.smaaWeightsTarget = {};
        stdRenderBabylon_state.smaaWidth         = 0;
        stdRenderBabylon_state.smaaHeight        = 0;
    }

    // Added: the three presented-size targets of the anti-aliasing stage, made when their size
    // changes. False, logging why, when one can't be created; there are none then.
    bool stdRenderBabylon_EnsureSmaaTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.smaaColorTarget.isNull()
            && stdRenderBabylon_state.smaaWidth == width
            && stdRenderBabylon_state.smaaHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeSmaaTargets();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = render::TextureFormat::Rgba8;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle color   = stdRenderBabylon_state.pDevice->createTexture(desc);
        const render::TextureHandle edges   = stdRenderBabylon_state.pDevice->createTexture(desc);
        const render::TextureHandle weights = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( color.isNull() || edges.isNull() || weights.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u anti-aliasing targets.\n",
                width, height);
            stdRenderBabylon_state.pDevice->destroyTexture(color);
            stdRenderBabylon_state.pDevice->destroyTexture(edges);
            stdRenderBabylon_state.pDevice->destroyTexture(weights);
            return false;
        }

        stdRenderBabylon_state.smaaColorTarget   = color;
        stdRenderBabylon_state.smaaEdgesTarget   = edges;
        stdRenderBabylon_state.smaaWeightsTarget = weights;
        stdRenderBabylon_state.smaaWidth         = width;
        stdRenderBabylon_state.smaaHeight        = height;
        return true;
    }

    // Added: an Rgba8, linearly sampled texture from SMAA's RG8 or R8 precomputed data. Babylon has
    // no two- or one-channel format, so the extra channels are padded. Null, logging why, on
    // failure.
    render::TextureHandle stdRenderBabylon_CreateSmaaDataTexture(uint32_t width, uint32_t height,
        const unsigned char* pSrc, uint32_t numChannels)
    {
        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = render::TextureFormat::Rgba8;
        desc.usage     = render::TextureUsage::Sampled;

        const render::TextureHandle texture = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( texture.isNull() )
        {
            return {};
        }

        std::vector<std::byte> data((size_t)width * height * 4u);
        for ( size_t i = 0; i < (size_t)width * height; ++i )
        {
            data[i * 4u + 0u] = (std::byte)pSrc[i * numChannels + 0u];
            data[i * 4u + 1u] = numChannels > 1u ? (std::byte)pSrc[i * numChannels + 1u]
                                                 : (std::byte)0;
            data[i * 4u + 2u] = (std::byte)0;
            data[i * 4u + 3u] = (std::byte)0xFF;
        }

        if ( !stdRenderBabylon_state.pDevice->uploadTexture(texture, 0,
            std::span<const std::byte>(data)) )
        {
            stdRenderBabylon_state.pDevice->destroyTexture(texture);
            return {};
        }

        return texture;
    }

    // Added: the pipelines and precomputed textures of the anti-aliasing stage, made on first use.
    bool stdRenderBabylon_EnsureSmaaResources()
    {
        if ( stdRenderBabylon_state.bSmaaSetupFailed )
        {
            return false;
        }

        if ( stdRenderBabylon_state.smaaAreaTexture.isNull() )
        {
            stdRenderBabylon_state.smaaAreaTexture = stdRenderBabylon_CreateSmaaDataTexture(
                stdSmaaTextures::kAreaTexWidth, stdSmaaTextures::kAreaTexHeight,
                stdSmaaTextures::kAreaTexBytes, 2u);
            if ( stdRenderBabylon_state.smaaAreaTexture.isNull() )
            {
                STDLOG_ERROR("stdRenderBabylon: could not create SMAA's area texture.\n");
                stdRenderBabylon_state.bSmaaSetupFailed = true;
                return false;
            }
        }

        if ( stdRenderBabylon_state.smaaSearchTexture.isNull() )
        {
            stdRenderBabylon_state.smaaSearchTexture = stdRenderBabylon_CreateSmaaDataTexture(
                stdSmaaTextures::kSearchTexWidth, stdSmaaTextures::kSearchTexHeight,
                stdSmaaTextures::kSearchTexBytes, 1u);
            if ( stdRenderBabylon_state.smaaSearchTexture.isNull() )
            {
                STDLOG_ERROR("stdRenderBabylon: could not create SMAA's search texture.\n");
                stdRenderBabylon_state.bSmaaSetupFailed = true;
                return false;
            }
        }

        render::PipelineDesc desc;
        desc.vertexShader = stdBabylon::shaders::presentVert();
        desc.primitive    = render::PrimitiveType::TriangleList;
        desc.colorTargets = { render::ColorTargetDesc{ render::TextureFormat::Rgba8, {} } };

        if ( stdRenderBabylon_state.smaaToneMapPipeline.isNull() )
        {
            desc.fragmentShader = stdRenderBabylon_FinalFrag();
            stdRenderBabylon_state.smaaToneMapPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.smaaEdgePipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::smaaEdgeFrag();
            stdRenderBabylon_state.smaaEdgePipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }
        if ( stdRenderBabylon_state.smaaWeightsPipeline.isNull() )
        {
            desc.fragmentShader = stdBabylon::shaders::smaaWeightsFrag();
            stdRenderBabylon_state.smaaWeightsPipeline =
                stdRenderBabylon_state.pDevice->createPipeline(desc);
        }

        if ( stdRenderBabylon_state.smaaToneMapPipeline.isNull()
            || stdRenderBabylon_state.smaaEdgePipeline.isNull()
            || stdRenderBabylon_state.smaaWeightsPipeline.isNull()
            || !stdRenderBabylon_EnsurePresentSampler()
            || !stdRenderBabylon_EnsureFinalSampler() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the anti-aliasing passes.\n");
            stdRenderBabylon_state.bSmaaSetupFailed = true;
            return false;
        }

        return true;
    }

    // Added: the last anti-aliasing pass's pipeline for its target: the swapchain, a Bgra8 capture
    // or the Rgba8 sharpening source. Made on first use; false when it can't be.
    bool stdRenderBabylon_EnsureSmaaBlendPipeline(bool bSwapchain, bool bCasSource,
        render::TextureFormat format)
    {
        render::PipelineHandle* pPipeline = bSwapchain
            ? &stdRenderBabylon_state.smaaBlendPipeline
            : (bCasSource ? &stdRenderBabylon_state.smaaBlendCasPipeline
                          : &stdRenderBabylon_state.smaaBlendCapturePipeline);
        if ( !pPipeline->isNull() )
        {
            return true;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::smaaBlendFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ format, {} } };

        *pPipeline = stdRenderBabylon_state.pDevice->createPipeline(desc);
        return !pPipeline->isNull();
    }

    // Added: frees the sharpening stage's presented-size source image. Recreated by the next
    // present that sharpens after a display or window size change.
    void stdRenderBabylon_FreeCasTarget()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.casSourceTarget);
        stdRenderBabylon_state.casSourceTarget = {};
        stdRenderBabylon_state.casWidth        = 0;
        stdRenderBabylon_state.casHeight       = 0;
    }

    // Added: the presented-size Rgba8 image the sharpening stage reads, made when its size
    // changes. False, logging why, when it can't be created; there is none then.
    bool stdRenderBabylon_EnsureCasTarget(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.casSourceTarget.isNull()
            && stdRenderBabylon_state.casWidth == width
            && stdRenderBabylon_state.casHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeCasTarget();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = render::TextureFormat::Rgba8;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u sharpening source.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.casSourceTarget = target;
        stdRenderBabylon_state.casWidth        = width;
        stdRenderBabylon_state.casHeight       = height;
        return true;
    }

    // Added: the tone-map pass into the sharpening source (which anti-aliasing off and sharpening
    // on share), made on first use.
    bool stdRenderBabylon_EnsureCasToneMapPipeline()
    {
        if ( !stdRenderBabylon_state.casToneMapPipeline.isNull() )
        {
            return true;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdRenderBabylon_FinalFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ render::TextureFormat::Rgba8, {} } };

        stdRenderBabylon_state.casToneMapPipeline =
            stdRenderBabylon_state.pDevice->createPipeline(desc);
        return !stdRenderBabylon_state.casToneMapPipeline.isNull();
    }

    // Added: the sharpening pass's pipeline for target (the swapchain or a Bgra8 capture), made on
    // first use.
    bool stdRenderBabylon_EnsureCasPipeline(bool bSwapchain, render::TextureFormat format)
    {
        render::PipelineHandle* pPipeline = bSwapchain
            ? &stdRenderBabylon_state.casPipeline
            : &stdRenderBabylon_state.casCapturePipeline;
        if ( !pPipeline->isNull() )
        {
            return true;
        }

        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::casFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ format, {} } };

        *pPipeline = stdRenderBabylon_state.pDevice->createPipeline(desc);
        return !pPipeline->isNull();
    }

    // Added: the pipeline that composites the HUD layer over the presented frame. It draws the
    // present pass's triangle sampling the HUD color target and blends it with ONE / ONE_MINUS_SRC_ALPHA,
    // because the HUD pass accumulates premultiplied color and coverage alpha. The target format is
    // the swapchain's or Bgra8 for a capture.
    render::PipelineHandle stdRenderBabylon_CreateHudPipeline(render::TextureFormat format)
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdBabylon::shaders::presentFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;

        render::BlendState blend;
        blend.enabled  = true;
        blend.srcColor = render::BlendFactor::One;
        blend.dstColor = render::BlendFactor::OneMinusSrcAlpha;
        blend.colorOp  = render::BlendOp::Add;
        blend.srcAlpha = render::BlendFactor::One;
        blend.dstAlpha = render::BlendFactor::OneMinusSrcAlpha;
        blend.alphaOp  = render::BlendOp::Add;
        desc.colorTargets = { render::ColorTargetDesc{ format, blend } };

        return stdRenderBabylon_state.pDevice->createPipeline(desc);
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

    void stdRenderBabylon_FreePostTargets()
    {
        for ( render::TextureHandle& target : stdRenderBabylon_state.aPostTargets )
        {
            stdRenderBabylon_DestroyTexture(target);
            target = {};
        }
    }

    // Makes the two post targets at the back buffer's size when they are missing. Their format is
    // the scene's, so an Rgba16F scene keeps values outside 0..1 through the passes before the tone
    // map. False, logging why, when one can't be created; there are none then.
    bool stdRenderBabylon_EnsurePostTargets()
    {
        if ( !stdRenderBabylon_state.aPostTargets[0].isNull() )
        {
            return true;
        }

        render::TextureDesc desc;
        desc.width     = stdRenderBabylon_state.width;
        desc.height    = stdRenderBabylon_state.height;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;
        for ( render::TextureHandle& target : stdRenderBabylon_state.aPostTargets )
        {
            target = stdRenderBabylon_state.pDevice->createTexture(desc);
            if ( target.isNull() )
            {
                STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u post-processing targets.\n", desc.width, desc.height);
                stdRenderBabylon_FreePostTargets();
                return false;
            }
        }

        return true;
    }

    // The texture the present pass samples: the last post pass's output, or the back buffer color.
    render::TextureHandle stdRenderBabylon_PresentSource()
    {
        const size_t numPasses = stdRenderBabylon_state.aPostPasses.size();
        if ( numPasses == 0 || stdRenderBabylon_state.aPostTargets[0].isNull() )
        {
            return stdRenderBabylon_state.colorTarget;
        }

        return stdRenderBabylon_state.aPostTargets[(numPasses - 1) % 2];
    }

    // The post-processing chain into the frame: pass i reads the output of pass i - 1 (the back buffer
    // color for pass 0) in fragment slot 0, the normal target in slot 1 and the depth target in slot 2
    // as far as its shader reads, and writes post target i % 2. Nothing without passes or a back buffer.
    void stdRenderBabylon_RecordPostChain(render::CommandList& list)
    {
        if ( stdRenderBabylon_state.aPostPasses.empty() || stdRenderBabylon_state.colorTarget.isNull()
            || !stdRenderBabylon_EnsurePostTargets() )
        {
            return;
        }

        render::TextureHandle input = stdRenderBabylon_state.colorTarget;
        for ( size_t i = 0; i < stdRenderBabylon_state.aPostPasses.size(); ++i )
        {
            const StdRenderBabylonState::PostPass& postPass = stdRenderBabylon_state.aPostPasses[i];
            const render::TextureHandle output = stdRenderBabylon_state.aPostTargets[i % 2];

            render::PassDesc pass;
            pass.colorTargets.push_back(render::ColorAttachment{ output, 0, render::LoadOp::DontCare,
                render::StoreOp::Store, {} });
            if ( !list.beginPass(pass) )
            {
                return;
            }

            const render::TextureHandle aInputs[3] = { input, stdRenderBabylon_state.normalTarget, stdRenderBabylon_state.depthTarget };
            list.setPipeline(postPass.pipeline);
            for ( uint32_t slot = 0; slot < postPass.numInputs; ++slot )
            {
                list.bindTexture(render::ShaderStage::Fragment, slot, aInputs[slot], stdRenderBabylon_state.presentSampler);
            }

            list.draw(3);
            (void)list.endPass();
            input = output;
        }
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

    // Added: true when list has a pass that drew into the HUD layer's color target.
    bool stdRenderBabylon_DrawsHudLayer(const render::CommandList& list)
    {
        const render::TextureHandle hudTarget = stdRenderBabylon_state.hudColorTarget;
        if ( hudTarget.isNull() )
        {
            return false;
        }

        for ( const render::Command& command : list.commands() )
        {
            if ( const render::BeginPass* pBegin = std::get_if<render::BeginPass>(&command) )
            {
                for ( const render::ColorAttachment& attachment : pBegin->desc.colorTargets )
                {
                    if ( attachment.target == hudTarget )
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    // Added: records the HUD layer over the presented frame and returns false, logging why, when the
    // pipeline or the pass can't be made. Nothing is recorded when no HUD was drawn or there is no
    // layer. The display-sized layer is sampled like the final pass samples the scene.
    bool stdRenderBabylon_RecordHudComposite(render::CommandList& list, render::TextureHandle target,
        render::TextureFormat format)
    {
        if ( !stdRenderBabylon_state.bHudLayerDrawn || stdRenderBabylon_state.hudColorTarget.isNull() )
        {
            return true;
        }

        render::PipelineHandle* pPipeline = target.isNull()
            ? &stdRenderBabylon_state.hudPipeline : &stdRenderBabylon_state.hudCapturePipeline;
        if ( pPipeline->isNull() )
        {
            *pPipeline = stdRenderBabylon_CreateHudPipeline(format);
        }
        if ( pPipeline->isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the HUD composite pipeline.\n");
            return false;
        }

        // Mirror the final pass: the pre-M9.4 1:1 nearest sample at render scale 1, linear otherwise.
        // At render scale 1 this keeps the HUD's pixels exactly where the old single pass put them,
        // also when the window scales the display-sized frame.
        render::SamplerHandle sampler;
        if ( stdRenderBabylon_state.renderScale == 1.0f )
        {
            if ( !stdRenderBabylon_EnsurePresentSampler() )
            {
                return false;
            }
            sampler = stdRenderBabylon_state.presentSampler;
        }
        else
        {
            if ( !stdRenderBabylon_EnsureFinalSampler() )
            {
                return false;
            }
            sampler = stdRenderBabylon_state.finalSampler;
        }

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ target, 0,
            render::LoadOp::Load, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(*pPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.hudColorTarget, sampler);
        list.draw(3);
        return list.endPass();
    }

    // The final pass: the presented source drawn over the whole target (the null handle, for the
    // swapchain), with the tone map when it runs. The vertex shader makes the triangle itself, so
    // nothing is bound but the result and the input. Returns false, having logged nothing, when the
    // pass can't be started or ended; the caller decides what that means.
    bool stdRenderBabylon_RecordFinalPass(render::CommandList& list, render::PipelineHandle pipeline,
        render::TextureHandle target)
    {
        if ( pipeline.isNull() || stdRenderBabylon_state.colorTarget.isNull() )
        {
            return false;
        }

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ target, 0,
            render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        // At render scale 1 the present is the pre-M9.4 1:1 nearest sample; at any other scale the
        // final pass resolves the scaled scene into the presented size with linear filtering.
        const render::SamplerHandle sampler = stdRenderBabylon_state.renderScale == 1.0f
            ? stdRenderBabylon_state.presentSampler
            : stdRenderBabylon_state.finalSampler;
        list.setPipeline(pipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_PresentSource(), sampler);
        if ( stdRenderBabylon_state.bToneMap )
        {
            const float aParams[4] = { stdRenderBabylon_state.toneMap.exposure,
                (float)stdRenderBabylon_state.toneMap.oper, 0.0f, 0.0f };
            list.pushUniforms(render::ShaderStage::Fragment, 0,
                std::as_bytes(std::span<const float>(aParams)));
        }
        list.draw(3);
        return list.endPass();
    }

    // Added: the size of the presented frame in pixels: the window's drawable size, or the game's
    // display size without a window. The anti-aliasing targets match it.
    void stdRenderBabylon_GetPresentedSize(uint32_t* pWidth, uint32_t* pHeight)
    {
        uint32_t width  = stdRenderBabylon_state.modeWidth;
        uint32_t height = stdRenderBabylon_state.modeHeight;

        StdBabylonContext* pContext = stdBabylon_GetContext();
        if ( pContext && pContext->pWindow )
        {
            const babylon::platform::Size size = pContext->pWindow->size();
            if ( size.width > 0 && size.height > 0 )
            {
                width  = static_cast<std::uint32_t>(size.width);
                height = static_cast<std::uint32_t>(size.height);
            }
        }

        *pWidth  = width;
        *pHeight = height;
    }

    // Added: the SMAA 1x stage over the tone-mapped image, before the HUD composite: the tone map
    // into an LDR target (presented size), luma edges, blending weights with SMAA's area and search
    // textures, then neighbourhood blending into target (the swapchain for a null handle). False,
    // having logged nothing, when a resource can't be made or a pass can't be started or ended; the
    // caller then presents without anti-aliasing.
    bool stdRenderBabylon_RecordSmaa(render::CommandList& list, render::TextureHandle target,
        render::TextureFormat format, uint32_t width, uint32_t height, bool bCasSource)
    {
        if ( width == 0 || height == 0 || !stdRenderBabylon_EnsureSmaaResources()
            || !stdRenderBabylon_EnsureSmaaTargets(width, height)
            || !stdRenderBabylon_EnsureSmaaBlendPipeline(target.isNull(), bCasSource, format) )
        {
            return false;
        }

        // The final pass's tone map into the LDR image, with the same source and sampling rules.
        if ( !stdRenderBabylon_RecordFinalPass(list, stdRenderBabylon_state.smaaToneMapPipeline,
            stdRenderBabylon_state.smaaColorTarget) )
        {
            return false;
        }

        const float aParams[4] = { 1.0f / (float)width, 1.0f / (float)height,
            (float)width, (float)height };

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_state.smaaEdgesTarget,
            0, render::LoadOp::Clear, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.smaaEdgePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.smaaColorTarget,
            stdRenderBabylon_state.finalSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const float>(aParams)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        pass.colorTargets[0] = render::ColorAttachment{ stdRenderBabylon_state.smaaWeightsTarget,
            0, render::LoadOp::DontCare, render::StoreOp::Store, {} };
        if ( !list.beginPass(pass) )
        {
            return false;
        }
        list.setPipeline(stdRenderBabylon_state.smaaWeightsPipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.smaaEdgesTarget,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.smaaAreaTexture,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 2, stdRenderBabylon_state.smaaSearchTexture,
            stdRenderBabylon_state.finalSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const float>(aParams)));
        list.draw(3);
        if ( !list.endPass() )
        {
            return false;
        }

        const render::PipelineHandle blend = target.isNull()
            ? stdRenderBabylon_state.smaaBlendPipeline
            : (bCasSource ? stdRenderBabylon_state.smaaBlendCasPipeline
                          : stdRenderBabylon_state.smaaBlendCapturePipeline);
        pass.colorTargets[0] = render::ColorAttachment{ target, 0, render::LoadOp::DontCare,
            render::StoreOp::Store, {} };
        if ( !list.beginPass(pass) )
        {
            return false;
        }
        list.setPipeline(blend);
        list.bindTexture(render::ShaderStage::Fragment, 0, stdRenderBabylon_state.smaaColorTarget,
            stdRenderBabylon_state.finalSampler);
        list.bindTexture(render::ShaderStage::Fragment, 1, stdRenderBabylon_state.smaaWeightsTarget,
            stdRenderBabylon_state.finalSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const float>(aParams)));
        list.draw(3);
        return list.endPass();
    }

    // Added: the sharpening pass from source (the presented-size LDR image) into target (the
    // swapchain for a null handle). False, having logged nothing, when a resource can't be made or
    // a pass can't be started or ended; the caller then presents without sharpening.
    bool stdRenderBabylon_RecordSharpening(render::CommandList& list, render::TextureHandle source,
        render::TextureHandle target, render::TextureFormat format, uint32_t width, uint32_t height)
    {
        if ( source.isNull() || width == 0 || height == 0
            || !stdRenderBabylon_EnsureCasPipeline(target.isNull(), format)
            || !stdRenderBabylon_EnsurePresentSampler() )
        {
            return false;
        }

        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ target, 0,
            render::LoadOp::DontCare, render::StoreOp::Store, {} });
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        const float aParams[4] = { 1.0f / (float)width, 1.0f / (float)height,
            stdRenderBabylon_state.sharpening, 0.0f };
        list.setPipeline(target.isNull() ? stdRenderBabylon_state.casPipeline
                                         : stdRenderBabylon_state.casCapturePipeline);
        list.bindTexture(render::ShaderStage::Fragment, 0, source,
            stdRenderBabylon_state.presentSampler);
        list.pushUniforms(render::ShaderStage::Fragment, 0, std::as_bytes(std::span<const float>(aParams)));
        list.draw(3);
        return list.endPass();
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

    return true;
}

void stdRenderBabylon_Close()
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        return;
    }

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
    stdRenderBabylon_state.pDevice->destroyTexture(stdRenderBabylon_state.smaaAreaTexture);
    stdRenderBabylon_state.pDevice->destroyTexture(stdRenderBabylon_state.smaaSearchTexture);
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

bool stdRenderBabylon_SetAntiAliasing(bool bEnabled)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetAntiAliasing: not open.\n");
        return false;
    }

    stdRenderBabylon_state.bAntiAliasing    = bEnabled;
    stdRenderBabylon_state.bSmaaSetupFailed = false;
    return true;
}

bool stdRenderBabylon_GetAntiAliasing()
{
    return stdRenderBabylon_state.bAntiAliasing;
}

bool stdRenderBabylon_SetSharpening(float strength)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetSharpening: not open.\n");
        return false;
    }

    if ( !std::isfinite(strength) )
    {
        STDLOG_ERROR("stdRenderBabylon_SetSharpening: sharpening %g is not finite.\n",
            (double)strength);
        return false;
    }

    const float clamped = std::clamp(strength, 0.0f, stdRenderBabylon_kMaxSharpening);
    if ( clamped != strength )
    {
        STDLOG_STATUS("stdRenderBabylon: sharpening %g is outside 0..%g; using %g.\n",
            (double)strength, (double)stdRenderBabylon_kMaxSharpening, (double)clamped);
    }

    stdRenderBabylon_state.sharpening      = clamped;
    stdRenderBabylon_state.bCasSetupFailed = false;
    return true;
}

float stdRenderBabylon_GetSharpening()
{
    return stdRenderBabylon_state.sharpening;
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

namespace
{
    // The submit of SubmitFrame and FlushFrame; bPresent records the present pass first.
    bool stdRenderBabylon_Submit(bool bPresent)
    {
        if ( !stdRenderBabylon_state.frame )
        {
            return true;
        }

        render::CommandList& list = *stdRenderBabylon_state.frame;
        bool bSubmitted = false;
        if ( list.inPass() )
        {
            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: a pass is still open, the frame is dropped.\n");
        }
        else
        {
            // Added: remember whether this frame drew the HUD layer, for the composite below and for
            // a later RenderPresentToTexture; a frame without a HUD pass leaves no HUD behind.
            stdRenderBabylon_state.bHudLayerDrawn = stdRenderBabylon_DrawsHudLayer(list);

            if ( bPresent )
            {
                stdRenderBabylon_RecordPostChain(list);

                if ( stdRenderBabylon_state.pDevice->info().hasSwapchain )
                {
                    const render::TextureFormat swapchainFormat =
                        stdRenderBabylon_state.pDevice->info().swapchainFormat;
                    uint32_t width  = 0;
                    uint32_t height = 0;
                    stdRenderBabylon_GetPresentedSize(&width, &height);

                    // Added: sharpening writes its own presented-size source, after SMAA or the
                    // tone map. When its resources can't be made, present without it.
                    bool bSharpen = stdRenderBabylon_state.sharpening > 0.0f
                        && !stdRenderBabylon_state.bCasSetupFailed;
                    if ( bSharpen
                        && ( !stdRenderBabylon_EnsureCasTarget(width, height)
                            || !stdRenderBabylon_EnsureCasToneMapPipeline() ) )
                    {
                        STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not set up sharpening; presenting without it.\n");
                        stdRenderBabylon_state.bCasSetupFailed = true;
                        bSharpen = false;
                    }

                    bool bPresented = false;
                    if ( stdRenderBabylon_state.bAntiAliasing )
                    {
                        bPresented = stdRenderBabylon_RecordSmaa(list,
                            bSharpen ? stdRenderBabylon_state.casSourceTarget : render::TextureHandle{},
                            bSharpen ? render::TextureFormat::Rgba8 : swapchainFormat,
                            width, height, bSharpen);
                        if ( !bPresented )
                        {
                            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the anti-aliasing passes; presenting without it.\n");
                        }
                    }

                    if ( !bPresented )
                    {
                        if ( bSharpen )
                        {
                            bPresented = stdRenderBabylon_RecordFinalPass(list,
                                stdRenderBabylon_state.casToneMapPipeline,
                                stdRenderBabylon_state.casSourceTarget)
                                && stdRenderBabylon_RecordSharpening(list,
                                    stdRenderBabylon_state.casSourceTarget, render::TextureHandle{},
                                    swapchainFormat, width, height);
                            if ( !bPresented )
                            {
                                STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the sharpening pass; presenting without it.\n");
                            }
                        }

                        if ( !bPresented && !stdRenderBabylon_RecordFinalPass(list,
                            stdRenderBabylon_state.presentPipeline, render::TextureHandle{}) )
                        {
                            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the final pass.\n");
                        }
                    }
                    else if ( bSharpen
                        && !stdRenderBabylon_RecordSharpening(list,
                            stdRenderBabylon_state.casSourceTarget, render::TextureHandle{},
                            swapchainFormat, width, height) )
                    {
                        STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the sharpening pass; presenting without it.\n");
                        if ( !stdRenderBabylon_RecordFinalPass(list,
                            stdRenderBabylon_state.presentPipeline, render::TextureHandle{}) )
                        {
                            STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the final pass.\n");
                        }
                    }

                    // Added: composite the HUD layer over the tone-mapped scene, after the scene's
                    // post chain, so no scene effect touches the HUD. It runs before the prompt and
                    // menu overlays.
                    if ( stdRenderBabylon_state.bHudLayerDrawn
                        && !stdRenderBabylon_RecordHudComposite(list, render::TextureHandle{},
                            stdRenderBabylon_state.pDevice->info().swapchainFormat) )
                    {
                        STDLOG_ERROR("stdRenderBabylon_SubmitFrame: could not record the HUD composite.\n");
                    }
                }

                if ( stdRenderBabylon_state.hudOverlay || stdRenderBabylon_state.presentOverlay )
                {
                    StdRenderBabylonOverlayTarget target;
                    target.texture = {};
                    target.format  = stdRenderBabylon_state.pDevice->info().hasSwapchain
                        ? stdRenderBabylon_state.pDevice->info().swapchainFormat
                        : render::TextureFormat::Bgra8;
                    // Without a window the overlay's native size is the game's display size, not
                    // the scaled scene target.
                    target.width  = stdRenderBabylon_state.modeWidth;
                    target.height = stdRenderBabylon_state.modeHeight;

                    StdBabylonContext* pContext = stdBabylon_GetContext();
                    if ( pContext && pContext->pWindow )
                    {
                        const babylon::platform::Size size = pContext->pWindow->size();
                        target.width  = size.width > 0 ? static_cast<std::uint32_t>(size.width) : 0;
                        target.height = size.height > 0 ? static_cast<std::uint32_t>(size.height) : 0;
                    }

                    if ( target.width > 0 && target.height > 0 )
                    {
                        if ( stdRenderBabylon_state.hudOverlay )
                        {
                            static_cast<void>(stdRenderBabylon_state.hudOverlay(
                                stdRenderBabylon_state.hudOverlayUser, list, target));
                        }

                        if ( stdRenderBabylon_state.presentOverlay )
                        {
                            static_cast<void>(stdRenderBabylon_state.presentOverlay(
                                stdRenderBabylon_state.presentOverlayUser, list, target));
                        }
                    }
                }
            }

            const bool bDrawsBackBuffer = stdRenderBabylon_DrawsBackBuffer(list);
            bSubmitted = stdRenderBabylon_state.pDevice->submit(list);
            if ( !bSubmitted )
            {
                STDLOG_ERROR("stdRenderBabylon_SubmitFrame: the device refused the frame.\n");
            }
            else if ( bDrawsBackBuffer )
            {
                ++stdRenderBabylon_state.backBufferVersion;
            }
        }

        stdRenderBabylon_state.frame.reset();
        stdRenderBabylon_DestroyDeferred();
        return bSubmitted;
    }
}

bool stdRenderBabylon_SubmitFrame()
{
    return stdRenderBabylon_Submit(true);
}

bool stdRenderBabylon_FlushFrame()
{
    return stdRenderBabylon_Submit(false);
}

bool stdRenderBabylon_PresentFrame()
{
    (void)stdRenderBabylon_GetFrame();
    return stdRenderBabylon_Submit(true);
}

void stdRenderBabylon_SetPresentOverlay(StdRenderBabylonPresentOverlay pfn, void* pUser)
{
    stdRenderBabylon_state.presentOverlay     = pfn;
    stdRenderBabylon_state.presentOverlayUser = pfn ? pUser : nullptr;
}

bool stdRenderBabylon_HasPresentOverlay()
{
    return stdRenderBabylon_state.presentOverlay != nullptr;
}

void stdRenderBabylon_SetHudOverlay(StdRenderBabylonPresentOverlay pfn, void* pUser)
{
    stdRenderBabylon_state.hudOverlay     = pfn;
    stdRenderBabylon_state.hudOverlayUser = pfn ? pUser : nullptr;
}

bool stdRenderBabylon_HasHudOverlay()
{
    return stdRenderBabylon_state.hudOverlay != nullptr;
}

bool stdRenderBabylon_RenderPresentToTexture(render::TextureHandle target,
    std::uint32_t width, std::uint32_t height)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: not open.\n");
        return false;
    }
    if ( stdRenderBabylon_state.colorTarget.isNull() )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: no back buffer.\n");
        return false;
    }
    if ( stdRenderBabylon_state.frame )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: a frame is being recorded.\n");
        return false;
    }
    if ( target.isNull() || width == 0 || height == 0 )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: bad target or size %ux%u.\n",
            width, height);
        return false;
    }
    if ( stdRenderBabylon_state.capturePipeline.isNull() )
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::presentVert();
        desc.fragmentShader = stdRenderBabylon_FinalFrag();
        desc.primitive      = render::PrimitiveType::TriangleList;
        desc.colorTargets   = { render::ColorTargetDesc{ render::TextureFormat::Bgra8, {} } };
        stdRenderBabylon_state.capturePipeline =
            stdRenderBabylon_state.pDevice->createPipeline(desc);
    }
    if ( stdRenderBabylon_state.capturePipeline.isNull()
        || !stdRenderBabylon_EnsurePresentSampler() || !stdRenderBabylon_EnsureFinalSampler() )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: no final pass pipeline.\n");
        return false;
    }

    render::CommandList list = stdRenderBabylon_state.pDevice->beginFrame();

    // Added: sharpening into the capture target through its own presented-size source, after SMAA
    // or the tone map. When its resources can't be made, capture without it.
    bool bSharpen = stdRenderBabylon_state.sharpening > 0.0f
        && !stdRenderBabylon_state.bCasSetupFailed;
    if ( bSharpen
        && ( !stdRenderBabylon_EnsureCasTarget(width, height)
            || !stdRenderBabylon_EnsureCasToneMapPipeline() ) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not set up sharpening; capturing without it.\n");
        stdRenderBabylon_state.bCasSetupFailed = true;
        bSharpen = false;
    }

    bool bPresented = false;
    if ( stdRenderBabylon_state.bAntiAliasing )
    {
        bPresented = stdRenderBabylon_RecordSmaa(list,
            bSharpen ? stdRenderBabylon_state.casSourceTarget : target,
            bSharpen ? render::TextureFormat::Rgba8 : render::TextureFormat::Bgra8,
            width, height, bSharpen);
        if ( !bPresented )
        {
            STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not record the anti-aliasing passes; presenting without it.\n");
        }
    }

    if ( !bPresented )
    {
        if ( bSharpen )
        {
            bPresented = stdRenderBabylon_RecordFinalPass(list,
                stdRenderBabylon_state.casToneMapPipeline,
                stdRenderBabylon_state.casSourceTarget)
                && stdRenderBabylon_RecordSharpening(list,
                    stdRenderBabylon_state.casSourceTarget, target,
                    render::TextureFormat::Bgra8, width, height);
            if ( !bPresented )
            {
                STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not record the sharpening pass; capturing without it.\n");
            }
        }

        if ( !bPresented && !stdRenderBabylon_RecordFinalPass(list,
            stdRenderBabylon_state.capturePipeline, target) )
        {
            STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: the target refused the final pass.\n");
            return false;
        }
    }
    else if ( bSharpen
        && !stdRenderBabylon_RecordSharpening(list, stdRenderBabylon_state.casSourceTarget,
            target, render::TextureFormat::Bgra8, width, height) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not record the sharpening pass; capturing without it.\n");
        if ( !stdRenderBabylon_RecordFinalPass(list,
            stdRenderBabylon_state.capturePipeline, target) )
        {
            STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: the target refused the final pass.\n");
            return false;
        }
    }

    // Added: the captured presented frame includes the HUD layer, before the prompt and menu
    // overlays, like the window.
    if ( !stdRenderBabylon_RecordHudComposite(list, target, render::TextureFormat::Bgra8) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: could not record the HUD composite.\n");
        return false;
    }

    const StdRenderBabylonOverlayTarget overlayTarget{ target,
        render::TextureFormat::Bgra8, width, height };
    if ( stdRenderBabylon_state.hudOverlay )
    {
        static_cast<void>(stdRenderBabylon_state.hudOverlay(
            stdRenderBabylon_state.hudOverlayUser, list, overlayTarget));
    }

    if ( stdRenderBabylon_state.presentOverlay )
    {
        static_cast<void>(stdRenderBabylon_state.presentOverlay(
            stdRenderBabylon_state.presentOverlayUser, list, overlayTarget));
    }

    if ( !stdRenderBabylon_state.pDevice->submit(list) )
    {
        STDLOG_ERROR("stdRenderBabylon_RenderPresentToTexture: the device refused the frame.\n");
        return false;
    }

    return true;
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

bool stdRenderBabylon_AddPostPass(const render::ShaderDesc& fragmentShader)
{
    render::Device* pDevice = stdRenderBabylon_state.pDevice;
    if ( !pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_AddPostPass: not open.\n");
        return false;
    }

    if ( fragmentShader.textureCount < 1 || fragmentShader.textureCount > 3 || fragmentShader.uniformCount != 0 )
    {
        STDLOG_ERROR("stdRenderBabylon_AddPostPass: the shader reads %u textures and %u uniform blocks; a post pass reads 1 to 3 textures and no uniforms.\n",
            fragmentShader.textureCount, fragmentShader.uniformCount);
        return false;
    }

    if ( fragmentShader.textureCount == 3 && !stdRenderBabylon_state.bDepthSampled )
    {
        STDLOG_ERROR("stdRenderBabylon_AddPostPass: the shader reads depth, which this device can't sample.\n");
        return false;
    }

    render::PipelineDesc desc;
    desc.vertexShader   = stdBabylon::shaders::presentVert();
    desc.fragmentShader = fragmentShader;
    desc.primitive      = render::PrimitiveType::TriangleList;
    // The chain's targets have the scene's format, so HDR values survive to the tone map.
    desc.colorTargets   = { render::ColorTargetDesc{ stdRenderBabylon_state.colorFormat, {} } };

    const render::PipelineHandle pipeline = pDevice->createPipeline(desc);
    if ( pipeline.isNull() || !stdRenderBabylon_EnsurePresentSampler() )
    {
        STDLOG_ERROR("stdRenderBabylon_AddPostPass: could not create the pass's pipeline.\n");
        pDevice->destroyPipeline(pipeline);
        return false;
    }

    stdRenderBabylon_state.aPostPasses.push_back(StdRenderBabylonState::PostPass{ pipeline, fragmentShader.textureCount });
    return true;
}

void stdRenderBabylon_ClearPostPasses()
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        return;
    }

    for ( const StdRenderBabylonState::PostPass& postPass : stdRenderBabylon_state.aPostPasses )
    {
        stdRenderBabylon_state.pDevice->destroyPipeline(postPass.pipeline);
    }

    stdRenderBabylon_state.aPostPasses.clear();
    stdRenderBabylon_FreePostTargets();
}

size_t stdRenderBabylon_GetNumPostPasses()
{
    return stdRenderBabylon_state.aPostPasses.size();
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
    stdRenderBabylon_state.toneMap = params;
}

StdRenderBabylonToneMap stdRenderBabylon_GetToneMap()
{
    return stdRenderBabylon_state.toneMap;
}

#ifdef J3D_TEST
void stdRenderBabylon_ForceLegacyPresentationForTest(bool bLegacy)
{
    stdRenderBabylon_bLegacyPresentationForTest = bLegacy;
}
#endif

render::TextureHandle stdRenderBabylon_GetPresentSource()
{
    return stdRenderBabylon_PresentSource();
}
