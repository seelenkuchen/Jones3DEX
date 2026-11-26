// The std3DBabylonGpu character-shadow tests (M11.2d): the pass order on the Null device and the
// darkening of a floor below a caster on the SDL GPU device.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace std3DBabylonTest;

TEST_GROUP(std3DBabylonGpu);

void TEST_std3DBabylonGpu_SETUP(void);
void TEST_std3DBabylonGpu_TEAR_DOWN(void);

namespace
{
    constexpr uint32_t kSceneSize = 64u;

    // Everything but the character-shadow stage off, so only it changes the picture.
    void UseShadowOnly()
    {
        TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(0));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(false));
        stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);
    }

    // The same camera as the contact-shadow tests: looking along +Y with world Z up.
    void SetShadowCamera()
    {
        const float aMatrix[12] = {
            1.0f, 0.0f, 0.0f, // right
            0.0f, 1.0f, 0.0f, // forward
            0.0f, 0.0f, 1.0f, // up
            0.0f, 0.0f, 0.0f
        };
        stdRenderBabylon_SetCameraTransform(aMatrix, true);
    }

    // A caster quad above the floor and farther from the camera. The light has a forward component,
    // so the shadow falls toward the camera: the caster at forward 2.5 shades the floor at forward 2.0
    // below and to the left of it. The points are rdroid camera space (x right, y forward, z up).
    void DrawShadowCasterQuad()
    {
        const float aPositions[12] = {
            0.766f, 2.5f, 0.7f,
            0.966f, 2.5f, 0.7f,
            0.966f, 2.5f, 0.9f,
            0.766f, 2.5f, 0.9f,
        };
        std3D_AddShadowCasterPolygon(aPositions, 4u);
    }

    // A closed box around the caster quad, submitted face by face and never drawn. Its camera-facing
    // faces are not part of the rendered scene, so the shadow under the floor can only come from the
    // whole model the polygon call carries.
    void AddClosedBoxShadowCaster()
    {
        const float x0 = 0.766f;
        const float x1 = 0.966f;
        const float y0 = 2.45f;
        const float y1 = 2.55f;
        const float z0 = 0.7f;
        const float z1 = 0.9f;

        const float aFaces[6][4][3] = {
            { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } }, // -forward
            { { x0, y1, z0 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 } }, // +forward
            { { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y0, z0 } }, // -up
            { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } }, // +up
            { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } }, // -right
            { { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 } }, // +right
        };
        for ( size_t i = 0; i < 6u; ++i )
        {
            std3D_AddShadowCasterPolygon(&aFaces[i][0][0], 4u);
        }
    }

    // A constant-depth floor (forward distance 2), a sky patch, and a caster quad above the floor.
    void DrawShadowScene(bool bSkyAndHud)
    {
        const uint32_t grey = 0xFF909090u;
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f,
            SceneDepthForEyeDepth(2.0f, 0.1f, 100.0f), grey);

        DrawShadowCasterQuad();

        if ( bSkyAndHud )
        {
            DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 8.0f, 8.0f,
                SceneDepthForEyeDepth(1.0f, 0.1f, 100.0f), 0xFF204060u);
            std3D_BeginHudLayer();
            DrawQuad(nullptr, (Std3DRenderState)0, 40.0f, 4.0f, 44.0f, 8.0f, 0.25f,
                0xFF00FF00u);
            std3D_EndHudLayer();
        }
    }

    // The same scene, but its only caster is the closed box through the polygon call; no caster is
    // drawn, so the camera-facing box faces never reach the screen.
    void DrawClosedBoxShadowScene()
    {
        const uint32_t grey = 0xFF909090u;
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f,
            SceneDepthForEyeDepth(2.0f, 0.1f, 100.0f), grey);
        AddClosedBoxShadowCaster();
    }

    struct RecordedPass
    {
        render::BeginPass begin;
        std::vector<render::BindTexture> aTextures;
    };

    std::vector<RecordedPass> RecordedPasses()
    {
        std::vector<RecordedPass> aPasses;
        for ( const render::RecordedCommand& command : Null().recorded() )
        {
            if ( const render::BeginPass* pBegin = std::get_if<render::BeginPass>(&command) )
            {
                RecordedPass pass;
                pass.begin = *pBegin;
                aPasses.push_back(pass);
            }
            else if ( const render::BindTexture* pBind = std::get_if<render::BindTexture>(&command) )
            {
                if ( !aPasses.empty() )
                {
                    aPasses.back().aTextures.push_back(*pBind);
                }
            }
        }
        return aPasses;
    }

    render::TextureHandle PassTarget(const RecordedPass& pass)
    {
        return pass.begin.desc.colorTargets.empty()
            ? render::TextureHandle{} : pass.begin.desc.colorTargets[0].target;
    }

    const render::BindTexture* FindSlot(const RecordedPass& pass, uint32_t slot)
    {
        for ( const render::BindTexture& bind : pass.aTextures )
        {
            if ( bind.slot == slot )
            {
                return &bind;
            }
        }
        return nullptr;
    }
}

