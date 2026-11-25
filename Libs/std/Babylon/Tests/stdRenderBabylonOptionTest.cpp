// The render device and replay frame command-line options.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

using namespace std3DBabylonTest;

TEST_GROUP(stdRenderBabylonOption);

TEST_SETUP(stdRenderBabylonOption)
{
}

TEST_TEAR_DOWN(stdRenderBabylonOption)
{
}

TEST(stdRenderBabylonOption, TakesThePairAndKeepsTheRest)
{
    char aProgram[] = "Jones3D-headless", aFrames[] = "--frames", aFive[] = "5", aRender[] = "--render",
        aGpu[] = "gpu", aLoad[] = "--load", aSave[] = "x.nds";
    char* argv[] = { aProgram, aFrames, aFive, aRender, aGpu, aLoad, aSave, nullptr };
    int argc = 7;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Null;

    TEST_ASSERT_TRUE(stdRenderBabylon_TakeRenderOption(&argc, argv, &type));
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Gpu);
    TEST_ASSERT_EQUAL_INT(5, argc);
    TEST_ASSERT_EQUAL_PTR(aProgram, argv[0]);
    TEST_ASSERT_EQUAL_PTR(aFrames, argv[1]);
    TEST_ASSERT_EQUAL_PTR(aFive, argv[2]);
    TEST_ASSERT_EQUAL_PTR(aLoad, argv[3]);
    TEST_ASSERT_EQUAL_PTR(aSave, argv[4]);
    TEST_ASSERT_NULL(argv[5]);
}

TEST(stdRenderBabylonOption, TheLastPairWins)
{
    char aProgram[] = "p", aRender[] = "--render", aGpu[] = "gpu", aRender2[] = "--render", aNull[] = "null";
    char* argv[] = { aProgram, aRender, aGpu, aRender2, aNull, nullptr };
    int argc = 5;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Gpu;

    TEST_ASSERT_TRUE(stdRenderBabylon_TakeRenderOption(&argc, argv, &type));
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Null);
    TEST_ASSERT_EQUAL_INT(1, argc);
    TEST_ASSERT_NULL(argv[1]);
}

TEST(stdRenderBabylonOption, NoPairChangesNothing)
{
    char aProgram[] = "p", aState[] = "--state", aFile[] = "s.txt";
    char* argv[] = { aProgram, aState, aFile, nullptr };
    int argc = 3;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Gpu;

    TEST_ASSERT_TRUE(stdRenderBabylon_TakeRenderOption(&argc, argv, &type));
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Gpu);
    TEST_ASSERT_EQUAL_INT(3, argc);
    TEST_ASSERT_EQUAL_PTR(aState, argv[1]);
    TEST_ASSERT_EQUAL_PTR(aFile, argv[2]);
}

TEST(stdRenderBabylonOption, AMissingOrUnknownValueIsRefused)
{
    char aProgram[] = "p", aRender[] = "--render", aVulkan[] = "vulkan", aRender2[] = "--render", aGpu[] = "gpu";
    char* argvUnknown[] = { aProgram, aRender2, aGpu, aRender, aVulkan, nullptr };
    int argc = 5;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Null;

    TEST_ASSERT_FALSE(stdRenderBabylon_TakeRenderOption(&argc, argvUnknown, &type));
    TEST_ASSERT_EQUAL_INT(5, argc);
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Null);
    TEST_ASSERT_EQUAL_PTR(aRender2, argvUnknown[1]);
    TEST_ASSERT_EQUAL_PTR(aVulkan, argvUnknown[4]);

    char* argvMissing[] = { aProgram, aRender, nullptr };
    argc = 2;
    TEST_ASSERT_FALSE(stdRenderBabylon_TakeRenderOption(&argc, argvMissing, &type));
    TEST_ASSERT_EQUAL_INT(2, argc);
    TEST_ASSERT_EQUAL_PTR(aRender, argvMissing[1]);
}

TEST_GROUP_RUNNER(stdRenderBabylonOption)
{
    RUN_TEST_CASE(stdRenderBabylonOption, TakesThePairAndKeepsTheRest);
    RUN_TEST_CASE(stdRenderBabylonOption, TheLastPairWins);
    RUN_TEST_CASE(stdRenderBabylonOption, NoPairChangesNothing);
    RUN_TEST_CASE(stdRenderBabylonOption, AMissingOrUnknownValueIsRefused);
}

TEST_GROUP(stdRenderReplayBabylonOption);

TEST_SETUP(stdRenderReplayBabylonOption)
{
}

TEST_TEAR_DOWN(stdRenderReplayBabylonOption)
{
}

