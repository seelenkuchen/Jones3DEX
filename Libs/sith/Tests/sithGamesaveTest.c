#include <unity_fixture.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sith/Dss/sithGamesave.h>
#include <sith/Dss/sithGamesaveSys.h>

#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>

#include "../World/Tests/sithThingTestSupport.h"

#define SITHGAMESAVE_TEST_FILE  "sithGamesaveTest.nds"
#define SITHGAMESAVE_TEST_SHORT "sithGamesaveTest_short.nds"

static tHostServices sithGamesaveTest_hostServices;
static uint8_t sithGamesaveTest_aThumbnail[SITHSAVEGAME_THUMBSIZE];

TEST_GROUP(sithGamesave);

static int J3DAPI sithGamesaveTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static void* J3DAPI sithGamesaveTest_Malloc(size_t size)
{
    return malloc(size ? size : 1u);
}

static void J3DAPI sithGamesaveTest_Free(void* pData)
{
    free(pData);
}

static void* J3DAPI sithGamesaveTest_Realloc(void* pData, size_t size)
{
    return realloc(pData, size);
}

static tFileHandle J3DAPI sithGamesaveTest_FileOpen(const char* pFilename, const char* pMode)
{
    FILE* pFile = fopen(pFilename, pMode);
    return (tFileHandle)(uintptr_t)pFile;
}

static int J3DAPI sithGamesaveTest_FileClose(tFileHandle fh)
{
    return fclose((FILE*)(uintptr_t)fh);
}

static size_t J3DAPI sithGamesaveTest_FileRead(tFileHandle fh, void* pData, size_t size)
{
    return fread(pData, 1u, size, (FILE*)(uintptr_t)fh);
}

static void sithGamesaveTest_DeleteFiles(void)
{
    remove(SITHGAMESAVE_TEST_FILE);
    remove(SITHGAMESAVE_TEST_SHORT);
}

static void sithGamesaveTest_WriteSave(const char* pFilename, int32_t bThumbnail, int32_t version, const uint8_t* pThumbnail, size_t thumbnailSize)
{
    NdsHeader header;
    memset(&header, 0, sizeof(header));
    header.version    = version;
    header.bThumbnail = bThumbnail;

    FILE* pFile = fopen(pFilename, "wb");
    TEST_ASSERT_NOT_NULL(pFile);
    TEST_ASSERT_EQUAL_size_t(1u, fwrite(&header, sizeof(header), 1u, pFile));
    if ( pThumbnail && thumbnailSize )
    {
        TEST_ASSERT_EQUAL_size_t(thumbnailSize, fwrite(pThumbnail, 1u, thumbnailSize, pFile));
    }
    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
}

static void sithGamesaveTest_WriteRaw(const char* pFilename, const void* pData, size_t size)
{
    FILE* pFile = fopen(pFilename, "wb");
    TEST_ASSERT_NOT_NULL(pFile);
    TEST_ASSERT_EQUAL_size_t(size, fwrite(pData, 1u, size, pFile));
    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
}

static void sithGamesaveTest_SetPixel(tVBuffer* pVBuffer, uint32_t x, uint32_t y, uint8_t blue, uint8_t green, uint8_t red)
{
    uint8_t* pPixel = &pVBuffer->pPixels[pVBuffer->rasterInfo.rowSize * y + (size_t)x * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL];
    pPixel[0] = blue;
    pPixel[1] = green;
    pPixel[2] = red;
}

static tVBuffer* sithGamesaveTest_CreateFrame(uint32_t width, uint32_t height)
{
    tRasterInfo rasterInfo;
    memset(&rasterInfo, 0, sizeof(rasterInfo));
    rasterInfo.width     = width;
    rasterInfo.height    = height;
    rasterInfo.rowSize   = (size_t)width * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL;
    rasterInfo.rowWidth  = width;
    rasterInfo.size      = rasterInfo.rowSize * height;
    rasterInfo.colorInfo = stdColor_cfRGB888;

    tVBuffer* pVBuffer = stdDisplay_VBufferNew(&rasterInfo, /*bUseVSurface=*/0, /*bUseVideoMemory=*/0);
    TEST_ASSERT_NOT_NULL(pVBuffer);
    TEST_ASSERT_NOT_NULL(pVBuffer->pPixels);
    return pVBuffer;
}

