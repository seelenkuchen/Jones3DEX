#include <unity_fixture.h>

#include <stdio.h>
#include <string.h>

#include <std/General/std.h>
#include <std/General/stdFileUtil.h>
#include <std/General/stdUserDir.h>

#include "stdGeneralTest.h"

#define STDUSERDIRTEST_DIR     "stdUserDirTest_dir"
#define STDUSERDIRTEST_LEGACY  "stdUserDirTest_legacy"
#define STDUSERDIRTEST_SOURCE  "stdUserDirTest_source.txt"
#define STDUSERDIRTEST_COPY    STDUSERDIRTEST_DIR "\\copy.txt"
#define STDUSERDIRTEST_SAVES   STDUSERDIRTEST_DIR "\\Saves"
#define STDUSERDIRTEST_SAVE_A  STDUSERDIRTEST_SAVES "\\a.nds"
#define STDUSERDIRTEST_SAVE_B  STDUSERDIRTEST_SAVES "\\b.nds"
#define STDUSERDIRTEST_SAVE_C  STDUSERDIRTEST_SAVES "\\c.txt"
#define STDUSERDIRTEST_LEG_A   STDUSERDIRTEST_LEGACY "\\a.nds"
#define STDUSERDIRTEST_LEG_B   STDUSERDIRTEST_LEGACY "\\b.nds"
#define STDUSERDIRTEST_LEG_C   STDUSERDIRTEST_LEGACY "\\c.txt"

static void stdUserDirTest_WriteFile(const char* pPath, const char* pText)
{
    tFileHandle fh = stdFileOpen(pPath, "wb");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    TEST_ASSERT_EQUAL_size_t(strlen(pText), stdFileWrite(fh, pText, strlen(pText)));
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
}

static void stdUserDirTest_ReadFile(const char* pPath, char* pOut, size_t outSize)
{
    TEST_ASSERT_TRUE(outSize > 0);
    pOut[0] = '\0';

    tFileHandle fh = stdFileOpen(pPath, "rb");
    TEST_ASSERT_NOT_EQUAL(0u, fh);
    size_t numRead = stdFileRead(fh, pOut, outSize - 1);
    pOut[numRead] = '\0';
    TEST_ASSERT_EQUAL_INT(0, stdFileClose(fh));
}

static void stdUserDirTest_Cleanup(void)
{
    StdGeneralTest_DeleteFile(STDUSERDIRTEST_SAVE_A);
    StdGeneralTest_DeleteFile(STDUSERDIRTEST_SAVE_B);
    StdGeneralTest_DeleteFile(STDUSERDIRTEST_SAVE_C);
    StdGeneralTest_DeleteFile(STDUSERDIRTEST_COPY);
    StdGeneralTest_DeleteFile(STDUSERDIRTEST_SOURCE);
    StdGeneralTest_DeleteFile(STDUSERDIRTEST_LEG_A);
    StdGeneralTest_DeleteFile(STDUSERDIRTEST_LEG_B);
    StdGeneralTest_DeleteFile(STDUSERDIRTEST_LEG_C);
    stdFileUtil_RmDir(STDUSERDIRTEST_SAVES);
    stdFileUtil_RmDir(STDUSERDIRTEST_DIR "\\nested");
    stdFileUtil_RmDir(STDUSERDIRTEST_DIR);
    stdFileUtil_RmDir(STDUSERDIRTEST_LEGACY);
}

TEST_GROUP(stdUserDir);

TEST_SETUP(stdUserDir)
{
    StdGeneralTest_Startup();
    stdUserDir_Reset();
    stdUserDirTest_Cleanup();
}

TEST_TEAR_DOWN(stdUserDir)
{
    stdUserDir_Reset();
    stdUserDirTest_Cleanup();
    StdGeneralTest_Shutdown();
}

TEST(stdUserDir, TestSetGetAndMakePath)
{
    char aPath[64];

    TEST_ASSERT_EQUAL_STRING("", stdUserDir_GetPath());

    TEST_ASSERT_TRUE(stdUserDir_MakePath(aPath, sizeof(aPath), "Jones.cfg"));
    TEST_ASSERT_EQUAL_STRING("Jones.cfg", aPath);

    TEST_ASSERT_TRUE(stdUserDir_Set(STDUSERDIRTEST_DIR "\\nested"));
    TEST_ASSERT_EQUAL_STRING(STDUSERDIRTEST_DIR "\\nested", stdUserDir_GetPath());
    TEST_ASSERT_TRUE(stdFileUtil_FileExists(STDUSERDIRTEST_DIR));
    TEST_ASSERT_TRUE(stdFileUtil_FileExists(STDUSERDIRTEST_DIR "\\nested"));

    TEST_ASSERT_TRUE(stdUserDir_MakePath(aPath, sizeof(aPath), "Jones.cfg"));
    TEST_ASSERT_EQUAL_STRING(STDUSERDIRTEST_DIR "\\nested\\Jones.cfg", aPath);

    TEST_ASSERT_TRUE(stdUserDir_Set(""));
    TEST_ASSERT_EQUAL_STRING("", stdUserDir_GetPath());
}

