// The level-shadow stage of the Babylon render adapter (M11.3): the level's static geometry casts onto the
// characters through a second shadow map, applied only where the caster mask marks a character.
// Part of the Babylon column's render frame owner; see stdRenderBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <babylon/postfx/Effect.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/Resources.h>
#include <babylon/postfx/ShadowReceiveEffect.h>
#include <babylon/postfx/TargetPool.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>
#include <babylon/shadow/DirectionalShadowMap.h>
#include <babylon/shadow/StaticCasterSet.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <std/General/std.h>

#include "std3DBabylonInternal.h"
#include "stdRenderBabylon.h"
#include "stdRenderBabylonInternal.h"

namespace render = babylon::render;
namespace postfx = babylon::postfx;
namespace shadow = babylon::shadow;
using namespace stdRenderBabylonInternal;

namespace
{
    // Added: M11.3. The level map's depth range reaches this far toward the light beyond the character
    // fit, in view units. The game's scale is about 0.2 units per character height, so 1.5 units lets a
    // ledge several character heights above the floor stay inside the map.
    constexpr float stdRenderBabylon_kLevelShadowStaticReach = 1.5f;

    // Added: M11.6f. The level-shadow receive look, named in the same way as the character one but
    // gentler. The receivers are the characters themselves, possibly far below a ledge, so there is no
    // distance-behind fade: its start and end stay at their 0 defaults, which disables it. The scale
    // matches M11.6b, where Indy is about 0.2 view units tall.
    constexpr float stdRenderBabylon_kLevelShadowStrength            = 0.3f;   // 1 - strength is a fully shadowed pixel's brightness
    constexpr float stdRenderBabylon_kLevelShadowPenumbra            = 0.002f; // view units: minimum soft edge
    constexpr float stdRenderBabylon_kLevelShadowPenumbraPerDistance = 0.03f;  // extra view units per view unit behind the map
    constexpr float stdRenderBabylon_kLevelShadowFadeStart           = 3.0f;   // view distance where the shadow starts to fade
    constexpr float stdRenderBabylon_kLevelShadowFadeEnd             = 6.0f;   // view distance where it is gone
    constexpr float stdRenderBabylon_kLevelShadowMinLight            = 0.03f;  // luminance below which no shadow applies
    constexpr float stdRenderBabylon_kLevelShadowFullLight           = 0.25f;  // luminance at which the full shadow applies

#ifdef J3D_TEST
    // Added: M11.3. How many times the static set has been uploaded from the CPU copy, so a test can
    // prove it uploads once and only again after new casters arrive.
    std::size_t stdRenderBabylon_numLevelStaticCasterUploads = 0;
#endif

    // Added: the level-shadow receive pass's scene-sized target, recreated when the size changes. It is
    // freed with the character-shadow targets because both stages share the scene size.
    bool stdRenderBabylon_EnsureLevelShadowTarget(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.levelShadowColorTarget.isNull()
            && stdRenderBabylon_state.levelShadowWidth == width
            && stdRenderBabylon_state.levelShadowHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeLevelShadowTargets();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u level-shadow target.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.levelShadowColorTarget = target;
        stdRenderBabylon_state.levelShadowWidth       = width;
        stdRenderBabylon_state.levelShadowHeight      = height;
        return true;
    }

    // Added: opens the level-shadow map on first use. A build or test with the stage off never allocates
    // it. False, having logged once, when it can't be made.
    bool stdRenderBabylon_EnsureLevelShadowMap()
    {
        if ( stdRenderBabylon_state.bLevelShadowsSetupFailed )
        {
            return false;
        }

        if ( stdRenderBabylon_state.pLevelShadowMap )
        {
            return stdRenderBabylon_state.pLevelShadowMap->isOpen();
        }

        if ( !stdRenderBabylon_state.pDevice )
        {
            return false;
        }

        std::unique_ptr<shadow::DirectionalShadowMap> pShadowMap =
            std::make_unique<shadow::DirectionalShadowMap>(*stdRenderBabylon_state.pDevice);
        shadow::ShadowMapDesc desc;
        desc.size         = stdRenderBabylon_kShadowMapSize;
        desc.staticReach  = stdRenderBabylon_kLevelShadowStaticReach;
        // Added: M11.6f. staticReach is a hard limit for the whole level set: a caster beyond it is
        // clipped rather than clamped to depth 0, so only geometry close above the characters counts.
        desc.clampCastersToNearPlane = false;
        if ( !pShadowMap->open(desc) )
        {
            STDLOG_WARNING("stdRenderBabylon: could not open the level-shadow map; level geometry does not cast.\n");
            stdRenderBabylon_state.bLevelShadowsSetupFailed = true;
            return false;
        }

        stdRenderBabylon_state.pLevelShadowMap = std::move(pShadowMap);
        return true;
    }

