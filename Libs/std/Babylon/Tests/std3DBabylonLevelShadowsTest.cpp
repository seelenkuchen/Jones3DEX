// The std3DBabylonGpu level-shadow tests (M11.3): the world-to-view matrix, when the second map is
// recorded, the static set's single upload, and the level-receive pass order. They use the Null render
// device, so they never need a GPU.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

#include "../std3DBabylonInternal.h"
#include "../stdRenderBabylonInternal.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

using namespace std3DBabylonTest;
using namespace stdRenderBabylonInternal;

TEST_GROUP(std3DBabylonGpu);

void TEST_std3DBabylonGpu_SETUP(void);
void TEST_std3DBabylonGpu_TEAR_DOWN(void);

namespace
{
    constexpr uint32_t kSceneSize = 64u;

    // Everything but the shadow stage off, so only the two shadow maps change the picture.
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
    }

    // The same camera as the character-shadow tests: looking along +Y with world Z up.
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

    // A character caster in rdroid camera space (x right, y forward, z up), as rdModel3 submits it.
    void AddCharacterCaster()
    {
        const float aPositions[12] = {
            0.766f, 2.5f, 0.7f,
            0.966f, 2.5f, 0.7f,
            0.966f, 2.5f, 0.9f,
            0.766f, 2.5f, 0.9f,
        };
        std3D_AddShadowCasterPolygon(aPositions, 4u);
    }

    // A ledge in world space (x, y forward, z up), above the caster, for the level map.
    void SetLevelCasters()
    {
        const float aPositions[18] = {
            0.70f, 2.30f, 1.00f,
            1.10f, 2.30f, 1.00f,
            1.10f, 2.80f, 1.00f,
            0.70f, 2.30f, 1.00f,
            1.10f, 2.80f, 1.00f,
            0.70f, 2.80f, 1.00f,
        };
        std3D_SetStaticShadowCasters(aPositions, 6u);
    }

    void ClearLevelCasters()
    {
        std3D_SetStaticShadowCasters(nullptr, 0u);
    }

    // A floor at a constant depth and, optionally, a visible caster-flagged quad on it. The mask stage
    // only runs when a caster draw was collected.
    void DrawScene(bool bDrawCasterQuad)
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f,
            SceneDepthForEyeDepth(2.0f, 0.1f, 100.0f), 0xFF909090u);
        if ( bDrawCasterQuad )
        {
            DrawQuad(nullptr, STD3D_RS_SHADOW_CASTER, 30.0f, 42.0f, 33.0f, 45.0f,
                SceneDepthForEyeDepth(2.0f, 0.1f, 100.0f), 0xFFCC3030u);
        }
    }

    bool HasLevelCasters()
    {
        return !std3DBabylonInternal::std3DBabylon_GetStaticShadowCasterPositions().empty();
    }

    struct RecordedPass
    {
        render::BeginPass begin;
        std::vector<render::SetPipeline> aPipelines;
        std::vector<render::BindVertexBuffer> aVertexBuffers;
        std::vector<render::BindTexture> aTextures;
        std::vector<render::Draw> aDraws;
        std::vector<render::DrawIndexed> aIndexedDraws;
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
            else if ( aPasses.empty() )
            {
                continue;
            }
            else if ( const render::SetPipeline* pPipeline =
                std::get_if<render::SetPipeline>(&command) )
            {
                aPasses.back().aPipelines.push_back(*pPipeline);
            }
            else if ( const render::BindVertexBuffer* pBind =
                std::get_if<render::BindVertexBuffer>(&command) )
            {
                aPasses.back().aVertexBuffers.push_back(*pBind);
            }
            else if ( const render::BindTexture* pBind = std::get_if<render::BindTexture>(&command) )
            {
                aPasses.back().aTextures.push_back(*pBind);
            }
            else if ( const render::Draw* pDraw = std::get_if<render::Draw>(&command) )
            {
                aPasses.back().aDraws.push_back(*pDraw);
            }
            else if ( const render::DrawIndexed* pDraw =
                std::get_if<render::DrawIndexed>(&command) )
            {
                aPasses.back().aIndexedDraws.push_back(*pDraw);
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

    // The depth-only shadow-map passes, in recording order: the character map first, the level map
    // second when it runs.
    std::vector<size_t> FindShadowMapPasses(const std::vector<RecordedPass>& aPasses)
    {
        std::vector<size_t> aIndices;
        for ( size_t i = 0; i < aPasses.size(); ++i )
        {
            if ( aPasses[i].begin.desc.colorTargets.empty()
                && aPasses[i].begin.desc.depthTarget.has_value()
                && !aPasses[i].begin.desc.depthTarget->target.isNull() )
            {
                aIndices.push_back(i);
            }
        }
        return aIndices;
    }

    bool BindsBuffer(const RecordedPass& pass, render::BufferHandle buffer)
    {
        for ( const render::BindVertexBuffer& bind : pass.aVertexBuffers )
        {
            if ( bind.buffer == buffer )
            {
                return true;
            }
        }
        return false;
    }

    // The desc the Null device kept for a pipeline it created, so a test can read fixed state such as
    // depthClip.
    const render::PipelineDesc* PipelineDescOf(render::PipelineHandle pipeline)
    {
        for ( const render::RecordedCommand& command : Null().recorded() )
        {
            if ( const render::PipelineCreated* pCreated =
                std::get_if<render::PipelineCreated>(&command) )
            {
                if ( pCreated->pipeline == pipeline )
                {
                    return &pCreated->desc;
                }
            }
        }
        return nullptr;
    }

    size_t FindReceivePass(const std::vector<RecordedPass>& aPasses, render::TextureHandle map)
    {
        for ( size_t i = 0; i < aPasses.size(); ++i )
        {
            const render::BindTexture* pSlot3 = FindSlot(aPasses[i], 3u);
            if ( pSlot3 && pSlot3->texture == map )
            {
                return i;
            }
        }
        return aPasses.size();
    }

    // Runs one scene and returns its passes, with the Null command recording cleared first. The static
    // casters are left as the caller set them, so a second frame sees the same CPU copy.
    std::vector<RecordedPass> RunScene(bool bCharacterCaster, bool bCasterQuad)
    {
        Null().clearRecorded();
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        DrawScene(bCasterQuad);
        if ( bCharacterCaster )
        {
            AddCharacterCaster();
        }
        EndFrame();
        return RecordedPasses();
    }
}

TEST(std3DBabylonGpu, StaticCastersValidateCopyAndClear)
{
    // No device needed: the backend only validates and keeps a CPU copy here.
    ClearLevelCasters();

    const float aPositions[9] = {
        0.0f, 0.0f, 2.0f,
        1.0f, 0.0f, 2.0f,
        0.0f, 1.0f, 2.0f,
    };
    std3D_SetStaticShadowCasters(aPositions, 3u);
    const std::vector<float>& copy =
        std3DBabylonInternal::std3DBabylon_GetStaticShadowCasterPositions();
    TEST_ASSERT_EQUAL_size_t(9u, copy.size());
    TEST_ASSERT_EQUAL_MEMORY(aPositions, copy.data(), sizeof(aPositions));
    TEST_ASSERT_TRUE(std3DBabylonInternal::std3DBabylon_AreStaticShadowCastersDirty());

    // NULL and 0 clear without a warning.
    StdGeneralTest_ResetDebugOutput();
    std3D_SetStaticShadowCasters(nullptr, 0u);
    TEST_ASSERT_TRUE(copy.empty());
    TEST_ASSERT_EQUAL_STRING("", StdGeneralTest_GetDebugOutput());

    // A count that is not a multiple of 3 clears and logs one warning.
    StdGeneralTest_ResetDebugOutput();
    std3D_SetStaticShadowCasters(aPositions, 4u);
    TEST_ASSERT_TRUE(copy.empty());
    TEST_ASSERT_NOT_NULL(std::strstr(StdGeneralTest_GetDebugOutput(), "multiple of 3"));

    // A non-finite coordinate clears and logs one warning.
    float aNan[9];
    std::memcpy(aNan, aPositions, sizeof(aNan));
    aNan[1] = std::numeric_limits<float>::quiet_NaN();
    StdGeneralTest_ResetDebugOutput();
    std3D_SetStaticShadowCasters(aNan, 3u);
    TEST_ASSERT_TRUE(copy.empty());
    TEST_ASSERT_NOT_NULL(std::strstr(StdGeneralTest_GetDebugOutput(), "non-finite"));

    ClearLevelCasters();
}

TEST(std3DBabylonGpu, WorldToViewMapsTheCameraBasisExactly)
{
    // A camera at (1, 2, 3) looking along +Y world, with +Z up.
    const float aCamera[12] = {
        1.0f, 0.0f, 0.0f, 1.0f, // right
        0.0f, 0.0f, 1.0f, 2.0f, // up
        0.0f, 1.0f, 0.0f, 3.0f, // forward
    };
    babylon::shadow::Matrix4 worldToView{};
    stdRenderBabylon_WorldToView(aCamera, &worldToView);

    const auto assertPoint = [&worldToView](const float aWorld[3], float x, float y, float z) {
        float aView[4];
        babylon::shadow::transformPoint(worldToView, aWorld, aView);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, x, aView[0]);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, y, aView[1]);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, z, aView[2]);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, aView[3]);
    };

    const float aFirst[3]  = { 1.0f, 5.0f, 3.0f };
    const float aSecond[3] = { 2.0f, 2.0f, 4.0f };
    assertPoint(aFirst, 0.0f, 0.0f, 3.0f);
    assertPoint(aSecond, 1.0f, 1.0f, 0.0f);

    // A rotated camera at (2, 3, 4): right -Y, up +Z and forward +X world.
    const float aRotated[12] = {
        0.0f, -1.0f, 0.0f, 2.0f, // right
        0.0f, 0.0f, 1.0f, 3.0f,  // up
        1.0f, 0.0f, 0.0f, 4.0f,  // forward
    };
    stdRenderBabylon_WorldToView(aRotated, &worldToView);

    // The point one right, two up and three forward from the eye.
    const float aRotatedWorld[3] = { 5.0f, 2.0f, 6.0f };
    assertPoint(aRotatedWorld, 1.0f, 2.0f, 3.0f);
}

