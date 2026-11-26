// The std3DBabylon character-shadow caster tests (M11.2d): the classic StdVertexTL projection
// inverted against the lit view positions, the per-frame caster list, and the blob-shadow query.
// They use the Null render device, so they never need a GPU.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

#include <std/General/stdConfig.h>
#include <std/General/stdJSON.h>

#include "../std3DBabylonInternal.h"

#include <cmath>
#include <cstdio>

using namespace std3DBabylonTest;
using namespace std3DBabylonInternal;

TEST_GROUP(std3DBabylonGpu);

void TEST_std3DBabylonGpu_SETUP(void);
void TEST_std3DBabylonGpu_TEAR_DOWN(void);

namespace
{
    // rdCamera's fov is the horizontal one at the 4:3 reference aspect. The adapter's SSAO
    // projection gives the tangents the receive stage reconstructs receivers with; at the square
    // test canvas they are the rdClip tangents too.
    void SetShadowCasterProjection()
    {
        TEST_ASSERT_EQUAL_INT(0, std3D_SetProjection(90.0f, 0.1f, 100.0f));
    }

    void GetShadowCasterTangents(float* pTanHalfX, float* pTanHalfY)
    {
        TEST_ASSERT_TRUE(stdRenderBabylon_GetSsaoProjectionForTest(pTanHalfX, pTanHalfY));
    }

    // The StdVertexTL rdClip_FaceToPlaneEx makes for a view-space point (x right, y forward, z up).
    // It uses the same tangents the shadow stage inverts with, so the round trip proves the
    // inversion. rhw is (1 / forward) / 32, the original projection's fixed-point scale.
    StdVertexTL TLFromView(float vx, float vy, float vz, uint32_t color)
    {
        const uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
        const uint32_t height = stdRenderBabylon_GetBackBufferHeight();
        float tanHalfX = 0.0f;
        float tanHalfY = 0.0f;
        GetShadowCasterTangents(&tanHalfX, &tanHalfY);

        const float xNdc = vx / (tanHalfX * vy);
        const float yNdc = vz / (tanHalfY * vy);
        const float sx   = (xNdc * 0.5f + 0.5f) * (float)width;
        const float sy   = (0.5f - yNdc * 0.5f) * (float)height;
        return Vertex(sx, sy, SceneDepthForEyeDepth(vy, 0.1f, 100.0f),
            1.0f / (32.0f * vy), color);
    }

    float RelativeError(float expected, float actual)
    {
        const float scale = std::fabs(expected) > 1.0e-6f ? std::fabs(expected) : 1.0e-6f;
        return std::fabs(actual - expected) / scale;
    }

    const std::vector<float>& CollectedPositions()
    {
        return std3DBabylon_GetShadowCasterPositions();
    }

    constexpr const char* kShadowsConfigPath = "stdRenderBabylonShadowTest.json";
    constexpr const char* kShadowsKey        = "engine.renderer.shadows";

    bool ShadowsFallbackHasStarted(void)
    {
        return true;
    }

    int ShadowsFallbackGetInt(const char*, int defaultValue)
    {
        return defaultValue;
    }

    float ShadowsFallbackGetFloat(const char*, float defaultValue)
    {
        return defaultValue;
    }

    int ShadowsFallbackGetStr(const char*, char* pDstStr, size_t size,
        const char* pDefaultValue)
    {
        if ( pDstStr && size > 0 )
        {
            pDstStr[0] = '\0';
            if ( pDefaultValue )
            {
                std::snprintf(pDstStr, size, "%s", pDefaultValue);
            }
        }
        return 0;
    }
}