TEST_SETUP(sithGamesave)
{
    memset(&sithGamesaveTest_hostServices, 0, sizeof(sithGamesaveTest_hostServices));
    sithGamesaveTest_hostServices.pStatusPrint  = sithGamesaveTest_Print;
    sithGamesaveTest_hostServices.pMessagePrint = sithGamesaveTest_Print;
    sithGamesaveTest_hostServices.pWarningPrint = sithGamesaveTest_Print;
    sithGamesaveTest_hostServices.pErrorPrint   = sithGamesaveTest_Print;
    sithGamesaveTest_hostServices.pDebugPrint   = sithGamesaveTest_Print;
    sithGamesaveTest_hostServices.pMalloc       = sithGamesaveTest_Malloc;
    sithGamesaveTest_hostServices.pFree         = sithGamesaveTest_Free;
    sithGamesaveTest_hostServices.pRealloc      = sithGamesaveTest_Realloc;
    sithGamesaveTest_hostServices.pFileOpen     = sithGamesaveTest_FileOpen;
    sithGamesaveTest_hostServices.pFileClose    = sithGamesaveTest_FileClose;
    sithGamesaveTest_hostServices.pFileRead     = sithGamesaveTest_FileRead;

    std_g_pHS  = &sithGamesaveTest_hostServices;
    sith_g_pHS = &sithGamesaveTest_hostServices;

    // The test build compiles stdConffile with the sithThingTest allocation hooks, whose
    // allocation limit must be enabled before the first file open allocates the line buffer.
    sithThingTest_ResetAllocationFailure();

    sithGamesave_Close();
    sithGamesave_Startup();
    sithGamesaveTest_DeleteFiles();
}

TEST_TEAR_DOWN(sithGamesave)
{
    sithGamesave_Close();
    sithGamesaveTest_DeleteFiles();
    std_g_pHS  = NULL;
    sith_g_pHS = NULL;
}

