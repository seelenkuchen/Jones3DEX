// The std3DBabylon character-shadow caster tests (M11.2e): polygons submitted through
// std3D_AddShadowCasterPolygon, the per-frame caster list, the cap, and the blob-shadow query.
// They use the Null render device, so they never need a GPU.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

#include <std/General/stdConfig.h>
#include <std/General/stdJSON.h>

#include "../std3DBabylonInternal.h"

#include <cstdio>
#include <limits>
#include <vector>

using namespace std3DBabylonTest;
using namespace std3DBabylonInternal;

TEST_GROUP(std3DBabylonGpu);

void TEST_std3DBabylonGpu_SETUP(void);
void TEST_std3DBabylonGpu_TEAR_DOWN(void);

namespace
{
    void OpenShadowCasterScene()
    {
        UseNullDevice();
        OpenStd3D(64u, 64u);
        TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    }

    const std::vector<float>& CollectedPositions()
    {
        return std3DBabylon_GetShadowCasterPositions();
    }

    // One caster vertex as it lands in the list: the camera-space point (x right, y forward, z up)
    // becomes the postfx view convention (x right, y up, z forward).
    void AssertCasterVertex(size_t index, float x, float yForward, float zUp)
    {
        const std::vector<float>& aPositions = CollectedPositions();
        TEST_ASSERT_TRUE(index * 3u + 2u < aPositions.size());
        TEST_ASSERT_EQUAL_FLOAT(x, aPositions[index * 3u + 0u]);
        TEST_ASSERT_EQUAL_FLOAT(zUp, aPositions[index * 3u + 1u]);
        TEST_ASSERT_EQUAL_FLOAT(yForward, aPositions[index * 3u + 2u]);
    }

    const float aShadowCasterTriangle[9] = {
        0.20f, 2.00f, 0.10f,
        -0.30f, 3.00f, -0.20f,
        0.05f, 2.50f, 0.25f,
    };

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

TEST(std3DBabylonGpu, AddShadowCasterPolygonTriangleUsesTheViewConvention)
{
    OpenShadowCasterScene();

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 3u);
    std3D_EndScene();

    TEST_ASSERT_EQUAL_size_t(9u, CollectedPositions().size());
    for ( size_t i = 0; i < 3u; ++i )
    {
        AssertCasterVertex(i, aShadowCasterTriangle[i * 3u + 0u],
            aShadowCasterTriangle[i * 3u + 1u], aShadowCasterTriangle[i * 3u + 2u]);
    }
}

TEST(std3DBabylonGpu, AddShadowCasterPolygonFansQuadsAndPentagons)
{
    OpenShadowCasterScene();

    const float aQuad[12] = {
        0.0f, 2.0f, 0.0f,
        1.0f, 2.0f, 0.0f,
        1.0f, 2.0f, 1.0f,
        0.0f, 2.0f, 1.0f,
    };
    const float aPentagon[15] = {
        0.0f, 3.0f, 0.0f,
        1.0f, 3.0f, 0.0f,
        1.5f, 3.0f, 1.0f,
        0.5f, 3.0f, 1.5f,
        -0.5f, 3.0f, 1.0f,
    };

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());

    std3D_AddShadowCasterPolygon(aQuad, 4u);
    TEST_ASSERT_EQUAL_size_t(18u, CollectedPositions().size());
    // Fan from vertex 0: 0, 1, 2 then 0, 2, 3.
    const size_t aQuadFan[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    for ( size_t i = 0; i < 6u; ++i )
    {
        AssertCasterVertex(i, aQuad[aQuadFan[i] * 3u + 0u], aQuad[aQuadFan[i] * 3u + 1u],
            aQuad[aQuadFan[i] * 3u + 2u]);
    }

    std3D_AddShadowCasterPolygon(aPentagon, 5u);
    TEST_ASSERT_EQUAL_size_t(45u, CollectedPositions().size());
    // Fan from vertex 0: 0, 1, 2 then 0, 2, 3 then 0, 3, 4.
    const size_t aPentagonFan[9] = { 0u, 1u, 2u, 0u, 2u, 3u, 0u, 3u, 4u };
    for ( size_t i = 0; i < 9u; ++i )
    {
        AssertCasterVertex(6u + i, aPentagon[aPentagonFan[i] * 3u + 0u],
            aPentagon[aPentagonFan[i] * 3u + 1u], aPentagon[aPentagonFan[i] * 3u + 2u]);
    }

    std3D_EndScene();
}

TEST(std3DBabylonGpu, AddShadowCasterPolygonIgnoresSmallAndNullPolygons)
{
    OpenShadowCasterScene();

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_AddShadowCasterPolygon(NULL, 3u);
    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 0u);
    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 2u);
    TEST_ASSERT_TRUE(CollectedPositions().empty());

    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 3u);
    TEST_ASSERT_EQUAL_size_t(9u, CollectedPositions().size());
    std3D_EndScene();
}