TEST(stdRenderReplayBabylonOption, TakesTheOptionsAndKeepsTheRest)
{
    char aProgram[] = "Jones3D-replay", aRender[] = "--render", aGpu[] = "gpu", aFramesOut[] = "--frames-out",
        aDir[] = "frames", aEvery[] = "--every", aThree[] = "3", aCapture[] = "--capture", aOut[] = "out.rcap",
        aNormals[] = "--normals";
    char* argv[] = { aProgram, aRender, aGpu, aFramesOut, aDir, aNormals, aEvery, aThree, aCapture, aOut, nullptr };
    int argc = 10;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Null;
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;

    TEST_ASSERT_TRUE(stdRenderBabylon_TakeRenderOption(&argc, argv, &type));
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Gpu);
    TEST_ASSERT_TRUE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argv, true, &output, &pError));
    TEST_ASSERT_NULL(pError);
    TEST_ASSERT_EQUAL_STRING("frames", output.pDirectory);
    TEST_ASSERT_EQUAL_UINT32(3u, output.every);
    TEST_ASSERT_TRUE(output.bNormals);
    TEST_ASSERT_EQUAL_INT(3, argc);
    TEST_ASSERT_EQUAL_PTR(aProgram, argv[0]);
    TEST_ASSERT_EQUAL_PTR(aCapture, argv[1]);
    TEST_ASSERT_EQUAL_PTR(aOut, argv[2]);
    TEST_ASSERT_NULL(argv[3]);
}

TEST(stdRenderReplayBabylonOption, FramesOutNeedsTheGpu)
{
    char aProgram[] = "p", aFramesOut[] = "--frames-out", aDir[] = "frames";
    char* argv[] = { aProgram, aFramesOut, aDir, nullptr };
    int argc = 3;
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;

    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argv, false, &output, &pError));
    TEST_ASSERT_NOT_NULL(pError);
    TEST_ASSERT_EQUAL_INT(3, argc);
    TEST_ASSERT_NULL(output.pDirectory);
}

TEST(stdRenderReplayBabylonOption, EveryNeedsFramesOut)
{
    char aProgram[] = "p", aEvery[] = "--every", aTwo[] = "2";
    char* argv[] = { aProgram, aEvery, aTwo, nullptr };
    int argc = 3;
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;

    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argv, true, &output, &pError));
    TEST_ASSERT_NOT_NULL(pError);
    TEST_ASSERT_EQUAL_INT(3, argc);
}

TEST(stdRenderReplayBabylonOption, NormalsNeedFramesOut)
{
    char aProgram[] = "p", aNormals[] = "--normals";
    char* argv[] = { aProgram, aNormals, nullptr };
    int argc = 2;
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;

    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argv, true, &output, &pError));
    TEST_ASSERT_NOT_NULL(pError);
    TEST_ASSERT_EQUAL_INT(2, argc);
    TEST_ASSERT_FALSE(output.bNormals);
}

TEST(stdRenderReplayBabylonOption, AMissingOrBadValueIsRefused)
{
    char aProgram[] = "p", aFramesOut[] = "--frames-out", aEmpty[] = "", aDir[] = "frames", aEvery[] = "--every",
        aZero[] = "0", aJunk[] = "2x";
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;
    int argc;

    char* argvMissing[] = { aProgram, aFramesOut, nullptr };
    argc = 2;
    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argvMissing, true, &output, &pError));
    TEST_ASSERT_EQUAL_INT(2, argc);

    char* argvEmpty[] = { aProgram, aFramesOut, aEmpty, nullptr };
    argc = 3;
    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argvEmpty, true, &output, &pError));
    TEST_ASSERT_EQUAL_INT(3, argc);

    char* argvZero[] = { aProgram, aFramesOut, aDir, aEvery, aZero, nullptr };
    argc = 5;
    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argvZero, true, &output, &pError));
    TEST_ASSERT_EQUAL_INT(5, argc);

    char* argvJunk[] = { aProgram, aFramesOut, aDir, aEvery, aJunk, nullptr };
    argc = 5;
    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argvJunk, true, &output, &pError));
    TEST_ASSERT_EQUAL_INT(5, argc);
}

TEST_GROUP_RUNNER(stdRenderReplayBabylonOption)
{
    RUN_TEST_CASE(stdRenderReplayBabylonOption, TakesTheOptionsAndKeepsTheRest);
    RUN_TEST_CASE(stdRenderReplayBabylonOption, FramesOutNeedsTheGpu);
    RUN_TEST_CASE(stdRenderReplayBabylonOption, EveryNeedsFramesOut);
    RUN_TEST_CASE(stdRenderReplayBabylonOption, NormalsNeedFramesOut);
    RUN_TEST_CASE(stdRenderReplayBabylonOption, AMissingOrBadValueIsRefused);
}
