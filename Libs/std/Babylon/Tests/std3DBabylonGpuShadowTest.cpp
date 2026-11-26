// The std3DBabylonGpu character-shadow tests (M11.2d): the pass order on the Null device and the
// darkening of a floor below a caster on the SDL GPU device.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

#include "../std3DBabylonInternal.h"

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

    // M11.3. A static ledge in world space (x, y forward, z up), above the caster, for the level map.
    // With the identity camera it lands at view (x, z, y), above the character casters.
    void SetLevelShadowLedge()
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

        // M11.2g. The same caster is also drawn as a visible, caster-flagged quad on the floor in the
        // shadowed region: it casts onto the floor, but its own pixels must not receive the shadow.
        DrawQuad(nullptr, STD3D_RS_SHADOW_CASTER, 30.0f, 42.0f, 33.0f, 45.0f,
            SceneDepthForEyeDepth(2.0f, 0.1f, 100.0f), 0xFFCC3030u);

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
        std::vector<render::DrawIndexed> aDraws;
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
            else if ( const render::DrawIndexed* pDraw = std::get_if<render::DrawIndexed>(&command) )
            {
                if ( !aPasses.empty() )
                {
                    aPasses.back().aDraws.push_back(*pDraw);
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

    // The depth-only shadow-map pass: no color target and a depth target.
    size_t FindShadowMapPass(const std::vector<RecordedPass>& aPasses)
    {
        for ( size_t i = 0; i < aPasses.size(); ++i )
        {
            if ( aPasses[i].begin.desc.colorTargets.empty()
                && aPasses[i].begin.desc.depthTarget.has_value()
                && !aPasses[i].begin.desc.depthTarget->target.isNull() )
            {
                return i;
            }
        }
        return aPasses.size();
    }

    // M11.2g's caster mask: one color target and the scene depth target, read-only.
    size_t FindShadowMaskPass(const std::vector<RecordedPass>& aPasses,
        render::TextureHandle sceneDepth)
    {
        for ( size_t i = 0; i < aPasses.size(); ++i )
        {
            if ( aPasses[i].begin.desc.colorTargets.size() == 1u
                && aPasses[i].begin.desc.depthTarget.has_value()
                && aPasses[i].begin.desc.depthTarget->target == sceneDepth )
            {
                return i;
            }
        }
        return aPasses.size();
    }

    // The receive pass: the pass that reads the shadow map at texture slot 3.
    size_t FindReceivePass(const std::vector<RecordedPass>& aPasses,
        render::TextureHandle mapTarget)
    {
        for ( size_t i = 0; i < aPasses.size(); ++i )
        {
            const render::BindTexture* pSlot3 = FindSlot(aPasses[i], 3u);
            if ( pSlot3 && pSlot3->texture == mapTarget )
            {
                return i;
            }
        }
        return aPasses.size();
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

TEST(std3DBabylonGpu, ShadowCasterDrawsAreMarkedAndOthersAreNot)
{
    UseNullDevice();
    OpenStd3D(kSceneSize, kSceneSize);

    const StdVertexTL aVerts[3] = {
        Vertex(2.0f, 2.0f, 0.5f, 1.0f, 0xFF102030u),
        Vertex(8.0f, 2.0f, 0.5f, 1.0f, 0xFF405060u),
        Vertex(2.0f, 8.0f, 0.5f, 1.0f, 0xFF708090u),
    };
    const StdVertexLit aLit[3] = {
        LitVertex(aVerts[0], 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        LitVertex(aVerts[1], 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        LitVertex(aVerts[2], 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f),
    };
    uint16_t aIndices[3] = { 0u, 1u, 2u };

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    // A plain draw, then a TL and a lit caster: only the two marked triangles are casters.
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0,
        const_cast<StdVertexTL*>(aVerts), 3u, aIndices, 3u);
    std3D_DrawRenderList(nullptr, STD3D_RS_SHADOW_CASTER,
        const_cast<StdVertexTL*>(aVerts), 3u, aIndices, 3u);
    std3D_DrawRenderListLit(nullptr, STD3D_RS_SHADOW_CASTER,
        const_cast<StdVertexLit*>(aLit), 3u, aIndices, 3u);

    // Lines and points never become casters, even with the bit set.
    std3D_SetRenderState(STD3D_RS_SHADOW_CASTER);
    std3D_DrawLineStrip(const_cast<StdVertexTL*>(aVerts), 3u);
    std3D_DrawPointList(const_cast<StdVertexTL*>(aVerts), 3u);

    // Nor does a HUD-layer draw.
    std3D_BeginHudLayer();
    std3D_DrawRenderList(nullptr, STD3D_RS_SHADOW_CASTER,
        const_cast<StdVertexTL*>(aVerts), 3u, aIndices, 3u);
    std3D_EndHudLayer();
    EndFrame();

    const std::vector<std3DBabylonInternal::Std3DBabylonDraw>& aCasterDraws =
        std3DBabylonInternal::std3DBabylon_GetShadowCasterDraws();
    TEST_ASSERT_EQUAL_size_t(2u, aCasterDraws.size());
    TEST_ASSERT_TRUE(aCasterDraws[0].bShadowCaster);
    TEST_ASSERT_FALSE(aCasterDraws[0].bLit);
    TEST_ASSERT_TRUE(aCasterDraws[1].bShadowCaster);
    TEST_ASSERT_TRUE(aCasterDraws[1].bLit);
}

TEST(std3DBabylonGpu, ShadowMaskOnlyRunsWithTheRecordedMapAndACasterDraw)
{
    UseNullDevice();
    OpenStd3D(kSceneSize, kSceneSize);
    UseShadowOnly();
    SetShadowCamera();

    const auto runScene = [](bool bAddCasterPolygon, bool bDrawFloor, bool bDrawCaster) {
        Null().clearRecorded();
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        if ( bDrawFloor )
        {
            DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f,
                SceneDepthForEyeDepth(2.0f, 0.1f, 100.0f), 0xFF909090u);
        }
        if ( bAddCasterPolygon )
        {
            DrawShadowCasterQuad();
        }
        if ( bDrawCaster )
        {
            DrawQuad(nullptr, STD3D_RS_SHADOW_CASTER, 28.0f, 36.0f, 32.0f, 40.0f,
                SceneDepthForEyeDepth(1.0f, 0.1f, 100.0f), 0xFFCC3030u);
        }
        EndFrame();
        return RecordedPasses();
    };

    // Shadows off: no map, no mask and no receive.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(false));
    std::vector<RecordedPass> aPasses = runScene(true, true, true);
    TEST_ASSERT_EQUAL_size_t(aPasses.size(), FindShadowMapPass(aPasses));
    TEST_ASSERT_EQUAL_size_t(aPasses.size(),
        FindShadowMaskPass(aPasses, stdRenderBabylon_GetDepthTarget()));
    TEST_ASSERT_EQUAL_size_t(aPasses.size(), FindReceivePass(aPasses, render::TextureHandle{}));

    // Shadows on but the map was not recorded: a caster draw alone changes nothing.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    aPasses = runScene(false, true, true);
    TEST_ASSERT_EQUAL_size_t(aPasses.size(), FindShadowMapPass(aPasses));
    TEST_ASSERT_EQUAL_size_t(aPasses.size(),
        FindShadowMaskPass(aPasses, stdRenderBabylon_GetDepthTarget()));

    // The map is recorded but no caster draw was collected: the receive pass keeps a null mask.
    aPasses = runScene(true, true, false);
    const size_t mapIndex = FindShadowMapPass(aPasses);
    TEST_ASSERT_TRUE(mapIndex < aPasses.size());
    const render::TextureHandle mapTarget = aPasses[mapIndex].begin.desc.depthTarget->target;
    const render::TextureHandle sceneDepth = stdRenderBabylon_GetDepthTarget();
    TEST_ASSERT_EQUAL_size_t(aPasses.size(), FindShadowMaskPass(aPasses, sceneDepth));
    const size_t receiveIndex = FindReceivePass(aPasses, mapTarget);
    TEST_ASSERT_TRUE(receiveIndex < aPasses.size());
    const render::BindTexture* pMaskBind = FindSlot(aPasses[receiveIndex], 4u);
    TEST_ASSERT_NOT_NULL(pMaskBind);
    TEST_ASSERT_TRUE(pMaskBind->texture == sceneDepth);

    // One caster draw: exactly one mask pass with one draw, and the receive pass samples it.
    aPasses = runScene(true, true, true);
    const size_t maskIndex = FindShadowMaskPass(aPasses, sceneDepth);
    TEST_ASSERT_TRUE(maskIndex < aPasses.size());
    TEST_ASSERT_TRUE(aPasses[maskIndex].begin.desc.depthTarget->load == render::LoadOp::Load);
    TEST_ASSERT_TRUE(aPasses[maskIndex].begin.desc.depthTarget->store == render::StoreOp::Store);
    TEST_ASSERT_TRUE(aPasses[maskIndex].begin.desc.colorTargets[0].load == render::LoadOp::Clear);
    TEST_ASSERT_EQUAL_size_t(1u, aPasses[maskIndex].aDraws.size());

    // The scene pass has the floor's draw, then the caster's.
    size_t scenePassIndex = aPasses.size();
    for ( size_t i = 0; i < aPasses.size(); ++i )
    {
        if ( aPasses[i].begin.desc.colorTargets.size() == 2u
            && aPasses[i].begin.desc.depthTarget.has_value()
            && aPasses[i].begin.desc.depthTarget->target == sceneDepth )
        {
            scenePassIndex = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(scenePassIndex < aPasses.size());
    TEST_ASSERT_EQUAL_size_t(2u, aPasses[scenePassIndex].aDraws.size());
    TEST_ASSERT_TRUE(aPasses[maskIndex].aDraws[0] == aPasses[scenePassIndex].aDraws[1]);

    const size_t maskedReceiveIndex = FindReceivePass(aPasses, mapTarget);
    TEST_ASSERT_TRUE(maskedReceiveIndex < aPasses.size());
    const render::BindTexture* pMaskedBind = FindSlot(aPasses[maskedReceiveIndex], 4u);
    TEST_ASSERT_NOT_NULL(pMaskedBind);
    TEST_ASSERT_FALSE(pMaskedBind->texture == sceneDepth);
    TEST_ASSERT_TRUE(pMaskedBind->texture == PassTarget(aPasses[maskIndex]));
}

TEST(std3DBabylonGpu, ShadowMaskReplaysALitCasterDrawWithItsOwnPipeline)
{
    UseNullDevice();
    OpenStd3D(kSceneSize, kSceneSize);
    UseShadowOnly();
    SetShadowCamera();

    const StdVertexTL aVerts[3] = {
        Vertex(28.0f, 36.0f, SceneDepthForEyeDepth(1.0f, 0.1f, 100.0f), 1.0f, 0xFFCC3030u),
        Vertex(32.0f, 36.0f, SceneDepthForEyeDepth(1.0f, 0.1f, 100.0f), 1.0f, 0xFFCC3030u),
        Vertex(28.0f, 40.0f, SceneDepthForEyeDepth(1.0f, 0.1f, 100.0f), 1.0f, 0xFFCC3030u),
    };
    const StdVertexLit aLit[3] = {
        LitVertex(aVerts[0], 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        LitVertex(aVerts[1], 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        LitVertex(aVerts[2], 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f),
    };
    uint16_t aIndices[3] = { 0u, 1u, 2u };

    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    DrawShadowCasterQuad();
    std3D_DrawRenderListLit(nullptr, STD3D_RS_SHADOW_CASTER,
        const_cast<StdVertexLit*>(aLit), 3u, aIndices, 3u);
    EndFrame();

    const std::vector<RecordedPass> aPasses = RecordedPasses();
    const size_t maskIndex = FindShadowMaskPass(aPasses, stdRenderBabylon_GetDepthTarget());
    TEST_ASSERT_TRUE(maskIndex < aPasses.size());
    TEST_ASSERT_EQUAL_size_t(1u, aPasses[maskIndex].aDraws.size());
    TEST_ASSERT_EQUAL_UINT32(3u, aPasses[maskIndex].aDraws[0].indexCount);
    TEST_ASSERT_EQUAL_UINT32(0u, aPasses[maskIndex].aDraws[0].firstIndex);
    TEST_ASSERT_EQUAL_INT32(0, aPasses[maskIndex].aDraws[0].vertexOffset);

    // The lit mask pipeline: the lit vertex shader and StdVertexLit's layout, the same fixed state
    // as the TL one. Its fragment shader is the mask shader (a value, no texture).
    const render::PipelineCreated* pMaskPipeline = nullptr;
    for ( const render::RecordedCommand& command : Null().recorded() )
    {
        const render::PipelineCreated* pCreated = std::get_if<render::PipelineCreated>(&command);
        if ( pCreated && pCreated->desc.vertexShader == stdBabylon::shaders::std3dLitVert()
            && pCreated->desc.fragmentShader == stdBabylon::shaders::std3dShadowMaskFrag() )
        {
            pMaskPipeline = pCreated;
            break;
        }
    }
    TEST_ASSERT_NOT_NULL(pMaskPipeline);
    TEST_ASSERT_EQUAL_size_t(1u, pMaskPipeline->desc.vertexLayout.bufferStrides.size());
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(StdVertexLit), pMaskPipeline->desc.vertexLayout.bufferStrides[0]);
    TEST_ASSERT_EQUAL_size_t(6u, pMaskPipeline->desc.vertexLayout.attributes.size());
    TEST_ASSERT_EQUAL_UINT32(32u, pMaskPipeline->desc.vertexLayout.attributes[4].offset);
    TEST_ASSERT_EQUAL_UINT32(44u, pMaskPipeline->desc.vertexLayout.attributes[5].offset);
    TEST_ASSERT_TRUE(pMaskPipeline->desc.depthFormat.has_value());
    TEST_ASSERT_TRUE(*pMaskPipeline->desc.depthFormat == stdRenderBabylon_GetDepthFormat());
    TEST_ASSERT_TRUE(pMaskPipeline->desc.depth.testEnabled);
    TEST_ASSERT_FALSE(pMaskPipeline->desc.depth.writeEnabled);
    TEST_ASSERT_TRUE(pMaskPipeline->desc.depth.compare == render::CompareOp::LessOrEqual);
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

    // M11.2g. The visible caster casts onto the floor but does not receive its own shadow: its
    // interior pixels are the shadows-off image.
    const size_t casterPixel = ((size_t)43u * kSceneSize + 31u) * 4u;
    for ( size_t channel = 0; channel < 4u; ++channel )
    {
        TEST_ASSERT_EQUAL_INT((int)off[casterPixel + channel], (int)on[casterPixel + channel]);
    }

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

TEST(std3DBabylonGpu, LevelGeometryShadowsTheCharacterButNotTheWorld)
{
    if ( !OpenGpuStd3D(kSceneSize, kSceneSize) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseShadowOnly();
    SetShadowCamera();

    const auto renderWithLedge = [](bool bLedge) {
        RenderScene([bLedge] {
            DrawShadowScene(false);
            if ( bLedge )
            {
                SetLevelShadowLedge();
            }
            else
            {
                std3D_SetStaticShadowCasters(nullptr, 0u);
            }
        });
    };

    // Off, with the ledge present: the baseline image.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(false));
    renderWithLedge(true);
    const std::vector<std::byte> off = CapturePresented(kSceneSize, kSceneSize);

    // On without the ledge only the character map runs; on with it the level map also applies.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    renderWithLedge(false);
    const std::vector<std::byte> onNoLedge = CapturePresented(kSceneSize, kSceneSize);
    renderWithLedge(true);
    const std::vector<std::byte> onLedge = CapturePresented(kSceneSize, kSceneSize);

    // The character's own pixels darken from the ledge; without it they keep the baseline colour.
    const size_t casterPixel = ((size_t)43u * kSceneSize + 31u) * 4u;
    for ( size_t channel = 0; channel < 4u; ++channel )
    {
        TEST_ASSERT_EQUAL_INT((int)off[casterPixel + channel],
            (int)onNoLedge[casterPixel + channel]);
    }
    const float noLedgeCaster = SsaoMeanBrightness(onNoLedge, kSceneSize, 30u, 42u, 33u, 45u);
    const float ledgeCaster    = SsaoMeanBrightness(onLedge, kSceneSize, 30u, 42u, 33u, 45u);
    std::printf("M11.3 level shadows: character %.2f without the ledge, %.2f with it\n",
        (double)noLedgeCaster, (double)ledgeCaster);
    TEST_ASSERT_TRUE(ledgeCaster < noLedgeCaster - 1.0f);

    // The level shadow stays off the world: the floor beside the character is exactly as without the
    // ledge.
    const float noLedgeFloor = SsaoMeanBrightness(onNoLedge, kSceneSize, 50u, 52u, 60u, 62u);
    const float ledgeFloor    = SsaoMeanBrightness(onLedge, kSceneSize, 50u, 52u, 60u, 62u);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, noLedgeFloor, ledgeFloor);

    // The character still casts onto the floor with the level map on. This rectangle also contains
    // the visible caster, whose pixels the level pass legitimately darkens, so only the darkening
    // against off is asserted here; the far-floor check above proves the world itself is untouched.
    const float offShadow     = SsaoMeanBrightness(off, kSceneSize, 28u, 42u, 36u, 49u);
    const float noLedgeShadow = SsaoMeanBrightness(onNoLedge, kSceneSize, 28u, 42u, 36u, 49u);
    const float ledgeShadow   = SsaoMeanBrightness(onLedge, kSceneSize, 28u, 42u, 36u, 49u);
    std::printf("M11.3 level shadows: floor below the caster %.2f off, %.2f on, %.2f with the ledge\n",
        (double)offShadow, (double)noLedgeShadow, (double)ledgeShadow);
    TEST_ASSERT_TRUE(noLedgeShadow < offShadow - 1.0f);
    TEST_ASSERT_TRUE(ledgeShadow < offShadow - 1.0f);
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
