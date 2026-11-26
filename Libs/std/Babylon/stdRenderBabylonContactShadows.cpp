// The screen-space contact-shadow stage of the Babylon render adapter.
// Part of the Babylon column's render frame owner; see stdRenderBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <babylon/postfx/ContactShadowEffect.h>
#include <babylon/postfx/Effect.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/Resources.h>
#include <babylon/postfx/TargetPool.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <std/General/std.h>

#include "stdBabylon.h"
#include "stdRenderBabylon.h"
#include "stdRenderBabylonInternal.h"

namespace render = babylon::render;
namespace postfx = babylon::postfx;
using namespace stdRenderBabylonInternal;

namespace
{
    // Added: the fixed overhead light of the M11.1 shadows, as an angle from vertical and an
    // azimuth in the Z-up game world. Like every value of the stage it is presentation only.
    constexpr float stdRenderBabylon_kContactLightTilt    = 35.0f; // degrees from vertical
    constexpr float stdRenderBabylon_kContactLightAzimuth = 30.0f; // degrees about the world Z axis
    constexpr float stdRenderBabylon_kContactPi           = 3.14159265358979323846f;

    // Added: the contact-shadow stage's scene-sized target, made when its size changes. False,
    // logging why, when it can't be created.
    bool stdRenderBabylon_EnsureContactShadowTargets(uint32_t width, uint32_t height)
    {
        if ( width == 0 || height == 0 )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.contactShadowColorTarget.isNull()
            && stdRenderBabylon_state.contactShadowWidth == width
            && stdRenderBabylon_state.contactShadowHeight == height )
        {
            return true;
        }

        stdRenderBabylon_FreeContactShadowTargets();

        render::TextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.mipLevels = 1;
        desc.format    = stdRenderBabylon_state.colorFormat;
        desc.usage     = render::TextureUsage::ColorTarget | render::TextureUsage::Sampled;

        const render::TextureHandle target = stdRenderBabylon_state.pDevice->createTexture(desc);
        if ( target.isNull() )
        {
            STDLOG_ERROR("stdRenderBabylon: could not create the %ux%u contact-shadow target.\n",
                width, height);
            return false;
        }

        stdRenderBabylon_state.contactShadowColorTarget = target;
        stdRenderBabylon_state.contactShadowWidth       = width;
        stdRenderBabylon_state.contactShadowHeight      = height;
        return true;
    }

    // Added: the contact-shadow stage's resources: the Babylon postfx objects the camera effects
    // already own, made when the frame owner opens. False, logging nothing again, when they are
    // unavailable or the setup already failed.
    bool stdRenderBabylon_EnsureContactShadowResources()
    {
        if ( stdRenderBabylon_state.bContactShadowsSetupFailed )
        {
            return false;
        }

        if ( !stdRenderBabylon_state.pPostfxResources
            || !stdRenderBabylon_state.pPostfxTargets
            || !stdRenderBabylon_state.pPostfxContactShadows )
        {
            STDLOG_ERROR("stdRenderBabylon: could not set up the contact-shadow stage; the postfx objects are unavailable.\n");
            stdRenderBabylon_state.bContactShadowsSetupFailed = true;
            return false;
        }

        return true;
    }

}

namespace stdRenderBabylonInternal
{
    // Added: the game world's direction toward the light in the shader camera's view space. aCamera
    // is the 12-float shader camera (right, up and forward in world coordinates, elements 0-2, 4-6
    // and 8-10); the result is (dot(right, Lw), dot(up, Lw), dot(forward, Lw)). Shared with the
    // character-shadow stage (M11.2d).
    void stdRenderBabylon_ContactLightDirection(const float aCamera[12], float aDirection[3])
    {
        const float tilt    = stdRenderBabylon_kContactLightTilt * stdRenderBabylon_kContactPi / 180.0f;
        const float azimuth = stdRenderBabylon_kContactLightAzimuth * stdRenderBabylon_kContactPi / 180.0f;
        const float aWorld[3] = { std::sin(tilt) * std::cos(azimuth),
            std::sin(tilt) * std::sin(azimuth), std::cos(tilt) };

        const float* pRight   = &aCamera[0];
        const float* pUp      = &aCamera[4];
        const float* pForward = &aCamera[8];
        aDirection[0] = pRight[0] * aWorld[0] + pRight[1] * aWorld[1] + pRight[2] * aWorld[2];
        aDirection[1] = pUp[0] * aWorld[0] + pUp[1] * aWorld[1] + pUp[2] * aWorld[2];
        aDirection[2] = pForward[0] * aWorld[0] + pForward[1] * aWorld[1] + pForward[2] * aWorld[2];
    }