TEST(std3DBabylonGpu, LevelMapOnlyRecordsWithCharactersMaskAndStaticCasters)
{
    UseNullDevice();
    OpenStd3D(kSceneSize, kSceneSize);
    UseShadowOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    SetShadowCamera();

    // The option off: no character map, no level map.
    SetLevelCasters();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(false));
    std::vector<RecordedPass> aPasses = RunScene(true, true);
    TEST_ASSERT_EQUAL_size_t(0u, FindShadowMapPasses(aPasses).size());

    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));

    // No static casters: only the character map.
    ClearLevelCasters();
    aPasses = RunScene(true, true);
    TEST_ASSERT_EQUAL_size_t(1u, FindShadowMapPasses(aPasses).size());

    // No caster draw, so no mask: only the character map.
    SetLevelCasters();
    aPasses = RunScene(true, false);
    TEST_ASSERT_EQUAL_size_t(1u, FindShadowMapPasses(aPasses).size());

    // No character casters, so no character map and no level map.
    SetLevelCasters();
    aPasses = RunScene(false, true);
    TEST_ASSERT_EQUAL_size_t(0u, FindShadowMapPasses(aPasses).size());

    // All present: the character map and then the level map.
    SetLevelCasters();
    aPasses = RunScene(true, true);
    const std::vector<size_t> aMapPasses = FindShadowMapPasses(aPasses);
    TEST_ASSERT_EQUAL_size_t(2u, aMapPasses.size());

    // The level map draws the static set in one non-indexed draw and no per-frame casters.
    const render::BufferHandle staticBuffer = stdRenderBabylon_GetLevelStaticCasterBufferForTest();
    TEST_ASSERT_FALSE(staticBuffer.isNull());
    TEST_ASSERT_TRUE(BindsBuffer(aPasses[aMapPasses[1]], staticBuffer));
    TEST_ASSERT_EQUAL_size_t(1u, aPasses[aMapPasses[1]].aDraws.size());
    TEST_ASSERT_EQUAL_size_t(0u, aPasses[aMapPasses[1]].aIndexedDraws.size());
    TEST_ASSERT_FALSE(BindsBuffer(aPasses[aMapPasses[0]], staticBuffer));

    // The level receive reads the level map; the character receive reads the character map.
    const render::TextureHandle characterMap =
        aPasses[aMapPasses[0]].begin.desc.depthTarget->target;
    const render::TextureHandle levelMap = aPasses[aMapPasses[1]].begin.desc.depthTarget->target;
    const size_t levelReceive = FindReceivePass(aPasses, levelMap);
    const size_t characterReceive = FindReceivePass(aPasses, characterMap);
    TEST_ASSERT_TRUE(levelReceive < aPasses.size());
    TEST_ASSERT_TRUE(characterReceive < aPasses.size());
    TEST_ASSERT_TRUE(levelReceive < characterReceive);

    // The level receive uses the caster mask in include mode (the mask is bound at slot 4), and the
    // character receive then reads its output as the scene source (slot 0).
    const render::BindTexture* pMask = FindSlot(aPasses[levelReceive], 4u);
    TEST_ASSERT_NOT_NULL(pMask);
    const render::BindTexture* pScene = FindSlot(aPasses[characterReceive], 0u);
    TEST_ASSERT_NOT_NULL(pScene);
    TEST_ASSERT_TRUE(pScene->texture == PassTarget(aPasses[levelReceive]));
    TEST_ASSERT_TRUE(PassTarget(aPasses[characterReceive]) != PassTarget(aPasses[levelReceive]));
}

