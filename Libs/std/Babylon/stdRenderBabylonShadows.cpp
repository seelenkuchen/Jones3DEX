// The character-shadow stage of the Babylon render adapter (M11.2d): the overhead shadow map of the
// casters the std3D draw path collected and the receive pass over the HDR scene.
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

#include <algorithm>
#include <cmath>
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
    // Added: M11.6b. The character-shadow receive look. The game's scale has Indy about 0.2 view
    // units tall, so the fades are a few body lengths: the penumbra widens with the receiver's gap
    // behind the caster, and the shadow is gone before the level's far geometry.
    constexpr float stdRenderBabylon_kCharacterShadowStrength            = 0.4f;   // 1 - strength is a fully shadowed pixel's brightness
    constexpr float stdRenderBabylon_kCharacterShadowPenumbra            = 0.002f; // view units: minimum soft edge
    constexpr float stdRenderBabylon_kCharacterShadowPenumbraPerDistance = 0.03f;  // extra view units per view unit behind the map
    constexpr float stdRenderBabylon_kCharacterShadowDistanceFadeStart   = 0.3f;   // view units behind the map's far side where the fade starts
    constexpr float stdRenderBabylon_kCharacterShadowDistanceFadeEnd     = 1.0f;   // where the shadow behind the caster is gone
    constexpr float stdRenderBabylon_kCharacterShadowFadeStart           = 3.0f;   // view distance where the shadow starts to fade
    constexpr float stdRenderBabylon_kCharacterShadowFadeEnd             = 6.0f;   // view distance where it is gone
    constexpr float stdRenderBabylon_kCharacterShadowMinLight            = 0.03f;  // luminance below which no shadow applies
    constexpr float stdRenderBabylon_kCharacterShadowFullLight           = 0.25f;  // luminance at which the full shadow applies

    // Added: the character-shadow stage's resources: the Babylon shadow map and postfx receive
    // effect, made when the frame owner opens or the option turns on. False, logging nothing again,
    // when they are unavailable or the setup already failed.
    bool stdRenderBabylon_EnsureShadowResources()
    {
        if ( stdRenderBabylon_state.bShadowsSetupFailed )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.pPostfxResources
            || !stdRenderBabylon_state.pPostfxTargets
            || !stdRenderBabylon_state.pPostfxShadowReceive
            || !stdRenderBabylon_state.pShadowMap
            || !stdRenderBabylon_state.pShadowMap->isOpen() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not set up the character-shadow stage; the shadow objects are unavailable.\n");
            stdRenderBabylon_state.bShadowsSetupFailed = true;
            return false;
        }

        return true;
    }
}