TEST(sithGamesave, TestThumbnailBoxFilterLayoutAndRounding)
{
    // 256 x 192 is exactly two source pixels per destination pixel.
    tVBuffer* pFrame = sithGamesaveTest_CreateFrame(256u, 192u);
    for ( uint32_t y = 0; y < 192u; ++y )
    {
        for ( uint32_t x = 0; x < 256u; ++x )
        {
            const uint32_t dx = x / 2u;
            const uint32_t dy = y / 2u;
            uint8_t blue      = 0;
            uint8_t green     = 0;
            uint8_t red       = 0;

            if ( dx == 0u && dy == 0u )
            {
                blue = 10u;
                green = 20u;
                red = 30u;
            }
            else if ( dx == SITHSAVEGAME_THUMB_WIDTH - 1u && dy == 0u )
            {
                blue = 40u;
                green = 50u;
                red = 60u;
            }
            else if ( dx == 0u && dy == SITHSAVEGAME_THUMB_HEIGHT - 1u )
            {
                blue = 70u;
                green = 80u;
                red = 90u;
            }
            else if ( dx == SITHSAVEGAME_THUMB_WIDTH - 1u && dy == SITHSAVEGAME_THUMB_HEIGHT - 1u )
            {
                blue = 100u;
                green = 110u;
                red = 120u;
            }
            else if ( dx == 1u && dy == 1u )
            {
                // Blue 1,1,2,2 averages to 1.5 and must round up to 2. Green 0,0,0,1 averages to 0.25.
                static const uint8_t aBlue[4]  = { 1u, 1u, 2u, 2u };
                static const uint8_t aGreen[4] = { 0u, 0u, 0u, 1u };
                static const uint8_t aRed[4]   = { 10u, 20u, 30u, 40u };
                const size_t index             = (size_t)(y % 2u) * 2u + (x % 2u);
                blue                           = aBlue[index];
                green                          = aGreen[index];
                red                            = aRed[index];
            }

            sithGamesaveTest_SetPixel(pFrame, x, y, blue, green, red);
        }
    }

    sithGamesave_SetThumbnailImage(pFrame);
    TEST_ASSERT_NOT_NULL(sithGamesave_pThumbnailImage);
    TEST_ASSERT_NOT_NULL(sithGamesave_CreateThumbnail());
    TEST_ASSERT_EQUAL_INT(1, sithGamesave_bThumbnail);

    const uint8_t* pThumbnail = (const uint8_t*)sithGamesave_hBmpThumbnail;
    TEST_ASSERT_NOT_NULL(pThumbnail);

    const size_t topLeft     = 0u;
    const size_t topRight    = (size_t)(SITHSAVEGAME_THUMB_WIDTH - 1u) * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL;
    const size_t bottomLeft  = (size_t)(SITHSAVEGAME_THUMB_HEIGHT - 1u) * SITHSAVEGAME_THUMB_WIDTH * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL;
    const size_t bottomRight = bottomLeft + topRight;
    const size_t rounded     = ((size_t)SITHSAVEGAME_THUMB_WIDTH + 1u) * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL;

    // B, G, R, top row first.
    TEST_ASSERT_EQUAL_HEX8(10u, pThumbnail[topLeft + 0u]);
    TEST_ASSERT_EQUAL_HEX8(20u, pThumbnail[topLeft + 1u]);
    TEST_ASSERT_EQUAL_HEX8(30u, pThumbnail[topLeft + 2u]);
    TEST_ASSERT_EQUAL_HEX8(40u, pThumbnail[topRight + 0u]);
    TEST_ASSERT_EQUAL_HEX8(50u, pThumbnail[topRight + 1u]);
    TEST_ASSERT_EQUAL_HEX8(60u, pThumbnail[topRight + 2u]);
    TEST_ASSERT_EQUAL_HEX8(70u, pThumbnail[bottomLeft + 0u]);
    TEST_ASSERT_EQUAL_HEX8(80u, pThumbnail[bottomLeft + 1u]);
    TEST_ASSERT_EQUAL_HEX8(90u, pThumbnail[bottomLeft + 2u]);
    TEST_ASSERT_EQUAL_HEX8(100u, pThumbnail[bottomRight + 0u]);
    TEST_ASSERT_EQUAL_HEX8(110u, pThumbnail[bottomRight + 1u]);
    TEST_ASSERT_EQUAL_HEX8(120u, pThumbnail[bottomRight + 2u]);

    // The rounded average and a zero block between the markers.
    TEST_ASSERT_EQUAL_HEX8(2u, pThumbnail[rounded + 0u]);
    TEST_ASSERT_EQUAL_HEX8(0u, pThumbnail[rounded + 1u]);
    TEST_ASSERT_EQUAL_HEX8(25u, pThumbnail[rounded + 2u]);
    TEST_ASSERT_EQUAL_HEX8(0u, pThumbnail[(size_t)10u * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL + 0u]);
    TEST_ASSERT_EQUAL_HEX8(0u, pThumbnail[(size_t)10u * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL + 1u]);
    TEST_ASSERT_EQUAL_HEX8(0u, pThumbnail[(size_t)10u * SITHSAVEGAME_THUMB_BYTES_PER_PIXEL + 2u]);

    stdDisplay_VBufferFree(pFrame);
}

TEST(sithGamesave, TestReadThumbnailRoundTrips)
{
    for ( size_t i = 0; i < SITHSAVEGAME_THUMBSIZE; ++i )
    {
        sithGamesaveTest_aThumbnail[i] = (uint8_t)(i * 37u + 11u);
    }

    sithGamesaveTest_WriteSave(SITHGAMESAVE_TEST_FILE, 1, SITHSAVEGAME_FILEVERSION, sithGamesaveTest_aThumbnail, SITHSAVEGAME_THUMBSIZE);

    uint8_t aRead[SITHSAVEGAME_THUMBSIZE];
    memset(aRead, 0, sizeof(aRead));
    TEST_ASSERT_EQUAL_INT(1, sithGamesave_ReadThumbnail(SITHGAMESAVE_TEST_FILE, aRead, sizeof(aRead)));
    TEST_ASSERT_EQUAL_MEMORY(sithGamesaveTest_aThumbnail, aRead, SITHSAVEGAME_THUMBSIZE);

    // A file that says it has no thumbnail returns 0 even though more bytes follow.
    sithGamesaveTest_WriteSave(SITHGAMESAVE_TEST_FILE, 0, SITHSAVEGAME_FILEVERSION, sithGamesaveTest_aThumbnail, SITHSAVEGAME_THUMBSIZE);
    TEST_ASSERT_EQUAL_INT(0, sithGamesave_ReadThumbnail(SITHGAMESAVE_TEST_FILE, aRead, sizeof(aRead)));
}