TEST(std3DBabylonGpu, ShadowMapRunsBeforeThePostChainAndReceiveBetweenContactAndSsao)
{
    UseNullDevice();
    OpenStd3D(kSceneSize, kSceneSize);

    UseShadowOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(true));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(1));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    TEST_ASSERT_TRUE(stdRenderBabylon_AddPostPass(std3DBabylonTest::shaders::postInvertFrag()));
    SetShadowCamera();

    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f,
        SceneDepthForEyeDepth(2.0f, 0.1f, 100.0f), 0xFF909090u);
    DrawShadowCasterQuad();
    EndFrame();

    const std::vector<RecordedPass> aPasses = RecordedPasses();
    const render::TextureHandle depthTarget  = stdRenderBabylon_GetDepthTarget();
    const render::TextureHandle normalTarget = stdRenderBabylon_GetNormalTarget();
    const render::TextureHandle sceneTarget  = stdRenderBabylon_GetColorTarget();

    // The shadow-map pass is the depth-only pass, and it comes before the post chain.
    size_t mapIndex = aPasses.size();
    for ( size_t i = 0; i < aPasses.size(); ++i )
    {
        if ( aPasses[i].begin.desc.colorTargets.empty()
            && aPasses[i].begin.desc.depthTarget.has_value()
            && !aPasses[i].begin.desc.depthTarget->target.isNull() )
        {
            mapIndex = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(mapIndex < aPasses.size());
    const render::TextureHandle mapTarget = aPasses[mapIndex].begin.desc.depthTarget->target;

    size_t postIndex = aPasses.size();
    for ( size_t i = mapIndex + 1u; i < aPasses.size(); ++i )
    {
        const render::BindTexture* pSlot0 = FindSlot(aPasses[i], 0u);
        if ( pSlot0 && pSlot0->texture == sceneTarget
            && !aPasses[i].begin.desc.colorTargets.empty()
            && PassTarget(aPasses[i]) != sceneTarget )
        {
            postIndex = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(postIndex < aPasses.size());
    TEST_ASSERT_TRUE(mapIndex < postIndex);

    // The receive pass reads the map in slot 3 and writes its own target.
    size_t receiveIndex = aPasses.size();
    for ( size_t i = postIndex + 1u; i < aPasses.size(); ++i )
    {
        const render::BindTexture* pSlot3 = FindSlot(aPasses[i], 3u);
        if ( pSlot3 && pSlot3->texture == mapTarget )
        {
            receiveIndex = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(receiveIndex < aPasses.size());
    const render::TextureHandle receiveOutput = PassTarget(aPasses[receiveIndex]);

    // The last contact-shadow pass is the one whose first two inputs are depth and normals. It
    // comes before the receive pass.
    size_t contactCompositeIndex = aPasses.size();
    for ( size_t i = postIndex + 1u; i < receiveIndex; ++i )
    {
        const render::BindTexture* pSlot0 = FindSlot(aPasses[i], 0u);
        const render::BindTexture* pSlot1 = FindSlot(aPasses[i], 1u);
        if ( pSlot0 && pSlot1 && pSlot0->texture == depthTarget && pSlot1->texture == normalTarget )
        {
            contactCompositeIndex = i;
        }
    }
    TEST_ASSERT_TRUE(contactCompositeIndex < receiveIndex);

    // Ambient occlusion's composite reads the receive output in slot 0, after it.
    size_t ssaoIndex = aPasses.size();
    for ( size_t i = receiveIndex + 1u; i < aPasses.size(); ++i )
    {
        const render::BindTexture* pSlot0 = FindSlot(aPasses[i], 0u);
        if ( pSlot0 && pSlot0->texture == receiveOutput )
        {
            ssaoIndex = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(ssaoIndex < aPasses.size());
    TEST_ASSERT_TRUE(receiveIndex < ssaoIndex);
    TEST_ASSERT_FALSE(receiveOutput.isNull());
}

TEST(std3DBabylonGpu, ShadowsOffLeavesTheImageByteIdentical)
{
    if ( !OpenGpuStd3D(kSceneSize, kSceneSize) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseShadowOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(false));
    SetShadowCamera();

    RenderScene([] { DrawShadowScene(false); });
    const std::vector<std::byte> off = CapturePresented(kSceneSize, kSceneSize);
    RenderScene([] { DrawShadowScene(false); });
    const std::vector<std::byte> offAgain = CapturePresented(kSceneSize, kSceneSize);
    TEST_ASSERT_EQUAL_size_t(off.size(), offAgain.size());
    TEST_ASSERT_EQUAL_MEMORY(off.data(), offAgain.data(), off.size());
}

TEST(std3DBabylonGpu, ShadowsDarkenTheFloorBelowTheCasterAndLeaveFarSkyAndHud)
{
    if ( !OpenGpuStd3D(kSceneSize, kSceneSize) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseShadowOnly();
    SetShadowCamera();

    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(false));
    RenderScene([] { DrawShadowScene(true); });
    const std::vector<std::byte> off = CapturePresented(kSceneSize, kSceneSize);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    RenderScene([] { DrawShadowScene(true); });
    const std::vector<std::byte> on = CapturePresented(kSceneSize, kSceneSize);

    // The floor below the caster (the light comes from +x, +up and +forward) darkens.
    const float offShadow = SsaoMeanBrightness(off, kSceneSize, 28u, 42u, 36u, 49u);
    const float onShadow  = SsaoMeanBrightness(on, kSceneSize, 28u, 42u, 36u, 49u);
    std::printf("M11.2d character shadows: floor below the caster %.2f off, %.2f on\n",
        (double)offShadow, (double)onShadow);
    TEST_ASSERT_TRUE(onShadow < offShadow - 1.0f);

    // The floor far from the caster is outside the map's footprint and stays as it was.
    const float offFar = SsaoMeanBrightness(off, kSceneSize, 50u, 52u, 60u, 62u);
    const float onFar  = SsaoMeanBrightness(on, kSceneSize, 50u, 52u, 60u, 62u);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, offFar, onFar);

    // A sky-tagged pixel is returned unchanged.
    const size_t skyPixel = ((size_t)4u * kSceneSize + 4u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 0u], (int)on[skyPixel + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 1u], (int)on[skyPixel + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 2u], (int)on[skyPixel + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 3u], (int)on[skyPixel + 3u]);

    // The HUD layer is composited after every scene stage and stays pixel exact.
    const size_t hudPixel = ((size_t)6u * kSceneSize + 42u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 0u], (int)on[hudPixel + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 1u], (int)on[hudPixel + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 2u], (int)on[hudPixel + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 3u], (int)on[hudPixel + 3u]);
}

TEST(std3DBabylonGpu, AClosedBoxItsVisibleFacesAreNotDrawnStillShadowsTheFloor)
{
    if ( !OpenGpuStd3D(kSceneSize, kSceneSize) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseShadowOnly();
    SetShadowCamera();

    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(false));
    RenderScene([] { DrawClosedBoxShadowScene(); });
    const std::vector<std::byte> off = CapturePresented(kSceneSize, kSceneSize);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    RenderScene([] { DrawClosedBoxShadowScene(); });
    const std::vector<std::byte> on = CapturePresented(kSceneSize, kSceneSize);

    // The whole closed box casts, so the floor below it darkens even though no caster face is drawn.
    const float offShadow = SsaoMeanBrightness(off, kSceneSize, 28u, 42u, 36u, 49u);
    const float onShadow  = SsaoMeanBrightness(on, kSceneSize, 28u, 42u, 36u, 49u);
    std::printf("M11.2e closed-box caster: floor below it %.2f off, %.2f on\n",
        (double)offShadow, (double)onShadow);
    TEST_ASSERT_TRUE(onShadow < offShadow - 1.0f);
}