TEST(std3DBabylonGpu, LevelShadowMapOpensWithCasterClippingEnabled)
{
    UseNullDevice();
    OpenStd3D(kSceneSize, kSceneSize);
    UseShadowOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    SetShadowCamera();
    SetLevelCasters();

    // Both maps open on this first frame, so their pipeline creations are in this recording.
    const std::vector<RecordedPass> aPasses = RunScene(true, true);
    const std::vector<size_t> aMapPasses = FindShadowMapPasses(aPasses);
    TEST_ASSERT_EQUAL_size_t(2u, aMapPasses.size());

    // M11.6f. The level map clips casters beyond staticReach instead of clamping them to the near
    // plane, so its pipeline draws with depthClip on.
    TEST_ASSERT_EQUAL_size_t(1u, aPasses[aMapPasses[1]].aPipelines.size());
    const render::PipelineDesc* pLevelPipeline =
        PipelineDescOf(aPasses[aMapPasses[1]].aPipelines[0].pipeline);
    TEST_ASSERT_NOT_NULL(pLevelPipeline);
    TEST_ASSERT_TRUE(pLevelPipeline->depthClip);

    ClearLevelCasters();
}

TEST(std3DBabylonGpu, LevelShadowReceiveEffectGetsTheGentlerLevelShadowSettings)
{
    UseNullDevice();
    OpenStd3D(kSceneSize, kSceneSize);
    UseShadowOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    SetShadowCamera();
    SetLevelCasters();

    RunScene(true, true);

    babylon::postfx::ShadowReceiveSettings settings;
    TEST_ASSERT_TRUE(stdRenderBabylon_GetLevelShadowReceiveSettingsForTest(&settings));
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.3f, settings.strength);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.002f, settings.penumbra);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.03f, settings.penumbraPerDistance);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 3.0f, settings.fadeStart);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 6.0f, settings.fadeEnd);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.03f, settings.minLight);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.25f, settings.fullLight);
    // The receivers are the characters themselves, possibly far below a ledge, so the distance-behind
    // fade stays disabled.
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, settings.distanceFadeStart);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.0f, settings.distanceFadeEnd);
    TEST_ASSERT_EQUAL_UINT32(0x8u, settings.receiverClasses);

    ClearLevelCasters();
}