TEST(stdUserDir, TestTooLongPathAndOutputRefused)
{
    char aLong[STDUSERDIR_MAXPATH + 1];
    char aPath[8] = "keep";

    memset(aLong, 'a', STDUSERDIR_MAXPATH);
    aLong[STDUSERDIR_MAXPATH] = '\0';

    TEST_ASSERT_TRUE(stdUserDir_Set(STDUSERDIRTEST_DIR));
    TEST_ASSERT_FALSE(stdUserDir_Set(aLong));
    TEST_ASSERT_EQUAL_STRING(STDUSERDIRTEST_DIR, stdUserDir_GetPath());

    TEST_ASSERT_FALSE(stdUserDir_MakePath(aPath, sizeof(aPath), "a-name-that-does-not-fit"));
    TEST_ASSERT_EQUAL_STRING("keep", aPath);
}

TEST(stdUserDir, TestCreatesMissingDirectory)
{
    TEST_ASSERT_FALSE(stdFileUtil_FileExists(STDUSERDIRTEST_DIR));
    TEST_ASSERT_TRUE(stdUserDir_Set(STDUSERDIRTEST_DIR));
    TEST_ASSERT_TRUE(stdFileUtil_FileExists(STDUSERDIRTEST_DIR));
}

TEST(stdUserDir, TestCopyFileOnlyWhenMissing)
{
    char aText[32];

    stdUserDirTest_WriteFile(STDUSERDIRTEST_SOURCE, "source");
    TEST_ASSERT_TRUE(stdUserDir_Set(STDUSERDIRTEST_DIR));

    TEST_ASSERT_TRUE(stdUserDir_CopyFileIfMissing(STDUSERDIRTEST_SOURCE, "copy.txt"));
    stdUserDirTest_ReadFile(STDUSERDIRTEST_COPY, aText, sizeof(aText));
    TEST_ASSERT_EQUAL_STRING("source", aText);
    TEST_ASSERT_TRUE(stdFileUtil_FileExists(STDUSERDIRTEST_SOURCE));

    stdUserDirTest_WriteFile(STDUSERDIRTEST_COPY, "changed");
    TEST_ASSERT_FALSE(stdUserDir_CopyFileIfMissing(STDUSERDIRTEST_SOURCE, "copy.txt"));
    stdUserDirTest_ReadFile(STDUSERDIRTEST_COPY, aText, sizeof(aText));
    TEST_ASSERT_EQUAL_STRING("changed", aText);
}

TEST(stdUserDir, TestMigrateDirectoryCopiesOnlyOnceAndKeepsExisting)
{
    char aText[32];

    TEST_ASSERT_TRUE(stdFileUtil_MkDir(STDUSERDIRTEST_LEGACY));
    stdUserDirTest_WriteFile(STDUSERDIRTEST_LEG_A, "A");
    stdUserDirTest_WriteFile(STDUSERDIRTEST_LEG_B, "B");
    stdUserDirTest_WriteFile(STDUSERDIRTEST_LEG_C, "C");

    TEST_ASSERT_TRUE(stdUserDir_Set(STDUSERDIRTEST_DIR));
    TEST_ASSERT_EQUAL_size_t(2u, stdUserDir_MigrateDirectory(STDUSERDIRTEST_LEGACY, "Saves", "nds"));

    stdUserDirTest_ReadFile(STDUSERDIRTEST_SAVE_A, aText, sizeof(aText));
    TEST_ASSERT_EQUAL_STRING("A", aText);
    stdUserDirTest_ReadFile(STDUSERDIRTEST_SAVE_B, aText, sizeof(aText));
    TEST_ASSERT_EQUAL_STRING("B", aText);
    TEST_ASSERT_FALSE(stdFileUtil_FileExists(STDUSERDIRTEST_SAVE_C));
    TEST_ASSERT_TRUE(stdFileUtil_FileExists(STDUSERDIRTEST_LEG_A));
    TEST_ASSERT_TRUE(stdFileUtil_FileExists(STDUSERDIRTEST_LEG_B));

    // The destination already has saves of its own: a second call copies nothing and keeps them.
    stdUserDirTest_WriteFile(STDUSERDIRTEST_SAVE_A, "user");
    TEST_ASSERT_EQUAL_size_t(0u, stdUserDir_MigrateDirectory(STDUSERDIRTEST_LEGACY, "Saves", "nds"));
    stdUserDirTest_ReadFile(STDUSERDIRTEST_SAVE_A, aText, sizeof(aText));
    TEST_ASSERT_EQUAL_STRING("user", aText);
}

TEST_GROUP_RUNNER(stdUserDir)
{
    RUN_TEST_CASE(stdUserDir, TestSetGetAndMakePath);
    RUN_TEST_CASE(stdUserDir, TestTooLongPathAndOutputRefused);
    RUN_TEST_CASE(stdUserDir, TestCreatesMissingDirectory);
    RUN_TEST_CASE(stdUserDir, TestCopyFileOnlyWhenMissing);
    RUN_TEST_CASE(stdUserDir, TestMigrateDirectoryCopiesOnlyOnceAndKeepsExisting);
}