namespace stdRenderBabylonInternal
{
    // Added: the character-shadow stage's scene-sized target, made when its size changes. False,
    // logging why, when it can't be created. The caster mask shares the size and is made by its own
    // stage after this one, so the mask pass calls this before it creates its target.
    bool stdRenderBabylon_EnsureShadowTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.shadowColorTarget.isNull()
            && stdRenderBabylon_state.shadowWidth == width
            && stdRenderBabylon_state.shadowHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeShadowTargets();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u character-shadow target.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.shadowColorTarget = target;
        stdRenderBabylon_state.shadowWidth       = width;
        stdRenderBabylon_state.shadowHeight      = height;
        return true;
    }

    // Added: opens the character-shadow map on first use. A build or test with the stage off never
    // allocates it, so it records no commands then. False, having logged once, when it can't be made.
    bool stdRenderBabylon_EnsureShadowMap()
    {
        if ( stdRenderBabylon_state.pShadowMap )
        {
            return stdRenderBabylon_state.pShadowMap->isOpen();
        }

        if ( !stdRenderBabylon_state.pDevice )
        {
            return false;
        }

        std::unique_ptr<shadow::DirectionalShadowMap> pShadowMap =
            std::make_unique<shadow::DirectionalShadowMap>(*stdRenderBabylon_state.pDevice);
        shadow::ShadowMapDesc desc;
        desc.size = stdRenderBabylon_kShadowMapSize;
        if ( !pShadowMap->open(desc) )
        {
            STDLOG_WARNING("stdRenderBabylon: could not open the character-shadow map; the blob shadow stays.\n");
            stdRenderBabylon_state.bShadowsSetupFailed = true;
            return false;
        }

        stdRenderBabylon_state.pShadowMap = std::move(pShadowMap);
        return true;
    }

    // Added: true while the shadow map and receive effect are open. std3D_DrawsThingShadows reads it.
    bool stdRenderBabylon_AreShadowObjectsOpen()
    {
        return stdRenderBabylon_state.pShadowMap
            && stdRenderBabylon_state.pShadowMap->isOpen()
            && stdRenderBabylon_state.pPostfxShadowReceive != nullptr;
    }

    // Added: frees the character-shadow stage's targets. Recreated by the next present with the stage
    // on after a scene size or render scale change.
    void stdRenderBabylon_FreeShadowTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.shadowColorTarget);
        stdRenderBabylon_state.shadowColorTarget = {};
        stdRenderBabylon_state.shadowWidth       = 0;
        stdRenderBabylon_state.shadowHeight      = 0;
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.shadowMaskTarget);
        stdRenderBabylon_state.shadowMaskTarget  = {};
        stdRenderBabylon_state.shadowMaskWidth   = 0;
        stdRenderBabylon_state.shadowMaskHeight  = 0;
        stdRenderBabylon_state.bShadowMaskRan    = false;
        stdRenderBabylon_state.bShadowsRan       = false;
    }

    // Added: records this frame's shadow map from the casters the std3D draw path collected, before
    // the post chain. False, having logged nothing, when the stage is off, no camera transform has
    // arrived, no caster was collected, or the map refuses the casters; bShadowMapRecorded stays
    // false so the receive pass is skipped.
    bool stdRenderBabylon_RecordShadowMap(render::CommandList& list)
    {
        if ( !stdRenderBabylon_state.bShadows
            || !stdRenderBabylon_state.bHasCameraTransform
            || !stdRenderBabylon_EnsureShadowMap() )
        {
            return false;
        }

        const std::vector<float>& aPositions = std3DBabylonInternal::std3DBabylon_GetShadowCasterPositions();
        if ( aPositions.empty() )
        {
            return false;
        }

        shadow::CasterBatch batch;
        batch.pPositions = aPositions.data();
        batch.numVertices = aPositions.size() / 3u;

        float aLight[3];
        stdRenderBabylon_ContactLightDirection(stdRenderBabylon_state.aLastCameraMatrix, aLight);
        if ( !stdRenderBabylon_state.pShadowMap->record(list, aLight,
                std::span<const shadow::CasterBatch>(&batch, 1)) )
        {
            return false;
        }

        std::memcpy(stdRenderBabylon_state.aShadowLightDirection, aLight,
            sizeof(stdRenderBabylon_state.aShadowLightDirection));
        stdRenderBabylon_state.bShadowMapRecorded = true;
        return true;
    }

    // Added: the character-shadow receive pass over the scene source (the contact-shadow output, the
    // water-ripple output or the post chain's), after the contact shadows and before ambient
    // occlusion. It reconstructs each pixel's view position from the depth target, looks it up in
    // the map and darkens ordinary surfaces whose path to the overhead light is blocked. False,
    // having logged nothing, when the frame's map was not recorded or a resource or pass fails.
    bool stdRenderBabylon_RecordShadowReceive(render::CommandList& list)
    {
        if ( !stdRenderBabylon_state.bShadows
            || !stdRenderBabylon_state.bShadowMapRecorded
            || stdRenderBabylon_state.colorTarget.isNull()
            || stdRenderBabylon_state.depthTarget.isNull()
            || stdRenderBabylon_state.normalTarget.isNull()
            || !stdRenderBabylon_state.bDepthSampled
            || !stdRenderBabylon_state.bHasCameraTransform )
        {
            return false;
        }

        const uint32_t width  = stdRenderBabylon_state.width;
        const uint32_t height = stdRenderBabylon_state.height;
        if ( !stdRenderBabylon_EnsureShadowTargets(width, height)
            || !stdRenderBabylon_EnsureShadowResources() )
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
        settings.strength            = stdRenderBabylon_kCharacterShadowStrength;
        settings.penumbra            = stdRenderBabylon_kCharacterShadowPenumbra;
        settings.penumbraPerDistance = stdRenderBabylon_kCharacterShadowPenumbraPerDistance;
        settings.distanceFadeStart   = stdRenderBabylon_kCharacterShadowDistanceFadeStart;
        settings.distanceFadeEnd     = stdRenderBabylon_kCharacterShadowDistanceFadeEnd;
        settings.fadeStart           = stdRenderBabylon_kCharacterShadowFadeStart;
        settings.fadeEnd             = stdRenderBabylon_kCharacterShadowFadeEnd;
        settings.minLight            = stdRenderBabylon_kCharacterShadowMinLight;
        settings.fullLight           = stdRenderBabylon_kCharacterShadowFullLight;
        settings.receiverClasses     = 0x8; // only ordinary surfaces, class 3 of the surface tags
        stdRenderBabylon_state.pPostfxShadowReceive->setSettings(settings);

        postfx::ShadowMapInput shadowMap;
        shadowMap.texture        = stdRenderBabylon_state.pShadowMap->texture();
        shadowMap.compareSampler = stdRenderBabylon_state.pShadowMap->compareSampler();
        shadowMap.mapSize        = stdRenderBabylon_state.pShadowMap->size();
        const shadow::Matrix4 viewToShadow = stdRenderBabylon_state.pShadowMap->viewToShadowTexture();
        std::memcpy(shadowMap.matrix, viewToShadow.m, sizeof(shadowMap.matrix));
        shadowMap.towardLight[0] = stdRenderBabylon_state.aShadowLightDirection[0];
        shadowMap.towardLight[1] = stdRenderBabylon_state.aShadowLightDirection[1];
        shadowMap.towardLight[2] = stdRenderBabylon_state.aShadowLightDirection[2];
        // Added: M11.2g. The caster mask keeps a character's own surface out of the shadow; a frame
        // whose mask stage did not run passes none, as before.
        if ( stdRenderBabylon_state.bShadowMaskRan )
        {
            shadowMap.excludeMask = stdRenderBabylon_state.shadowMaskTarget;
        }
        stdRenderBabylon_state.pPostfxShadowReceive->setShadowMap(shadowMap);

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
            stdRenderBabylon_state.shadowColorTarget,
            stdRenderBabylon_state.colorFormat, width, height };
        if ( !stdRenderBabylon_state.pPostfxShadowReceive->record(pass) )
        {
            return false;
        }

        stdRenderBabylon_state.bShadowsRan = true;
        return true;
    }
}

bool stdRenderBabylon_SetShadows(bool bEnabled)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetShadows: not open.\n");
        return false;
    }

    stdRenderBabylon_state.bShadows            = bEnabled;
    stdRenderBabylon_state.bShadowsSetupFailed = false;
    stdRenderBabylon_state.bShadowsRan         = false;
    if ( bEnabled )
    {
        // Make the map now, so std3D_DrawsThingShadows answers at once.
        static_cast<void>(stdRenderBabylon_EnsureShadowMap());
    }
    return true;
}

bool stdRenderBabylon_GetShadows()
{
    return stdRenderBabylon_state.bShadows;
}

#ifdef J3D_TEST
bool stdRenderBabylon_GetShadowReceiveSettingsForTest(postfx::ShadowReceiveSettings* pSettings)
{
    if ( !stdRenderBabylon_state.pPostfxShadowReceive )
    {
        return false;
    }
    if ( pSettings )
    {
        *pSettings = stdRenderBabylon_state.pPostfxShadowReceive->settings();
    }
    return true;
}
#endif
