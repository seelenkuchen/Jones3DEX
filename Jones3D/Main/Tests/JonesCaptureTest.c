#include <unity_fixture.h>

#include <Jones3D/Main/JonesCapture.h>

TEST_GROUP(JonesCapture);

TEST_SETUP(JonesCapture)
{
}

TEST_TEAR_DOWN(JonesCapture)
{
}

TEST(JonesCapture, TestNoOption)
{
    char aPath[32] = "unchanged";
    char aRest[64] = "unchanged";

    TEST_ASSERT_FALSE(JonesCapture_ParseCommandLine("1 +foo", aPath, sizeof(aPath), aRest, sizeof(aRest)));
    TEST_ASSERT_EQUAL_STRING("", aPath);
    TEST_ASSERT_EQUAL_STRING("", aRest);
}

TEST(JonesCapture, TestOptionFirst)
{
    char aPath[32];
    char aRest[64];

    TEST_ASSERT_TRUE(JonesCapture_ParseCommandLine("-capture scene.rcap", aPath, sizeof(aPath), aRest, sizeof(aRest)));
    TEST_ASSERT_EQUAL_STRING("scene.rcap", aPath);
    TEST_ASSERT_EQUAL_STRING("", aRest);
}

TEST(JonesCapture, TestOptionMiddle)
{
    char aPath[32];
    char aRest[64];

    TEST_ASSERT_TRUE(JonesCapture_ParseCommandLine("1 -capture scene.rcap +foo", aPath, sizeof(aPath), aRest, sizeof(aRest)));
    TEST_ASSERT_EQUAL_STRING("scene.rcap", aPath);
    TEST_ASSERT_EQUAL_STRING("1 +foo", aRest);
}

TEST(JonesCapture, TestOptionLast)
{
    char aPath[32];
    char aRest[64];

    TEST_ASSERT_TRUE(JonesCapture_ParseCommandLine("1 -capture scene.rcap", aPath, sizeof(aPath), aRest, sizeof(aRest)));
    TEST_ASSERT_EQUAL_STRING("scene.rcap", aPath);
    TEST_ASSERT_EQUAL_STRING("1", aRest);
}

TEST(JonesCapture, TestQuotedPathWithSpaces)
{
    char aPath[64];
    char aRest[64];

    TEST_ASSERT_TRUE(JonesCapture_ParseCommandLine(
        "1 -capture \"C:\\My Captures\\a.rcap\" extra",
        aPath, sizeof(aPath), aRest, sizeof(aRest)
    ));
    TEST_ASSERT_EQUAL_STRING("C:\\My Captures\\a.rcap", aPath);
    TEST_ASSERT_EQUAL_STRING("1 extra", aRest);
}

TEST(JonesCapture, TestMissingValue)
{
    char aPath[32] = "unchanged";
    char aRest[64] = "unchanged";

    TEST_ASSERT_FALSE(JonesCapture_ParseCommandLine("1 -capture", aPath, sizeof(aPath), aRest, sizeof(aRest)));
    TEST_ASSERT_EQUAL_STRING("", aPath);
    TEST_ASSERT_EQUAL_STRING("", aRest);
}

TEST(JonesCapture, TestEmptyQuotedValue)
{
    char aPath[32] = "unchanged";
    char aRest[64] = "unchanged";

    TEST_ASSERT_FALSE(JonesCapture_ParseCommandLine("-capture \"\"", aPath, sizeof(aPath), aRest, sizeof(aRest)));
    TEST_ASSERT_EQUAL_STRING("", aPath);
    TEST_ASSERT_EQUAL_STRING("", aRest);
}

TEST(JonesCapture, TestPathTooLong)
{
    char aPath[8]  = "unset";
    char aRest[64] = "unchanged";

    TEST_ASSERT_FALSE(JonesCapture_ParseCommandLine("-capture 12345678", aPath, sizeof(aPath), aRest, sizeof(aRest)));
    TEST_ASSERT_EQUAL_STRING("", aPath);
    TEST_ASSERT_EQUAL_STRING("", aRest);
}

TEST(JonesCapture, TestPathExactFit)
{
    char aPath[8];
    char aRest[64];

    TEST_ASSERT_TRUE(JonesCapture_ParseCommandLine("-capture 1234567", aPath, sizeof(aPath), aRest, sizeof(aRest)));
    TEST_ASSERT_EQUAL_STRING("1234567", aPath);
    TEST_ASSERT_EQUAL_STRING("", aRest);
}

TEST(JonesCapture, TestRestTooLong)
{
    char aPath[32] = "unchanged";
    char aRest[8]  = "unset";

    TEST_ASSERT_FALSE(JonesCapture_ParseCommandLine(
        "one two -capture file three",
        aPath, sizeof(aPath), aRest, sizeof(aRest)
    ));
    TEST_ASSERT_EQUAL_STRING("", aPath);
    TEST_ASSERT_EQUAL_STRING("", aRest);
}

TEST(JonesCapture, TestOtherTokensPreserved)
{
    char aPath[32];
    char aRest[64];

    TEST_ASSERT_TRUE(JonesCapture_ParseCommandLine(
        "12 -capture file +set foo bar",
        aPath, sizeof(aPath), aRest, sizeof(aRest)
    ));
    TEST_ASSERT_EQUAL_STRING("file", aPath);
    TEST_ASSERT_EQUAL_STRING("12 +set foo bar", aRest);
}

TEST(JonesCapture, TestWhitespaceNormalized)
{
    char aPath[32];
    char aRest[64];

    TEST_ASSERT_TRUE(JonesCapture_ParseCommandLine(
        "  12\t-capture   file    34  ",
        aPath, sizeof(aPath), aRest, sizeof(aRest)
    ));
    TEST_ASSERT_EQUAL_STRING("file", aPath);
    TEST_ASSERT_EQUAL_STRING("12 34", aRest);
}

TEST(JonesCapture, TestOptionNameIsCaseSensitive)
{
    char aPath[32] = "unchanged";
    char aRest[64] = "unchanged";

    TEST_ASSERT_FALSE(JonesCapture_ParseCommandLine("-Capture file", aPath, sizeof(aPath), aRest, sizeof(aRest)));
    TEST_ASSERT_EQUAL_STRING("", aPath);
    TEST_ASSERT_EQUAL_STRING("", aRest);
}

TEST_GROUP_RUNNER(JonesCapture)
{
    RUN_TEST_CASE(JonesCapture, TestNoOption);
    RUN_TEST_CASE(JonesCapture, TestOptionFirst);
    RUN_TEST_CASE(JonesCapture, TestOptionMiddle);
    RUN_TEST_CASE(JonesCapture, TestOptionLast);
    RUN_TEST_CASE(JonesCapture, TestQuotedPathWithSpaces);
    RUN_TEST_CASE(JonesCapture, TestMissingValue);
    RUN_TEST_CASE(JonesCapture, TestEmptyQuotedValue);
    RUN_TEST_CASE(JonesCapture, TestPathTooLong);
    RUN_TEST_CASE(JonesCapture, TestPathExactFit);
    RUN_TEST_CASE(JonesCapture, TestRestTooLong);
    RUN_TEST_CASE(JonesCapture, TestOtherTokensPreserved);
    RUN_TEST_CASE(JonesCapture, TestWhitespaceNormalized);
    RUN_TEST_CASE(JonesCapture, TestOptionNameIsCaseSensitive);
}