TEST(std3DBabylonGpu, AddShadowCasterPolygonDropsTheWholePolygonOnABadCoordinate)
{
    OpenShadowCasterScene();

    const float aNanTriangle[9] = {
        0.0f, 2.0f, 0.0f,
        std::numeric_limits<float>::quiet_NaN(), 2.0f, 0.0f,
        1.0f, 2.0f, 1.0f,
    };
    const float aInfiniteQuad[12] = {
        0.0f, 2.0f, 0.0f,
        1.0f, std::numeric_limits<float>::infinity(), 0.0f,
        1.0f, 2.0f, 1.0f,
        0.0f, 2.0f, 1.0f,
    };

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());

    // One bad coordinate drops the whole polygon; the next polygon must start at vertex 0.
    std3D_AddShadowCasterPolygon(aNanTriangle, 3u);
    TEST_ASSERT_TRUE(CollectedPositions().empty());
    std3D_AddShadowCasterPolygon(aInfiniteQuad, 4u);
    TEST_ASSERT_TRUE(CollectedPositions().empty());

    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 3u);
    TEST_ASSERT_EQUAL_size_t(9u, CollectedPositions().size());
    for ( size_t i = 0; i < 3u; ++i )
    {
        AssertCasterVertex(i, aShadowCasterTriangle[i * 3u + 0u],
            aShadowCasterTriangle[i * 3u + 1u], aShadowCasterTriangle[i * 3u + 2u]);
    }

    std3D_EndScene();
}

TEST(std3DBabylonGpu, AddShadowCasterPolygonOnlyInsideASceneWithShadowsOn)
{
    OpenShadowCasterScene();

    // Outside a scene it is ignored.
    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 3u);
    TEST_ASSERT_TRUE(CollectedPositions().empty());

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());

    // Inside a scene but with the stage off it is ignored.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(false));
    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 3u);
    TEST_ASSERT_TRUE(CollectedPositions().empty());

    // With the stage on it lands.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetShadows(true));
    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 3u);
    TEST_ASSERT_EQUAL_size_t(9u, CollectedPositions().size());

    std3D_EndScene();
}

TEST(std3DBabylonGpu, AddShadowCasterPolygonListResetsAtTheNextStartScene)
{
    OpenShadowCasterScene();

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 3u);
    std3D_EndScene();
    TEST_ASSERT_EQUAL_size_t(9u, CollectedPositions().size());

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    TEST_ASSERT_TRUE(CollectedPositions().empty());
    std3D_EndScene();
}

TEST(std3DBabylonGpu, ShadowCasterDrawStateAddsNoPositionsNow)
{
    OpenShadowCasterScene();

    const StdVertexTL aVerts[3] = {
        Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
        Vertex(8.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
        Vertex(8.0f, 8.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
    };
    StdVertexLit aLit[3];
    for ( size_t i = 0; i < 3u; ++i )
    {
        aLit[i] = LitVertex(aVerts[i], 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    }
    uint16_t aIndices[3] = { 0u, 1u, 2u };

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());

    // The old collector read the marked draws; casters now come only through the polygon call.
    std3D_DrawRenderList(nullptr, STD3D_RS_SHADOW_CASTER,
        const_cast<StdVertexTL*>(aVerts), 3u, aIndices, 3u);
    TEST_ASSERT_TRUE(CollectedPositions().empty());

    std3D_DrawRenderListLit(nullptr, STD3D_RS_SHADOW_CASTER, aLit, 3u, aIndices, 3u);
    TEST_ASSERT_TRUE(CollectedPositions().empty());

    std3D_EndScene();
}

TEST(std3DBabylonGpu, AddShadowCasterPolygonCapKeepsPolygonsWhole)
{
    OpenShadowCasterScene();

    // The cap is 300000 vertices, 900000 floats. A 100001-vertex polygon fans into 99999 triangles,
    // 299997 vertices (899991 floats).
    std::vector<float> aHugePolygon(100001u * 3u);
    for ( size_t i = 0; i < aHugePolygon.size(); ++i )
    {
        aHugePolygon[i] = (float)(i % 7u) * 0.1f;
    }

    const float aQuad[12] = {
        0.0f, 2.0f, 0.0f,
        1.0f, 2.0f, 0.0f,
        1.0f, 2.0f, 1.0f,
        0.0f, 2.0f, 1.0f,
    };

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());

    std3D_AddShadowCasterPolygon(aHugePolygon.data(), 100001u);
    TEST_ASSERT_EQUAL_size_t(899991u, CollectedPositions().size());

    // A quad would push the list over the cap, so it is dropped whole rather than clipped.
    std3D_AddShadowCasterPolygon(aQuad, 4u);
    TEST_ASSERT_EQUAL_size_t(899991u, CollectedPositions().size());

    // A triangle fits exactly and is added whole.
    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 3u);
    TEST_ASSERT_EQUAL_size_t(900000u, CollectedPositions().size());

    // One more does not fit and is dropped whole.
    std3D_AddShadowCasterPolygon(aShadowCasterTriangle, 3u);
    TEST_ASSERT_EQUAL_size_t(900000u, CollectedPositions().size());

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
