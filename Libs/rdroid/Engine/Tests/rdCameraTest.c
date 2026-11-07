// Added: tests of rdCamera_BuildStdLights (option A L4, ADR-0023), the frame's light list for
// std3D_SetLights. A camera with a hand-made view matrix collects lights through rdCamera_AddLight,
// as sithRender does; the built list must be the camera's list in its order and up to its cap, with
// the positions in view space as worked out here by hand, the values rdCamera_AddLight derived, and
// the camera untouched.
#include <unity_fixture.h>

#include <j3dcore/Tests/j3dTest.h>
#include <rdroid/Math/Tests/rdMathTestHost.h>

#include <rdroid/Engine/rdCamera.h>
#include <rdroid/Engine/rdLight.h>
#include <rdroid/Math/rdVector.h>

#include <std/General/std3D.h>
#include <std/General/stdUtil.h>

#include <stddef.h>
#include <string.h>

static rdCamera rdCameraTest_camera;
static rdLight rdCameraTest_aLights[RDCAMERA_MAX_LIGHTS + 2];
static StdLight rdCameraTest_aOut[STD3D_MAXLIGHTS + 1];

TEST_GROUP(rdCamera);

TEST_SETUP(rdCamera)
{
    // World to view: view x = world y - 2, view y = 5 - world x, view z = world z - 1.
    memset(&rdCameraTest_camera, 0, sizeof(rdCameraTest_camera));
    rdCameraTest_camera.viewMatrix.rvec.x = 0.0f;
    rdCameraTest_camera.viewMatrix.rvec.y = -1.0f;
    rdCameraTest_camera.viewMatrix.rvec.z = 0.0f;
    rdCameraTest_camera.viewMatrix.lvec.x = 1.0f;
    rdCameraTest_camera.viewMatrix.lvec.y = 0.0f;
    rdCameraTest_camera.viewMatrix.lvec.z = 0.0f;
    rdCameraTest_camera.viewMatrix.uvec.x = 0.0f;
    rdCameraTest_camera.viewMatrix.uvec.y = 0.0f;
    rdCameraTest_camera.viewMatrix.uvec.z = 1.0f;
    rdCameraTest_camera.viewMatrix.dvec.x = -2.0f;
    rdCameraTest_camera.viewMatrix.dvec.y = 5.0f;
    rdCameraTest_camera.viewMatrix.dvec.z = -1.0f;
    rdCameraTest_camera.attenuationMin = 0.2f;
    rdCameraTest_camera.attenuationMax = 0.1f;

    for ( size_t i = 0; i < STD_ARRAYLEN(rdCameraTest_aLights); ++i )
    {
        rdLight_NewEntry(&rdCameraTest_aLights[i]);
    }

    // A pattern, so a test sees what was written and what was left.
    memset(rdCameraTest_aOut, 0xA5, sizeof(rdCameraTest_aOut));
}

TEST_TEAR_DOWN(rdCamera)
{
}

static void rdCameraTest_AssertLight(const StdLight* pOut, float vx, float vy, float vz, const rdLight* pLight, uint32_t flags)
{
    TEST_ASSERT_EQUAL_FLOAT(vx, pOut->vx);
    TEST_ASSERT_EQUAL_FLOAT(vy, pOut->vy);
    TEST_ASSERT_EQUAL_FLOAT(vz, pOut->vz);
    TEST_ASSERT_EQUAL_MEMORY(&pLight->minRadius, &pOut->minRadius, sizeof(float));
    TEST_ASSERT_EQUAL_MEMORY(&pLight->color.red, &pOut->red, sizeof(float));
    TEST_ASSERT_EQUAL_MEMORY(&pLight->color.green, &pOut->green, sizeof(float));
    TEST_ASSERT_EQUAL_MEMORY(&pLight->color.blue, &pOut->blue, sizeof(float));
    TEST_ASSERT_EQUAL_MEMORY(&pLight->color.alpha, &pOut->alpha, sizeof(float));
    TEST_ASSERT_EQUAL_MEMORY(&pLight->maxRadius, &pOut->maxRadius, sizeof(float));
    TEST_ASSERT_EQUAL_FLOAT(0.2f, pOut->attenuationMin);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, pOut->attenuationMax);
    TEST_ASSERT_EQUAL_HEX32(flags, pOut->flags);
}