    // Added: frees the contact-shadow stage's target. Recreated by the next present with the stage
    // on after a scene size or render scale change.
    void stdRenderBabylon_FreeContactShadowTargets()
    {
        stdRenderBabylon_DestroyTexture(stdRenderBabylon_state.contactShadowColorTarget);
        stdRenderBabylon_state.contactShadowColorTarget = {};
        stdRenderBabylon_state.contactShadowWidth       = 0;
        stdRenderBabylon_state.contactShadowHeight      = 0;
        stdRenderBabylon_state.bContactShadowsRan       = false;
    }

    // Added: the contact-shadow stage over the scene source (the water-ripple output or the post
    // chain's). It marches a short ray through the depth target toward the fixed overhead light
    // and darkens ordinary surfaces whose path is blocked; sky, water, empty pixels and the
    // first-person overlay stay unchanged. Without a camera transform yet the light direction is
    // unknown, so the stage is skipped. bContactShadowsRan is set afterwards so ambient occlusion
    // reads the result. False, having logged nothing, when a resource or pass fails.
    bool stdRenderBabylon_RecordContactShadows(render::CommandList& list)
    {
        if ( !stdRenderBabylon_state.bContactShadows
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
        if ( !stdRenderBabylon_EnsureContactShadowTargets(width, height)
            || !stdRenderBabylon_EnsureContactShadowResources() )
        {
            return false;
        }

        const render::TextureHandle scene = stdRenderBabylon_state.bWaterRan
            ? stdRenderBabylon_state.waterColorTarget : stdRenderBabylon_ChainSource();

        // The rdCamera's fov is horizontal at the 4:3 reference aspect; the helper gives the
        // vertical tangent first and the horizontal one for this scene's real aspect.
        float tanHalfX = 0.0f;
        float tanHalfY = 0.0f;
        stdRenderBabylon_SsaoProjection(width, height, &tanHalfX, &tanHalfY);
        const float nearPlane = std::max(stdRenderBabylon_state.projectionNearPlane, 1.0e-5f);
        const float farPlane  = std::max(stdRenderBabylon_state.projectionFarPlane,
            nearPlane + 1.0e-3f);

        postfx::ContactShadowSettings settings;
        settings.receiverClasses = 0x8; // only ordinary surfaces, class 3 of the surface tags
        stdRenderBabylon_state.pPostfxContactShadows->setSettings(settings);

        float aLight[3];
        stdRenderBabylon_ContactLightDirection(stdRenderBabylon_state.aLastCameraMatrix, aLight);
        stdRenderBabylon_state.pPostfxContactShadows->setLightDirection(aLight[0], aLight[1],
            aLight[2]);

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
        // rdCamera's scene depth is 0.1 at the near plane and 1.0 at the far plane; below 0.1 is
        // the first-person overlay and no scene surface.
        frame.projection.depthNear = 0.1f;
        frame.projection.depthFar  = 1.0f;

        postfx::PassContext pass{ list, *stdRenderBabylon_state.pPostfxResources,
            *stdRenderBabylon_state.pPostfxTargets, frame, scene,
            stdRenderBabylon_state.colorFormat, width, height,
            stdRenderBabylon_state.contactShadowColorTarget,
            stdRenderBabylon_state.colorFormat, width, height };
        if ( !stdRenderBabylon_state.pPostfxContactShadows->record(pass) )
        {
            return false;
        }

        stdRenderBabylon_state.bContactShadowsRan = true;
        return true;
    }

}

bool stdRenderBabylon_SetContactShadows(bool bEnabled)
{
    if ( !stdRenderBabylon_state.pDevice )
    {
        STDLOG_ERROR("stdRenderBabylon_SetContactShadows: not open.\n");
        return false;
    }

    stdRenderBabylon_state.bContactShadows            = bEnabled;
    stdRenderBabylon_state.bContactShadowsSetupFailed = false;
    stdRenderBabylon_state.bContactShadowsRan         = false;
    return true;
}

bool stdRenderBabylon_GetContactShadows()
{
    return stdRenderBabylon_state.bContactShadows;
}

#ifdef J3D_TEST
bool stdRenderBabylon_GetContactLightDirectionForTest(const float aCamera[12], float* pX,
    float* pY, float* pZ)
{
    if ( !aCamera )
    {
        return false;
    }

    float aDirection[3];
    stdRenderBabylon_ContactLightDirection(aCamera, aDirection);
    if ( pX )
    {
        *pX = aDirection[0];
    }
    if ( pY )
    {
        *pY = aDirection[1];
    }
    if ( pZ )
    {
        *pZ = aDirection[2];
    }
    return true;
}
#endif