TEST(sithGamesave, TestReadThumbnailRejectsInvalidAndTruncatedFiles)
{
    uint8_t aRead[SITHSAVEGAME_THUMBSIZE];

    // Truncated header.
    sithGamesaveTest_WriteRaw(SITHGAMESAVE_TEST_SHORT, sithGamesaveTest_aThumbnail, sizeof(NdsHeader) / 2u);
    TEST_ASSERT_EQUAL_INT(0, sithGamesave_ReadThumbnail(SITHGAMESAVE_TEST_SHORT, aRead, sizeof(aRead)));

    // Header claims a thumbnail but the thumbnail is truncated.
    sithGamesaveTest_WriteSave(SITHGAMESAVE_TEST_FILE, 1, SITHSAVEGAME_FILEVERSION, sithGamesaveTest_aThumbnail, SITHSAVEGAME_THUMBSIZE / 2u);
    TEST_ASSERT_EQUAL_INT(0, sithGamesave_ReadThumbnail(SITHGAMESAVE_TEST_FILE, aRead, sizeof(aRead)));

    // Out-of-range thumbnail flags and versions.
    sithGamesaveTest_WriteSave(SITHGAMESAVE_TEST_FILE, 2, SITHSAVEGAME_FILEVERSION, sithGamesaveTest_aThumbnail, SITHSAVEGAME_THUMBSIZE);
    TEST_ASSERT_EQUAL_INT(0, sithGamesave_ReadThumbnail(SITHGAMESAVE_TEST_FILE, aRead, sizeof(aRead)));

    sithGamesaveTest_WriteSave(SITHGAMESAVE_TEST_FILE, 1, SITHSAVEGAME_FILEVERSION + 1, sithGamesaveTest_aThumbnail, SITHSAVEGAME_THUMBSIZE);
    TEST_ASSERT_EQUAL_INT(0, sithGamesave_ReadThumbnail(SITHGAMESAVE_TEST_FILE, aRead, sizeof(aRead)));

    // A destination too small for the fixed thumbnail is rejected before reading.
    sithGamesaveTest_WriteSave(SITHGAMESAVE_TEST_FILE, 1, SITHSAVEGAME_FILEVERSION, sithGamesaveTest_aThumbnail, SITHSAVEGAME_THUMBSIZE);
    TEST_ASSERT_EQUAL_INT(0, sithGamesave_ReadThumbnail(SITHGAMESAVE_TEST_FILE, aRead, SITHSAVEGAME_THUMBSIZE - 1u));

    // Missing file and invalid arguments.
    TEST_ASSERT_EQUAL_INT(0, sithGamesave_ReadThumbnail("sithGamesaveTest_missing.nds", aRead, sizeof(aRead)));
    TEST_ASSERT_EQUAL_INT(0, sithGamesave_ReadThumbnail(NULL, aRead, sizeof(aRead)));
    TEST_ASSERT_EQUAL_INT(0, sithGamesave_ReadThumbnail(SITHGAMESAVE_TEST_FILE, NULL, sizeof(aRead)));
}

TEST_GROUP_RUNNER(sithGamesave)
{
    RUN_TEST_CASE(sithGamesave, TestThumbnailBoxFilterLayoutAndRounding);
    RUN_TEST_CASE(sithGamesave, TestReadThumbnailRoundTrips);
    RUN_TEST_CASE(sithGamesave, TestReadThumbnailRejectsInvalidAndTruncatedFiles);
}