TEST(std3DBabylonGpu, ShadowCasterTLInversionMatchesTheLitViewPosition)
{
    UseNullDevice();
    OpenStd3D(64u, 64u);
    SetShadowCasterProjection();

    float tanHalfX = 0.0f;
    float tanHalfY = 0.0f;
    GetShadowCasterTangents(&tanHalfX, &tanHalfY);

    // A StdVertexLit carries both its TL form and its view position. Inverting the TL must give
    // (vx, vz, vy), the postfx view convention.
    const float aView[3][3] = {
        {  0.20f, 2.00f,  0.10f },
        { -0.30f, 3.00f, -0.20f },
        {  0.05f, 2.50f,  0.25f },
    };
    StdVertexLit aLit[3];
    for ( size_t i = 0; i < 3u; ++i )
    {
        aLit[i] = LitVertex(TLFromView(aView[i][0], aView[i][1], aView[i][2], 0xFFFFFFFFu),
            aView[i][0], aView[i][1], aView[i][2], 0.0f, 0.0f, 0.0f);
    }

    for ( size_t i = 0; i < 3u; ++i )
    {
        float aPosition[3];
        TEST_ASSERT_TRUE(std3DBabylon_TLToView(aLit[i].tl, 64u, 64u, tanHalfX, tanHalfY,
            aPosition));
        TEST_ASSERT_TRUE(RelativeError(aView[i][0], aPosition[0]) < 1.0e-3f);
        TEST_ASSERT_TRUE(RelativeError(aView[i][2], aPosition[1]) < 1.0e-3f);
        TEST_ASSERT_TRUE(RelativeError(aView[i][1], aPosition[2]) < 1.0e-3f);
    }

    // The lit draw path adds its view positions directly.
    uint16_t aIndices[3] = { 0u, 1u, 2u };
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderListLit(nullptr, STD3D_RS_SHADOW_CASTER, aLit, 3u, aIndices, 3u);
    std3D_EndScene();

    const std::vector<float>& aPositions = CollectedPositions();
    TEST_ASSERT_EQUAL_size_t(9u, aPositions.size());
    for ( size_t i = 0; i < 3u; ++i )
    {
        TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, aView[i][0], aPositions[i * 3u + 0u]);
        TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, aView[i][2], aPositions[i * 3u + 1u]);
        TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, aView[i][1], aPositions[i * 3u + 2u]);
    }
}

TEST(std3DBabylonGpu, ShadowCasterClassicTLInversionMatchesTheViewPosition)
{
    UseNullDevice();
    OpenStd3D(64u, 64u);
    SetShadowCasterProjection();

    const float aView[3][3] = {
        {  0.20f, 2.00f,  0.10f },
        { -0.30f, 3.00f, -0.20f },
        {  0.05f, 2.50f,  0.25f },
    };
    StdVertexTL aVerts[3];
    for ( size_t i = 0; i < 3u; ++i )
    {
        aVerts[i] = TLFromView(aView[i][0], aView[i][1], aView[i][2], 0xFFFFFFFFu);
    }

    uint16_t aIndices[3] = { 0u, 1u, 2u };
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, STD3D_RS_SHADOW_CASTER, aVerts, 3u, aIndices, 3u);
    std3D_EndScene();

    const std::vector<float>& aPositions = CollectedPositions();
    TEST_ASSERT_EQUAL_size_t(9u, aPositions.size());
    for ( size_t i = 0; i < 3u; ++i )
    {
        TEST_ASSERT_TRUE(RelativeError(aView[i][0], aPositions[i * 3u + 0u]) < 1.0e-3f);
        TEST_ASSERT_TRUE(RelativeError(aView[i][2], aPositions[i * 3u + 1u]) < 1.0e-3f);
        TEST_ASSERT_TRUE(RelativeError(aView[i][1], aPositions[i * 3u + 2u]) < 1.0e-3f);
    }
}