    // Added: makes the GPU set on first use and uploads the CPU copy only while it is dirty. False when
    // the device or an upload fails; the dirty flag stays set so the next frame tries again.
    bool stdRenderBabylon_EnsureLevelStaticCasters()
    {
        if ( !stdRenderBabylon_state.pDevice )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.pLevelStaticCasters )
        {
            stdRenderBabylon_state.pLevelStaticCasters =
                std::make_unique<shadow::StaticCasterSet>(*stdRenderBabylon_state.pDevice);
        }

        if ( std3DBabylonInternal::std3DBabylon_AreStaticShadowCastersDirty() )
        {
            const std::vector<float>& aPositions =
                std3DBabylonInternal::std3DBabylon_GetStaticShadowCasterPositions();
            if ( !stdRenderBabylon_state.pLevelStaticCasters->upload(
                    std::span<const float>(aPositions)) )
            {
                return false;
            }

#ifdef J3D_TEST
            ++stdRenderBabylon_numLevelStaticCasterUploads;
#endif
            std3DBabylonInternal::std3DBabylon_MarkStaticShadowCastersClean();
        }

        return true;
    }
}

namespace stdRenderBabylonInternal
{
    // Added: M11.3. The frame's world-to-view matrix from the shader camera transform. aCamera is the
    // layout stdRenderBabylon_ToShaderCamera writes: right, up and forward in world coordinates with the
    // camera's world position in the fourth column (elements 3, 7 and 11). A world point maps to
    // (dot(right, p - eye), dot(up, p - eye), dot(forward, p - eye)), the same x-right, y-up, z-forward
    // space the character casters and the depth reconstruction use.
    void stdRenderBabylon_WorldToView(const float aCamera[12], shadow::Matrix4* pOut)
    {
        if ( !aCamera || !pOut )
        {
            return;
        }

        const float* pRight   = &aCamera[0];
        const float* pUp      = &aCamera[4];
        const float* pForward = &aCamera[8];
        const float eyeX = aCamera[3];
        const float eyeY = aCamera[7];
        const float eyeZ = aCamera[11];

        pOut->m[0]  = pRight[0];
        pOut->m[1]  = pRight[1];
        pOut->m[2]  = pRight[2];
        pOut->m[3]  = -(pRight[0] * eyeX + pRight[1] * eyeY + pRight[2] * eyeZ);
        pOut->m[4]  = pUp[0];
        pOut->m[5]  = pUp[1];
        pOut->m[6]  = pUp[2];
        pOut->m[7]  = -(pUp[0] * eyeX + pUp[1] * eyeY + pUp[2] * eyeZ);
        pOut->m[8]  = pForward[0];
        pOut->m[9]  = pForward[1];
        pOut->m[10] = pForward[2];
        pOut->m[11] = -(pForward[0] * eyeX + pForward[1] * eyeY + pForward[2] * eyeZ);
        pOut->m[12] = 0.0f;
        pOut->m[13] = 0.0f;
        pOut->m[14] = 0.0f;
        pOut->m[15] = 1.0f;
    }