// Three lights as sithRender adds them: one with its radii left to rdCamera_AddLight, a disabled one
// and one with its own radii. The list holds them in the order added, in view space.
TEST(rdCamera, TestBuildStdLightsIsTheCameraListInViewSpace)
{
    const rdVector3 aPositions[3] = { { 3.0f, 4.0f, 5.0f }, { -1.0f, 0.0f, 1.0f }, { 0.5f, -2.0f, 10.0f } };
    rdLight* pA = &rdCameraTest_aLights[0];
    rdLight* pB = &rdCameraTest_aLights[1];
    rdLight* pC = &rdCameraTest_aLights[2];

    rdVector_Set4(&pA->color, 0.6f, 0.3f, 0.9f, 2.0f);
    rdVector_Set4(&pB->color, 1.0f, 0.5f, 0.25f, 0.0f);
    pB->minRadius = 4.0f;
    pB->maxRadius = 2.0f;
    pB->bEnabled  = 0;
    rdVector_Set4(&pC->color, 0.125f, 0.25f, 0.5f, 0.75f);
    pC->minRadius = 8.0f;
    pC->maxRadius = 3.5f;

    TEST_ASSERT_EQUAL_INT(1, rdCamera_AddLight(&rdCameraTest_camera, pA, &aPositions[0]));
    TEST_ASSERT_EQUAL_INT(1, rdCamera_AddLight(&rdCameraTest_camera, pB, &aPositions[1]));
    TEST_ASSERT_EQUAL_INT(1, rdCamera_AddLight(&rdCameraTest_camera, pC, &aPositions[2]));

    // rdCamera_AddLight derived A's radii from its intensity: 0.6 / 0.2 and 0.6 / 0.1.
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 3.0f, pA->minRadius);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 6.0f, pA->maxRadius);

    TEST_ASSERT_EQUAL_size_t(3u, rdCamera_BuildStdLights(&rdCameraTest_camera, rdCameraTest_aOut, STD_ARRAYLEN(rdCameraTest_aOut)));
    rdCameraTest_AssertLight(&rdCameraTest_aOut[0], 2.0f, 2.0f, 4.0f, pA, STD_LIGHT_ENABLED);
    rdCameraTest_AssertLight(&rdCameraTest_aOut[1], -2.0f, 6.0f, 0.0f, pB, 0u);
    rdCameraTest_AssertLight(&rdCameraTest_aOut[2], -4.0f, 4.5f, 9.0f, pC, STD_LIGHT_ENABLED);

    // Nothing past the list is written.
    static const uint8_t aUntouched[sizeof(StdLight)] = {
        0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5,
        0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5,
        0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5, 0xA5,
    };
    TEST_ASSERT_EQUAL_MEMORY(aUntouched, &rdCameraTest_aOut[3], sizeof(StdLight));
}

// The camera keeps the first RDCAMERA_MAX_LIGHTS lights it is given (rdCamera_AddLight refuses the
// rest), and the list has exactly those, in order. A smaller array gets the first sizeLights.
TEST(rdCamera, TestBuildStdLightsKeepsTheCameraCapAndOrder)
{
    for ( size_t i = 0; i < STD_ARRAYLEN(rdCameraTest_aLights); ++i )
    {
        const rdVector3 pos = { 5.0f - (float)i, 2.0f, 1.0f }; // view (0, i, 0)
        rdCameraTest_aLights[i].minRadius = 1.0f + (float)i;
        rdCameraTest_aLights[i].maxRadius = 0.5f;
        TEST_ASSERT_EQUAL_INT(i < RDCAMERA_MAX_LIGHTS ? 1 : 0, rdCamera_AddLight(&rdCameraTest_camera, &rdCameraTest_aLights[i], &pos));
    }

    TEST_ASSERT_EQUAL_size_t(RDCAMERA_MAX_LIGHTS, rdCameraTest_camera.numLights);
    TEST_ASSERT_EQUAL_size_t(STD3D_MAXLIGHTS, rdCamera_BuildStdLights(&rdCameraTest_camera, rdCameraTest_aOut, STD3D_MAXLIGHTS));
    for ( size_t i = 0; i < STD3D_MAXLIGHTS; ++i )
    {
        rdCameraTest_AssertLight(&rdCameraTest_aOut[i], 0.0f, (float)i, 0.0f, &rdCameraTest_aLights[i], STD_LIGHT_ENABLED);
    }

    memset(rdCameraTest_aOut, 0xA5, sizeof(rdCameraTest_aOut));
    TEST_ASSERT_EQUAL_size_t(5u, rdCamera_BuildStdLights(&rdCameraTest_camera, rdCameraTest_aOut, 5u));
    rdCameraTest_AssertLight(&rdCameraTest_aOut[4], 0.0f, 4.0f, 0.0f, &rdCameraTest_aLights[4], STD_LIGHT_ENABLED);
    TEST_ASSERT_EQUAL_HEX8(0xA5, ((const uint8_t*)&rdCameraTest_aOut[5])[0]);

    rdCamera_ClearLights(&rdCameraTest_camera);
    TEST_ASSERT_EQUAL_size_t(0u, rdCamera_BuildStdLights(&rdCameraTest_camera, rdCameraTest_aOut, STD3D_MAXLIGHTS));
}

// Building the list reads the camera and its lights only.
TEST(rdCamera, TestBuildStdLightsChangesNothing)
{
    const rdVector3 pos = { 1.0f, 2.0f, 3.0f };
    rdVector_Set4(&rdCameraTest_aLights[0].color, 0.3f, 0.6f, 0.9f, 1.0f);
    TEST_ASSERT_EQUAL_INT(1, rdCamera_AddLight(&rdCameraTest_camera, &rdCameraTest_aLights[0], &pos));

    rdCamera cameraBefore = rdCameraTest_camera;
    rdLight lightBefore   = rdCameraTest_aLights[0];
    TEST_ASSERT_EQUAL_size_t(1u, rdCamera_BuildStdLights(&rdCameraTest_camera, rdCameraTest_aOut, STD3D_MAXLIGHTS));
    TEST_ASSERT_EQUAL_MEMORY(&cameraBefore, &rdCameraTest_camera, sizeof(cameraBefore));
    TEST_ASSERT_EQUAL_MEMORY(&lightBefore, &rdCameraTest_aLights[0], sizeof(lightBefore));
}

TEST_GROUP_RUNNER(rdCamera)
{
    RUN_TEST_CASE(rdCamera, TestBuildStdLightsIsTheCameraListInViewSpace);
    RUN_TEST_CASE(rdCamera, TestBuildStdLightsKeepsTheCameraCapAndOrder);
    RUN_TEST_CASE(rdCamera, TestBuildStdLightsChangesNothing);
}