TEST(std3DBabylonGpu, ShadowCasterListTakesOnlyMarkedSceneTrianglesAndResets)
{
    UseNullDevice();
    OpenStd3D(64u, 64u);
    SetShadowCasterProjection();

    StdVertexTL aVerts[4] = {
        Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
        Vertex(8.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
        Vertex(8.0f, 8.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
        Vertex(0.0f, 8.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
    };
    uint16_t aIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());

    // A marked triangle adds three positions.
    std3D_DrawRenderList(nullptr, STD3D_RS_SHADOW_CASTER, aVerts, 3u, aIndices, 3u);
    TEST_ASSERT_EQUAL_size_t(3u, CollectedPositions().size() / 3u);

    // An unmarked triangle adds nothing.
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aIndices, 3u);
    TEST_ASSERT_EQUAL_size_t(3u, CollectedPositions().size() / 3u);

    // A marked HUD draw adds nothing.
    std3D_BeginHudLayer();
    std3D_DrawRenderList(nullptr, STD3D_RS_SHADOW_CASTER, aVerts, 3u, aIndices, 3u);
    std3D_EndHudLayer();
    TEST_ASSERT_EQUAL_size_t(3u, CollectedPositions().size() / 3u);

    // A marked quad adds its two triangles, six positions.
    std3D_DrawRenderList(nullptr, STD3D_RS_SHADOW_CASTER, aVerts, 4u, aIndices, 6u);
    TEST_ASSERT_EQUAL_size_t(9u, CollectedPositions().size() / 3u);

    std3D_EndScene();
    TEST_ASSERT_EQUAL_size_t(9u, CollectedPositions().size() / 3u);

    // The next scene starts empty.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    TEST_ASSERT_TRUE(CollectedPositions().empty());
    std3D_EndScene();
}

TEST(std3DBabylonGpu, DrawsThingShadowsIsZeroBeforeOpenAndFollowsTheOption)
{
    // Not open yet.
    TEST_ASSERT_EQUAL_INT(0, std3D_DrawsThingShadows());

    UseNullDevice();
    TEST_ASSERT_TRUE(stdRenderBabylon_Open(std3DBabylonTest_pDevice.get()));

    // The option starts off without a started config; the option test covers the profile default.
    // Turning it on makes the backend answer 1.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    TEST_ASSERT_EQUAL_INT(1, std3D_DrawsThingShadows());

    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(false));
    TEST_ASSERT_EQUAL_INT(0, std3D_DrawsThingShadows());
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    TEST_ASSERT_EQUAL_INT(1, std3D_DrawsThingShadows());
    stdRenderBabylon_Close();

    TEST_ASSERT_EQUAL_INT(0, std3D_DrawsThingShadows());
    DropDevice();
}

TEST(std3DBabylonGpu, ShadowsOptionDefaultsAndSetting)
{
    // The setter refuses before the frame owner is open.
    TEST_ASSERT_FALSE(stdRenderBabylon_SetShadows(true));

    if ( stdConfig_HasStarted() )
    {
        stdConfig_Shutdown();
    }
    std::remove(kShadowsConfigPath);

    const StdConfigFallback fallback = { ShadowsFallbackHasStarted,
        ShadowsFallbackGetInt, ShadowsFallbackGetFloat, ShadowsFallbackGetStr };
    stdConfig_SetFallback(&fallback);
    TEST_ASSERT_TRUE(stdJSON_Startup());
    TEST_ASSERT_TRUE(stdConfig_Startup(kShadowsConfigPath));
    TEST_ASSERT_FALSE(stdConfig_Contains(kShadowsKey));

    UseNullDevice();
    TEST_ASSERT_TRUE(stdRenderBabylon_Open(std3DBabylonTest_pDevice.get()));

    // The Enhanced profile defaults the stage on, Retail and QOL-off builds off.
#ifdef J3D_QOL_IMPROVEMENTS
    TEST_ASSERT_TRUE(stdRenderBabylon_GetShadows());
#else
    TEST_ASSERT_FALSE(stdRenderBabylon_GetShadows());
#endif

    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(false));
    TEST_ASSERT_FALSE(stdRenderBabylon_GetShadows());
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    TEST_ASSERT_TRUE(stdRenderBabylon_GetShadows());
    stdRenderBabylon_Close();

    // A config value overrides the profile's default when the frame owner opens.
    TEST_ASSERT_TRUE(stdConfig_SetBool(kShadowsKey, false));
    TEST_ASSERT_TRUE(stdRenderBabylon_Open(std3DBabylonTest_pDevice.get()));
    TEST_ASSERT_FALSE(stdRenderBabylon_GetShadows());
    stdRenderBabylon_Close();

    TEST_ASSERT_TRUE(stdConfig_SetBool(kShadowsKey, true));
    TEST_ASSERT_TRUE(stdRenderBabylon_Open(std3DBabylonTest_pDevice.get()));
    TEST_ASSERT_TRUE(stdRenderBabylon_GetShadows());
    stdRenderBabylon_Close();
    DropDevice();

    stdConfig_Shutdown();
    stdJSON_Shutdown();
    std::remove(kShadowsConfigPath);
}