TEST(std3DBabylonGpu, LevelStaticCastersUploadOnceAndAgainAfterANewUpload)
{
    UseNullDevice();
    OpenStd3D(kSceneSize, kSceneSize);
    UseShadowOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    SetShadowCamera();

    // Mark the CPU copy dirty, then render two frames with the same casters: one upload only.
    SetLevelCasters();
    const size_t before = stdRenderBabylon_GetLevelStaticCasterUploadCountForTest();

    RunScene(true, true);
    const size_t afterFirst = stdRenderBabylon_GetLevelStaticCasterUploadCountForTest();
    TEST_ASSERT_EQUAL_size_t(before + 1u, afterFirst);

    RunScene(true, true);
    TEST_ASSERT_EQUAL_size_t(afterFirst,
        stdRenderBabylon_GetLevelStaticCasterUploadCountForTest());
    const size_t afterSecond = stdRenderBabylon_GetLevelStaticCasterUploadCountForTest();

    // A new upload marks the set dirty, so the next frame uploads it again.
    const float aPositions[9] = {
        0.0f, 0.0f, 2.0f,
        1.0f, 0.0f, 2.0f,
        0.0f, 1.0f, 2.0f,
    };
    std3D_SetStaticShadowCasters(aPositions, 3u);
    RunScene(true, true);
    TEST_ASSERT_EQUAL_size_t(afterSecond + 1u,
        stdRenderBabylon_GetLevelStaticCasterUploadCountForTest());
    TEST_ASSERT_TRUE(HasLevelCasters());

    ClearLevelCasters();
}