    // Added: frees the level-shadow receive target. Called with the character-shadow targets when the
    // scene size changes and when the back buffer goes.
    void stdRenderBabylon_FreeLevelShadowTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.levelShadowColorTarget);
        stdRenderBabylon_state.levelShadowColorTarget = {};
        stdRenderBabylon_state.levelShadowWidth       = 0;
        stdRenderBabylon_state.levelShadowHeight      = 0;
        stdRenderBabylon_state.bLevelMapRecorded      = false;
        stdRenderBabylon_state.bLevelShadowsRan       = false;
    }

    // Added: records the level-shadow map. It draws only the static level set, transformed from world
    // space, and fits the light around the same character casters the character map used through a
    // fit-only batch, so both maps share one light and one footprint. False, logging nothing, when the
    // stage is off, this frame has no character map or caster mask, there are no static casters, or the
    // map refuses the batches; bLevelMapRecorded stays false then.
    bool stdRenderBabylon_RecordLevelShadowMap(render::CommandList& list)
    {
        if ( !stdRenderBabylon_state.bShadows
            || !stdRenderBabylon_state.bHasCameraTransform
            || !stdRenderBabylon_state.bShadowMapRecorded
            || !stdRenderBabylon_state.bShadowMaskRan
            || stdRenderBabylon_state.bLevelShadowsSetupFailed )
        {
            return false;
        }

        if ( !stdRenderBabylon_EnsureLevelShadowMap()
            || !stdRenderBabylon_EnsureLevelStaticCasters()
            || !stdRenderBabylon_state.pLevelStaticCasters
            || stdRenderBabylon_state.pLevelStaticCasters->numVertices() == 0 )
        {
            return false;
        }

        const std::vector<float>& aPositions =
            std3DBabylonInternal::std3DBabylon_GetShadowCasterPositions();
        if ( aPositions.empty() )
        {
            return false;
        }

        // Both matrices come from the frame's one camera matrix.
        const float* pCamera = stdRenderBabylon_state.aLastCameraMatrix;

        shadow::CasterBatch aBatches[2];
        aBatches[0].pStatic = stdRenderBabylon_state.pLevelStaticCasters.get();
        stdRenderBabylon_WorldToView(pCamera, &aBatches[0].toView);
        aBatches[1].pPositions  = aPositions.data();
        aBatches[1].numVertices = aPositions.size() / 3u;
        aBatches[1].fitOnly     = true;

        // Added: M11.6f. Anchor the level fit in the world like the character map, so a camera
        // rotation about its own position leaves a fixed caster's light fit unchanged.
        shadow::Matrix4 viewToWorld;
        stdRenderBabylon_ViewToWorld(pCamera, &viewToWorld);
        stdRenderBabylon_state.pLevelShadowMap->setViewToWorld(viewToWorld);

        float aLight[3];
        stdRenderBabylon_ContactLightDirection(pCamera, aLight);
        if ( !stdRenderBabylon_state.pLevelShadowMap->record(list, aLight,
                std::span<const shadow::CasterBatch>(aBatches, 2)) )
        {
            return false;
        }

        stdRenderBabylon_state.bLevelMapRecorded = true;
        return true;
    }

    // Added: the level-shadow receive pass over the same scene source the character receive would read.
    // It applies the level map only to the caster mask's pixels (include mode), so level geometry shadows
    // the characters and nothing else. It runs just before the character receive, which then reads its
    // output as the scene source. False, logging nothing, when the frame has no level map or mask, a
    // resource is missing, or the pass fails.
    bool stdRenderBabylon_RecordLevelShadowReceive(render::CommandList& list)
    {
        if ( !stdRenderBabylon_state.bShadows
            || !stdRenderBabylon_state.bLevelMapRecorded
            || !stdRenderBabylon_state.bShadowMaskRan
            || stdRenderBabylon_state.shadowMaskTarget.isNull()
            || stdRenderBabylon_state.colorTarget.isNull()
            || stdRenderBabylon_state.depthTarget.isNull()
            || stdRenderBabylon_state.normalTarget.isNull()
            || !stdRenderBabylon_state.bDepthSampled
            || !stdRenderBabylon_state.bHasCameraTransform
            || !stdRenderBabylon_state.pLevelShadowMap
            || !stdRenderBabylon_state.pLevelShadowMap->isOpen()
            || !stdRenderBabylon_state.pPostfxResources
            || !stdRenderBabylon_state.pPostfxTargets
            || !stdRenderBabylon_state.pPostfxLevelShadowReceive )
        {
            return false;
        }

        const uint32_t width  = stdRenderBabylon_state.width;
        const uint32_t height = stdRenderBabylon_state.height;
        if ( !stdRenderBabylon_EnsureLevelShadowTarget(width, height) )
        {
            return false;
        }

        const render::TextureHandle scene = stdRenderBabylon_state.bContactShadowsRan
            ? stdRenderBabylon_state.contactShadowColorTarget
            : (stdRenderBabylon_state.bWaterRan
                ? stdRenderBabylon_state.waterColorTarget : stdRenderBabylon_ChainSource());

        float tanHalfX = 0.0f;
        float tanHalfY = 0.0f;
        stdRenderBabylon_SsaoProjection(width, height, &tanHalfX, &tanHalfY);
        const float nearPlane = std::max(stdRenderBabylon_state.projectionNearPlane, 1.0e-5f);
        const float farPlane  = std::max(stdRenderBabylon_state.projectionFarPlane,
            nearPlane + 1.0e-3f);

        postfx::ShadowReceiveSettings settings;
        settings.strength            = stdRenderBabylon_kLevelShadowStrength;
        settings.penumbra            = stdRenderBabylon_kLevelShadowPenumbra;
        settings.penumbraPerDistance = stdRenderBabylon_kLevelShadowPenumbraPerDistance;
        settings.fadeStart           = stdRenderBabylon_kLevelShadowFadeStart;
        settings.fadeEnd             = stdRenderBabylon_kLevelShadowFadeEnd;
        settings.minLight            = stdRenderBabylon_kLevelShadowMinLight;
        settings.fullLight           = stdRenderBabylon_kLevelShadowFullLight;
        settings.receiverClasses     = 0x8; // only ordinary surfaces, class 3 of the surface tags
        stdRenderBabylon_state.pPostfxLevelShadowReceive->setSettings(settings);

        postfx::ShadowMapInput shadowMap;
        shadowMap.texture        = stdRenderBabylon_state.pLevelShadowMap->texture();
        shadowMap.compareSampler = stdRenderBabylon_state.pLevelShadowMap->compareSampler();
        shadowMap.mapSize        = stdRenderBabylon_state.pLevelShadowMap->size();
        const shadow::Matrix4 viewToShadow =
            stdRenderBabylon_state.pLevelShadowMap->viewToShadowTexture();
        std::memcpy(shadowMap.matrix, viewToShadow.m, sizeof(shadowMap.matrix));
        shadowMap.towardLight[0] = stdRenderBabylon_state.aShadowLightDirection[0];
        shadowMap.towardLight[1] = stdRenderBabylon_state.aShadowLightDirection[1];
        shadowMap.towardLight[2] = stdRenderBabylon_state.aShadowLightDirection[2];
        shadowMap.excludeMask    = stdRenderBabylon_state.shadowMaskTarget;
        shadowMap.maskMode       = postfx::MaskMode::Include;
        stdRenderBabylon_state.pPostfxLevelShadowReceive->setShadowMap(shadowMap);

        postfx::FrameInputs frame;
        frame.color       = scene;
        frame.colorFormat = stdRenderBabylon_state.colorFormat;
        frame.depth       = stdRenderBabylon_state.depthTarget;
        frame.normals     = stdRenderBabylon_state.normalTarget;
        frame.width       = width;
        frame.height      = height;
        frame.projection.nearPlane   = nearPlane;
        frame.projection.farPlane    = farPlane;
        frame.projection.tanHalfFovX = tanHalfX;
        frame.projection.tanHalfFovY = tanHalfY;
        frame.projection.depthNear   = 0.1f;
        frame.projection.depthFar    = 1.0f;

        postfx::PassContext pass{ list, *stdRenderBabylon_state.pPostfxResources,
            *stdRenderBabylon_state.pPostfxTargets, frame, scene,
            stdRenderBabylon_state.colorFormat, width, height,
            stdRenderBabylon_state.levelShadowColorTarget,
            stdRenderBabylon_state.colorFormat, width, height };
        if ( !stdRenderBabylon_state.pPostfxLevelShadowReceive->record(pass) )
        {
            return false;
        }

        stdRenderBabylon_state.bLevelShadowsRan = true;
        return true;
    }
}

#ifdef J3D_TEST
namespace stdRenderBabylonInternal
{
    std::size_t stdRenderBabylon_GetLevelStaticCasterUploadCountForTest()
    {
        return stdRenderBabylon_numLevelStaticCasterUploads;
    }

    render::BufferHandle stdRenderBabylon_GetLevelStaticCasterBufferForTest()
    {
        if ( !stdRenderBabylon_state.pLevelStaticCasters )
        {
            return render::BufferHandle{};
        }
        return stdRenderBabylon_state.pLevelStaticCasters->buffer();
    }

    bool stdRenderBabylon_GetLevelShadowReceiveSettingsForTest(
        postfx::ShadowReceiveSettings* pSettings)
    {
        if ( !stdRenderBabylon_state.pPostfxLevelShadowReceive )
        {
            return false;
        }
        if ( pSettings )
        {
            *pSettings = stdRenderBabylon_state.pPostfxLevelShadowReceive->settings();
        }
        return true;
    }
}
#endif
