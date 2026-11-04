// Added: M6.0c render capture replay. The reader keeps one record in memory at a time (blob
// content is read straight into its final buffer), rebuilds the recorded objects and makes the
// recorded calls through the public renderer functions, so the forwarders record the replay.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <std/General/std.h>
#include <std/General/stdMemory.h>
#include <std/General/stdRenderCapture.h>
#include <std/General/stdRenderCaptureRecord.h>
#include <std/General/stdUtil.h>

#include <std/Win95/std3D.h>
#include <std/Win95/stdDisplay.h>
#include <std/Win95/stdDisplayCapture.h>
#include <std/Win95/stdRenderCaptureIds.h>

#include "stdRenderReplay.h"

#define STDRENDERREPLAY_VERSION           1u
#define STDRENDERREPLAY_MAXCOLUMNLEN      64u
#define STDRENDERREPLAY_READCHUNKSIZE     65536u
#define STDRENDERREPLAY_MAXPAYLOADSIZE    (256u * 1024u * 1024u)
#define STDRENDERREPLAY_RECORDHEADERSIZE  8u
#define STDRENDERREPLAY_BLOBPREFIXSIZE    12u
#define STDRENDERREPLAY_MAXMISMATCHLOGS   10u
#define STDRENDERREPLAY_UNKNOWNHANDLE     0xFFFFFFFFu

static const uint8_t stdRenderReplay_aSignature[8] = { 'J', '3', 'D', 'R', 'C', 'A', 'P', 0 };

// Added: the zeroed pixel format a recorded non-NULL stdDisplay_CreateZBuffer argument maps to.
static tSysPixelFormat stdRenderReplay_zeroPixelFormat;

typedef struct sStdRenderReplayReader
{
    tHostServices* pHS;
    tFileHandle file;
    uint8_t aBuffer[STDRENDERREPLAY_READCHUNKSIZE];
    size_t bufferPos;
    size_t bufferSize;
    uint64_t offset; // absolute offset of the next byte the caller has not seen
} StdRenderReplayReader;

typedef struct sStdRenderReplayCursor
{
    const uint8_t* pData;
    size_t size;
    size_t pos;
    bool bFailed;
} StdRenderReplayCursor;

typedef struct sStdRenderReplayBlob
{
    uint8_t* pData;
    size_t size;
} StdRenderReplayBlob;

typedef struct sStdRenderReplayVBuffer
{
    uint32_t handle;
    tVBuffer* pVBuffer;
    size_t allocationSize;
} StdRenderReplayVBuffer;

typedef struct sStdRenderReplaySysTexture
{
    uint32_t handle;
    tSystemTexture* pTexture;
} StdRenderReplaySysTexture;

typedef struct sStdRenderReplay
{
    StdRenderReplayReader reader;

    StdRenderReplayBlob* pBlobs;
    size_t numBlobs;
    size_t blobCapacity;

    StdRenderReplayVBuffer* pVBuffers;
    size_t numVBuffers;
    size_t vBufferCapacity;

    StdRenderReplaySysTexture* pSysTextures;
    size_t numSysTextures;
    size_t sysTextureCapacity;

    void* pBackBufferPixels;
    uint32_t backBufferHeight;
    int32_t backBufferPitch;
    bool bHasBackBufferLock;
    size_t backBufferAllocationSize;
    bool bBackBufferAllocationKnown;

    StdDisplayEnvironment* pDisplayEnvironment;

    StdVertexTL* pVertices;
    size_t vertexCapacity;
    uint16_t* pIndices;
    size_t indexCapacity;
    tVBuffer** ppMipmaps;
    size_t mipmapCapacity;

    bool bOutputOpen;
    bool bWarnedUnknownTexture;
    bool bWarnedBlobSize;

    size_t numRecords;
    size_t numBlobRecords;
    size_t numSkipped;
    size_t numMismatches;
    size_t numMismatchLogs;
} StdRenderReplay;

static size_t StdRenderReplayReader_Read(StdRenderReplayReader* pReader, void* pDest, size_t size)
{
    uint8_t* pOut = (uint8_t*)pDest;
    size_t total = 0u;

    while ( total < size )
    {
        size_t numCopy;

        if ( pReader->bufferPos == pReader->bufferSize )
        {
            size_t numRead = pReader->pHS->pFileRead(pReader->file, pReader->aBuffer, sizeof(pReader->aBuffer));
            if ( numRead == 0u )
            {
                break;
            }

            pReader->bufferPos  = 0u;
            pReader->bufferSize = numRead;
        }

        numCopy = pReader->bufferSize - pReader->bufferPos;
        if ( numCopy > size - total )
        {
            numCopy = size - total;
        }

        if ( pOut )
        {
            memcpy(&pOut[total], &pReader->aBuffer[pReader->bufferPos], numCopy);
        }

        pReader->bufferPos += numCopy;
        pReader->offset    += numCopy;
        total              += numCopy;
    }

    return total;
}

static bool StdRenderReplayReader_Skip(StdRenderReplayReader* pReader, uint64_t size)
{
    while ( size > 0u )
    {
        size_t numChunk = size > STDRENDERREPLAY_READCHUNKSIZE
            ? STDRENDERREPLAY_READCHUNKSIZE
            : (size_t)size;

        if ( StdRenderReplayReader_Read(pReader, NULL, numChunk) != numChunk )
        {
            return false;
        }

        size -= numChunk;
    }

    return true;
}

static bool StdRenderReplayCursor_Has(const StdRenderReplayCursor* pCursor, size_t size)
{
    return !pCursor->bFailed && size <= pCursor->size - pCursor->pos;
}

static uint8_t StdRenderReplayCursor_GetU8(StdRenderReplayCursor* pCursor)
{
    if ( !StdRenderReplayCursor_Has(pCursor, 1u) )
    {
        pCursor->bFailed = true;
        return 0u;
    }

    return pCursor->pData[pCursor->pos++];
}

static uint16_t StdRenderReplayCursor_GetU16(StdRenderReplayCursor* pCursor)
{
    uint16_t value;

    if ( !StdRenderReplayCursor_Has(pCursor, 2u) )
    {
        pCursor->bFailed = true;
        return 0u;
    }

    value = (uint16_t)pCursor->pData[pCursor->pos];
    value |= (uint16_t)((uint16_t)pCursor->pData[pCursor->pos + 1u] << 8);
    pCursor->pos += 2u;
    return value;
}

static uint32_t StdRenderReplayCursor_GetU32(StdRenderReplayCursor* pCursor)
{
    uint32_t value;

    if ( !StdRenderReplayCursor_Has(pCursor, 4u) )
    {
        pCursor->bFailed = true;
        return 0u;
    }

    value = (uint32_t)pCursor->pData[pCursor->pos];
    value |= (uint32_t)pCursor->pData[pCursor->pos + 1u] << 8;
    value |= (uint32_t)pCursor->pData[pCursor->pos + 2u] << 16;
    value |= (uint32_t)pCursor->pData[pCursor->pos + 3u] << 24;
    pCursor->pos += 4u;
    return value;
}

static int32_t StdRenderReplayCursor_GetI32(StdRenderReplayCursor* pCursor)
{
    return (int32_t)StdRenderReplayCursor_GetU32(pCursor);
}

static uint64_t StdRenderReplayCursor_GetU64(StdRenderReplayCursor* pCursor)
{
    uint64_t low  = StdRenderReplayCursor_GetU32(pCursor);
    uint64_t high = StdRenderReplayCursor_GetU32(pCursor);
    return low | ( high << 32 );
}

static float StdRenderReplayCursor_GetF32(StdRenderReplayCursor* pCursor)
{
    uint32_t bits = StdRenderReplayCursor_GetU32(pCursor);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool StdRenderReplayCursor_GetBytes(StdRenderReplayCursor* pCursor, void* pDest, size_t size)
{
    if ( !StdRenderReplayCursor_Has(pCursor, size) )
    {
        pCursor->bFailed = true;
        return false;
    }

    memcpy(pDest, &pCursor->pData[pCursor->pos], size);
    pCursor->pos += size;
    return true;
}

static void StdRenderReplay_ReadColorInfo(StdRenderReplayCursor* pCursor, ColorInfo* pColorInfo)
{
    pColorInfo->colorMode          = (tColorMode)StdRenderReplayCursor_GetI32(pCursor);
    pColorInfo->bpp                = StdRenderReplayCursor_GetU32(pCursor);
    pColorInfo->redBPP             = StdRenderReplayCursor_GetU32(pCursor);
    pColorInfo->greenBPP           = StdRenderReplayCursor_GetU32(pCursor);
    pColorInfo->blueBPP            = StdRenderReplayCursor_GetU32(pCursor);
    pColorInfo->redPosShift        = StdRenderReplayCursor_GetI32(pCursor);
    pColorInfo->greenPosShift      = StdRenderReplayCursor_GetI32(pCursor);
    pColorInfo->bluePosShift       = StdRenderReplayCursor_GetI32(pCursor);
    pColorInfo->redPosShiftRight   = StdRenderReplayCursor_GetI32(pCursor);
    pColorInfo->greenPosShiftRight = StdRenderReplayCursor_GetI32(pCursor);
    pColorInfo->bluePosShiftRight  = StdRenderReplayCursor_GetI32(pCursor);
    pColorInfo->alphaBPP           = StdRenderReplayCursor_GetU32(pCursor);
    pColorInfo->alphaPosShift      = StdRenderReplayCursor_GetI32(pCursor);
    pColorInfo->alphaPosShiftRight = StdRenderReplayCursor_GetI32(pCursor);
}

static bool StdRenderReplay_ReadOptColorInfo(
    StdRenderReplayCursor* pCursor,
    ColorInfo* pColorInfo,
    bool* pbPresent
)
{
    uint8_t present = StdRenderReplayCursor_GetU8(pCursor);

    if ( pCursor->bFailed )
    {
        return false;
    }

    if ( present == 0u )
    {
        *pbPresent = false;
        return true;
    }

    if ( present != 1u )
    {
        pCursor->bFailed = true;
        return false;
    }

    StdRenderReplay_ReadColorInfo(pCursor, pColorInfo);
    *pbPresent = true;
    return !pCursor->bFailed;
}

static bool StdRenderReplay_ReadOptRect(StdRenderReplayCursor* pCursor, StdRect* pRect, bool* pbPresent)
{
    uint8_t present = StdRenderReplayCursor_GetU8(pCursor);

    if ( pCursor->bFailed )
    {
        return false;
    }

    if ( present == 0u )
    {
        *pbPresent = false;
        return true;
    }

    if ( present != 1u )
    {
        pCursor->bFailed = true;
        return false;
    }

    pRect->left   = StdRenderReplayCursor_GetI32(pCursor);
    pRect->top    = StdRenderReplayCursor_GetI32(pCursor);
    pRect->right  = StdRenderReplayCursor_GetI32(pCursor);
    pRect->bottom = StdRenderReplayCursor_GetI32(pCursor);
    *pbPresent    = true;
    return !pCursor->bFailed;
}

static bool StdRenderReplay_ReadOptColorKey(
    StdRenderReplayCursor* pCursor,
    DDCOLORKEY* pColorKey,
    bool* pbPresent
)
{
    uint8_t present = StdRenderReplayCursor_GetU8(pCursor);

    if ( pCursor->bFailed )
    {
        return false;
    }

    if ( present == 0u )
    {
        *pbPresent = false;
        return true;
    }

    if ( present != 1u )
    {
        pCursor->bFailed = true;
        return false;
    }

    pColorKey->dwColorSpaceLowValue  = StdRenderReplayCursor_GetU32(pCursor);
    pColorKey->dwColorSpaceHighValue = StdRenderReplayCursor_GetU32(pCursor);
    *pbPresent                       = true;
    return !pCursor->bFailed;
}

static bool StdRenderReplay_ReadOptRasterInfo(
    StdRenderReplayCursor* pCursor,
    tRasterInfo* pRasterInfo,
    bool* pbPresent
)
{
    uint8_t present = StdRenderReplayCursor_GetU8(pCursor);

    if ( pCursor->bFailed )
    {
        return false;
    }

    if ( present == 0u )
    {
        *pbPresent = false;
        return true;
    }

    if ( present != 1u )
    {
        pCursor->bFailed = true;
        return false;
    }

    pRasterInfo->width  = StdRenderReplayCursor_GetU32(pCursor);
    pRasterInfo->height = StdRenderReplayCursor_GetU32(pCursor);
    StdRenderReplay_ReadColorInfo(pCursor, &pRasterInfo->colorInfo);
    pRasterInfo->size     = 0u;
    pRasterInfo->rowSize  = 0u;
    pRasterInfo->rowWidth = 0u;
    *pbPresent            = true;
    return !pCursor->bFailed;
}

static void StdRenderReplay_ReadRasterOut(StdRenderReplayCursor* pCursor, tRasterInfo* pRasterInfo)
{
    pRasterInfo->width    = StdRenderReplayCursor_GetU32(pCursor);
    pRasterInfo->height   = StdRenderReplayCursor_GetU32(pCursor);
    pRasterInfo->size     = (size_t)StdRenderReplayCursor_GetU64(pCursor);
    pRasterInfo->rowSize  = (size_t)StdRenderReplayCursor_GetU64(pCursor);
    pRasterInfo->rowWidth = (size_t)StdRenderReplayCursor_GetU64(pCursor);
    StdRenderReplay_ReadColorInfo(pCursor, &pRasterInfo->colorInfo);
}

static bool StdRenderReplay_ReadOptVideoMode(
    StdRenderReplayCursor* pCursor,
    StdVideoMode* pVideoMode,
    bool* pbPresent
)
{
    uint8_t present = StdRenderReplayCursor_GetU8(pCursor);

    if ( pCursor->bFailed )
    {
        return false;
    }

    if ( present == 0u )
    {
        *pbPresent = false;
        return true;
    }

    if ( present != 1u )
    {
        pCursor->bFailed = true;
        return false;
    }

    pVideoMode->aspectRatio = StdRenderReplayCursor_GetF32(pCursor);
    StdRenderReplay_ReadRasterOut(pCursor, &pVideoMode->rasterInfo);
    pVideoMode->refreshRate = StdRenderReplayCursor_GetU32(pCursor);
    *pbPresent              = true;
    return !pCursor->bFailed;
}

static bool StdRenderReplay_ReserveVertices(StdRenderReplay* pReplay, size_t count)
{
    size_t capacity;
    void* pData;

    if ( count <= pReplay->vertexCapacity )
    {
        return true;
    }

    capacity = pReplay->vertexCapacity ? pReplay->vertexCapacity : 16u;
    while ( capacity < count )
    {
        if ( capacity > SIZE_MAX / 2u )
        {
            capacity = count;
            break;
        }

        capacity *= 2u;
    }

    if ( capacity > SIZE_MAX / sizeof(StdVertexTL) )
    {
        return false;
    }

    pData = STDREALLOC(pReplay->pVertices, capacity * sizeof(StdVertexTL));
    if ( !pData )
    {
        return false;
    }

    pReplay->pVertices      = (StdVertexTL*)pData;
    pReplay->vertexCapacity = capacity;
    return true;
}

static bool StdRenderReplay_ReserveIndices(StdRenderReplay* pReplay, size_t count)
{
    size_t capacity;
    void* pData;

    if ( count <= pReplay->indexCapacity )
    {
        return true;
    }

    capacity = pReplay->indexCapacity ? pReplay->indexCapacity : 32u;
    while ( capacity < count )
    {
        if ( capacity > SIZE_MAX / 2u )
        {
            capacity = count;
            break;
        }

        capacity *= 2u;
    }

    if ( capacity > SIZE_MAX / sizeof(uint16_t) )
    {
        return false;
    }

    pData = STDREALLOC(pReplay->pIndices, capacity * sizeof(uint16_t));
    if ( !pData )
    {
        return false;
    }

    pReplay->pIndices      = (uint16_t*)pData;
    pReplay->indexCapacity = capacity;
    return true;
}

static bool StdRenderReplay_ReserveMipmaps(StdRenderReplay* pReplay, size_t count)
{
    size_t capacity;
    void* pData;

    if ( count <= pReplay->mipmapCapacity )
    {
        return true;
    }

    capacity = pReplay->mipmapCapacity ? pReplay->mipmapCapacity : 8u;
    while ( capacity < count )
    {
        if ( capacity > SIZE_MAX / 2u )
        {
            capacity = count;
            break;
        }

        capacity *= 2u;
    }

    if ( capacity > SIZE_MAX / sizeof(tVBuffer*) )
    {
        return false;
    }

    pData = STDREALLOC(pReplay->ppMipmaps, capacity * sizeof(tVBuffer*));
    if ( !pData )
    {
        return false;
    }

    pReplay->ppMipmaps      = (tVBuffer**)pData;
    pReplay->mipmapCapacity = capacity;
    return true;
}

static bool StdRenderReplay_ReadVertices(
    StdRenderReplay* pReplay,
    StdRenderReplayCursor* pCursor,
    StdVertexTL** ppVertices,
    size_t* pNumVertices
)
{
    uint32_t count = StdRenderReplayCursor_GetU32(pCursor);
    size_t i;

    if ( pCursor->bFailed )
    {
        return false;
    }

    if ( (uint64_t)count * sizeof(StdVertexTL) > (uint64_t)(pCursor->size - pCursor->pos) )
    {
        pCursor->bFailed = true;
        return false;
    }

    if ( !StdRenderReplay_ReserveVertices(pReplay, count) )
    {
        pCursor->bFailed = true;
        return false;
    }

    for ( i = 0; i < count; ++i )
    {
        pReplay->pVertices[i].sx       = StdRenderReplayCursor_GetF32(pCursor);
        pReplay->pVertices[i].sy       = StdRenderReplayCursor_GetF32(pCursor);
        pReplay->pVertices[i].sz       = StdRenderReplayCursor_GetF32(pCursor);
        pReplay->pVertices[i].rhw      = StdRenderReplayCursor_GetF32(pCursor);
        pReplay->pVertices[i].color    = StdRenderReplayCursor_GetU32(pCursor);
        pReplay->pVertices[i].specular = StdRenderReplayCursor_GetU32(pCursor);
        pReplay->pVertices[i].tu       = StdRenderReplayCursor_GetF32(pCursor);
        pReplay->pVertices[i].tv       = StdRenderReplayCursor_GetF32(pCursor);
    }

    *ppVertices   = count ? pReplay->pVertices : NULL;
    *pNumVertices = count;
    return !pCursor->bFailed;
}

static bool StdRenderReplay_ReadIndices(
    StdRenderReplay* pReplay,
    StdRenderReplayCursor* pCursor,
    uint16_t** ppIndices,
    size_t* pNumIndices
)
{
    uint32_t count = StdRenderReplayCursor_GetU32(pCursor);
    size_t i;

    if ( pCursor->bFailed )
    {
        return false;
    }

    if ( (uint64_t)count * sizeof(uint16_t) > (uint64_t)(pCursor->size - pCursor->pos) )
    {
        pCursor->bFailed = true;
        return false;
    }

    if ( !StdRenderReplay_ReserveIndices(pReplay, count) )
    {
        pCursor->bFailed = true;
        return false;
    }

    for ( i = 0; i < count; ++i )
    {
        pReplay->pIndices[i] = StdRenderReplayCursor_GetU16(pCursor);
    }

    *ppIndices   = count ? pReplay->pIndices : NULL;
    *pNumIndices = count;
    return !pCursor->bFailed;
}

static StdRenderReplayBlob* StdRenderReplay_FindBlob(StdRenderReplay* pReplay, uint32_t blobId)
{
    if ( blobId == 0u || blobId > pReplay->numBlobs )
    {
        return NULL;
    }

    return &pReplay->pBlobs[blobId - 1u];
}

static bool StdRenderReplay_AppendBlob(StdRenderReplay* pReplay, uint8_t* pData, size_t size)
{
    size_t capacity;
    void* pBlobs;

    if ( pReplay->numBlobs == pReplay->blobCapacity )
    {
        capacity = pReplay->blobCapacity ? pReplay->blobCapacity * 2u : 8u;
        if ( capacity > SIZE_MAX / sizeof(StdRenderReplayBlob) )
        {
            return false;
        }

        pBlobs = STDREALLOC(pReplay->pBlobs, capacity * sizeof(StdRenderReplayBlob));
        if ( !pBlobs )
        {
            return false;
        }

        pReplay->pBlobs        = (StdRenderReplayBlob*)pBlobs;
        pReplay->blobCapacity  = capacity;
    }

    pReplay->pBlobs[pReplay->numBlobs].pData = pData;
    pReplay->pBlobs[pReplay->numBlobs].size  = size;
    ++pReplay->numBlobs;
    return true;
}

static StdRenderReplayVBuffer* StdRenderReplay_FindVBufferEntry(StdRenderReplay* pReplay, uint32_t handle)
{
    size_t i;

    for ( i = 0; i < pReplay->numVBuffers; ++i )
    {
        if ( pReplay->pVBuffers[i].handle == handle )
        {
            return &pReplay->pVBuffers[i];
        }
    }

    return NULL;
}

static tVBuffer* StdRenderReplay_FindVBuffer(StdRenderReplay* pReplay, uint32_t handle)
{
    StdRenderReplayVBuffer* pEntry;

    if ( handle == 1u )
    {
        return &stdDisplay_g_backBuffer;
    }

    pEntry = StdRenderReplay_FindVBufferEntry(pReplay, handle);
    return pEntry ? pEntry->pVBuffer : NULL;
}

static bool StdRenderReplay_ComputeAllocationSize(const tVBuffer* pVBuffer, size_t* pSize)
{
    uint64_t size;

    if ( !pVBuffer )
    {
        *pSize = 0u;
        return true;
    }

    if ( pVBuffer->rasterInfo.rowSize != 0u
        && (uint64_t)pVBuffer->rasterInfo.height > UINT64_MAX / (uint64_t)pVBuffer->rasterInfo.rowSize )
    {
        return false;
    }

    size = (uint64_t)pVBuffer->rasterInfo.height * (uint64_t)pVBuffer->rasterInfo.rowSize;
    if ( size > (uint64_t)SIZE_MAX )
    {
        return false;
    }

    *pSize = (size_t)size;
    return true;
}

static bool StdRenderReplay_DefineVBuffer(
    StdRenderReplay* pReplay,
    uint32_t handle,
    tVBuffer* pVBuffer,
    size_t allocationSize
)
{
    StdRenderReplayVBuffer* pExisting;
    size_t capacity;
    size_t i;
    void* pEntries;

    if ( !pVBuffer || handle == 0u )
    {
        return false;
    }

    // A conversion may return its source buffer, whose handle the writer then reports again.
    pExisting = StdRenderReplay_FindVBufferEntry(pReplay, handle);
    if ( pExisting )
    {
        return pExisting->pVBuffer == pVBuffer;
    }

    // No two handles may name the same live pointer.
    for ( i = 0; i < pReplay->numVBuffers; ++i )
    {
        if ( pReplay->pVBuffers[i].pVBuffer == pVBuffer )
        {
            return false;
        }
    }

    if ( pReplay->numVBuffers == pReplay->vBufferCapacity )
    {
        capacity = pReplay->vBufferCapacity ? pReplay->vBufferCapacity * 2u : 8u;
        if ( capacity > SIZE_MAX / sizeof(StdRenderReplayVBuffer) )
        {
            return false;
        }

        pEntries = STDREALLOC(pReplay->pVBuffers, capacity * sizeof(StdRenderReplayVBuffer));
        if ( !pEntries )
        {
            return false;
        }

        pReplay->pVBuffers      = (StdRenderReplayVBuffer*)pEntries;
        pReplay->vBufferCapacity = capacity;
    }

    pReplay->pVBuffers[pReplay->numVBuffers].handle         = handle;
    pReplay->pVBuffers[pReplay->numVBuffers].pVBuffer       = pVBuffer;
    pReplay->pVBuffers[pReplay->numVBuffers].allocationSize = allocationSize;
    ++pReplay->numVBuffers;
    return true;
}

static void StdRenderReplay_RemoveVBuffer(StdRenderReplay* pReplay, uint32_t handle)
{
    size_t i;

    for ( i = 0; i < pReplay->numVBuffers; ++i )
    {
        if ( pReplay->pVBuffers[i].handle == handle )
        {
            memmove(
                &pReplay->pVBuffers[i],
                &pReplay->pVBuffers[i + 1u],
                ( pReplay->numVBuffers - i - 1u ) * sizeof(StdRenderReplayVBuffer)
            );
            --pReplay->numVBuffers;
            return;
        }
    }
}

static tSystemTexture* StdRenderReplay_FindSysTexture(StdRenderReplay* pReplay, uint32_t handle)
{
    size_t i;

    for ( i = 0; i < pReplay->numSysTextures; ++i )
    {
        if ( pReplay->pSysTextures[i].handle == handle )
        {
            return pReplay->pSysTextures[i].pTexture;
        }
    }

    return NULL;
}

static tSystemTexture* StdRenderReplay_EnsureSysTexture(StdRenderReplay* pReplay, uint32_t handle)
{
    tSystemTexture* pTexture;
    size_t capacity;
    void* pEntries;

    if ( handle == 0u )
    {
        return NULL;
    }

    pTexture = StdRenderReplay_FindSysTexture(pReplay, handle);
    if ( pTexture )
    {
        return pTexture;
    }

    pTexture = (tSystemTexture*)STDMALLOC(sizeof(tSystemTexture));
    if ( !pTexture )
    {
        STDLOG_ERROR("Render replay: could not allocate a system texture.\n");
        return NULL;
    }

    STD_ZEROMEM(pTexture, sizeof(*pTexture));

    if ( pReplay->numSysTextures == pReplay->sysTextureCapacity )
    {
        capacity = pReplay->sysTextureCapacity ? pReplay->sysTextureCapacity * 2u : 8u;
        if ( capacity > SIZE_MAX / sizeof(StdRenderReplaySysTexture) )
        {
            STDFREE(pTexture);
            return NULL;
        }

        pEntries = STDREALLOC(pReplay->pSysTextures, capacity * sizeof(StdRenderReplaySysTexture));
        if ( !pEntries )
        {
            STDFREE(pTexture);
            return NULL;
        }

        pReplay->pSysTextures       = (StdRenderReplaySysTexture*)pEntries;
        pReplay->sysTextureCapacity = capacity;
    }

    pReplay->pSysTextures[pReplay->numSysTextures].handle   = handle;
    pReplay->pSysTextures[pReplay->numSysTextures].pTexture = pTexture;
    ++pReplay->numSysTextures;
    return pTexture;
}

static void StdRenderReplay_RemoveSysTexture(StdRenderReplay* pReplay, uint32_t handle)
{
    size_t i;

    for ( i = 0; i < pReplay->numSysTextures; ++i )
    {
        if ( pReplay->pSysTextures[i].handle == handle )
        {
            memmove(
                &pReplay->pSysTextures[i],
                &pReplay->pSysTextures[i + 1u],
                ( pReplay->numSysTextures - i - 1u ) * sizeof(StdRenderReplaySysTexture)
            );
            --pReplay->numSysTextures;
            return;
        }
    }
}

static void StdRenderReplay_MismatchU(
    StdRenderReplay* pReplay,
    size_t recordIndex,
    uint16_t id,
    const char* pField,
    uint64_t expected,
    uint64_t actual
)
{
    if ( expected == actual )
    {
        return;
    }

    ++pReplay->numMismatches;
    if ( pReplay->numMismatchLogs < STDRENDERREPLAY_MAXMISMATCHLOGS )
    {
        ++pReplay->numMismatchLogs;
        STDLOG_WARNING(
            "Render replay: mismatch at record %llu (id %u) %s: recorded %llu, replayed %llu\n",
            (unsigned long long)recordIndex,
            (unsigned)id,
            pField,
            (unsigned long long)expected,
            (unsigned long long)actual
        );
    }
}

static void StdRenderReplay_MismatchI(
    StdRenderReplay* pReplay,
    size_t recordIndex,
    uint16_t id,
    const char* pField,
    int64_t expected,
    int64_t actual
)
{
    if ( expected == actual )
    {
        return;
    }

    ++pReplay->numMismatches;
    if ( pReplay->numMismatchLogs < STDRENDERREPLAY_MAXMISMATCHLOGS )
    {
        ++pReplay->numMismatchLogs;
        STDLOG_WARNING(
            "Render replay: mismatch at record %llu (id %u) %s: recorded %lld, replayed %lld\n",
            (unsigned long long)recordIndex,
            (unsigned)id,
            pField,
            (long long)expected,
            (long long)actual
        );
    }
}

static bool StdRenderReplay_RestorePixels(
    StdRenderReplay* pReplay,
    uint8_t* pDest,
    size_t destSize,
    uint32_t blobId
)
{
    StdRenderReplayBlob* pBlob;
    size_t copySize;

    if ( blobId == 0u )
    {
        return true;
    }

    pBlob = StdRenderReplay_FindBlob(pReplay, blobId);
    if ( !pBlob || !pDest )
    {
        return false;
    }

    copySize = pBlob->size;
    if ( copySize != destSize )
    {
        if ( !pReplay->bWarnedBlobSize )
        {
            pReplay->bWarnedBlobSize = true;
            STDLOG_WARNING(
                "Render replay: blob %u is %llu bytes but the buffer is %llu; copying the smaller size.\n",
                (unsigned)blobId,
                (unsigned long long)copySize,
                (unsigned long long)destSize
            );
        }

        if ( copySize > destSize )
        {
            copySize = destSize;
        }
    }

    if ( copySize > 0u )
    {
        memcpy(pDest, pBlob->pData, copySize);
    }

    return true;
}

static bool StdRenderReplay_RestoreVBufferBlob(
    StdRenderReplay* pReplay,
    tVBuffer* pVBuffer,
    uint32_t blobId
)
{
    uint64_t size;
    uint8_t* pPixels;
    bool bLocked = false;
    bool bOk;

    if ( blobId == 0u )
    {
        return true;
    }

    if ( !pVBuffer || pVBuffer->rasterInfo.rowSize == 0u )
    {
        return false;
    }

    size = (uint64_t)pVBuffer->rasterInfo.height * (uint64_t)pVBuffer->rasterInfo.rowSize;
    if ( size > (uint64_t)SIZE_MAX )
    {
        return false;
    }

    pPixels = pVBuffer->pPixels;
    if ( !pPixels )
    {
        if ( pVBuffer->type != VBUFFER_HARDWARE || !stdDisplay_VBufferLock(pVBuffer) )
        {
            return false;
        }

        pPixels = pVBuffer->pPixels;
        bLocked = true;
    }

    if ( !pPixels )
    {
        if ( bLocked )
        {
            stdDisplay_VBufferUnlock(pVBuffer);
        }

        return false;
    }

    bOk = StdRenderReplay_RestorePixels(pReplay, pPixels, (size_t)size, blobId);
    if ( bLocked )
    {
        stdDisplay_VBufferUnlock(pVBuffer);
    }

    return bOk;
}

static void StdRenderReplay_LogRecordError(
    StdRenderReplay* pReplay,
    size_t recordIndex,
    uint16_t id,
    uint64_t offset,
    const char* pReason
)
{
    (void)pReplay;
    STDLOG_ERROR(
        "Render replay: malformed record %llu (id %u) at offset %llu: %s\n",
        (unsigned long long)recordIndex,
        (unsigned)id,
        (unsigned long long)offset,
        pReason
    );
}

static bool StdRenderReplay_IsKnownId(uint16_t id)
{
    return ( id >= STDRENDERCAPTURE_ID_STD3D_STARTUP && id <= STDRENDERCAPTURE_ID_STD3D_RESETGLOBALS )
        || ( id >= STDRENDERCAPTURE_ID_STDDISPLAY_STARTUP && id <= STDRENDERCAPTURE_ID_STDDISPLAY_RESETGLOBALS )
        || id == STDRENDERCAPTURE_RECORD_GLOBAL
        || id == STDRENDERCAPTURE_RECORD_FRAMES
        || id == STDRENDERCAPTURE_RECORD_VBSTATE;
}

static bool StdRenderReplay_IsSkippedId(uint16_t id)
{
    switch ( id )
    {
        case STDRENDERCAPTURE_ID_STDDISPLAY_REGISTERDEVICEPRERESETCALLBACK:
        case STDRENDERCAPTURE_ID_STDDISPLAY_REGISTERDEVICEPOSTRESETCALLBACK:
        case STDRENDERCAPTURE_ID_STDDISPLAY_REGISTERDEVICERELEASECALLBACK:
        case STDRENDERCAPTURE_ID_STDDISPLAY_SAVESCREEN:
        case STDRENDERCAPTURE_ID_STDDISPLAY_GETFRONTBUFFERDC:
        case STDRENDERCAPTURE_ID_STDDISPLAY_RELEASEFRONTBUFFERDC:
        case STDRENDERCAPTURE_ID_STDDISPLAY_GETBACKBUFFERDC:
        case STDRENDERCAPTURE_ID_STDDISPLAY_RELEASEBACKBUFFERDC:
            return true;
        default:
            return false;
    }
}

static bool StdRenderReplay_ProcessRecord(
    StdRenderReplay* pReplay,
    uint16_t id,
    StdRenderReplayCursor* pCursor,
    size_t recordIndex
)
{
    switch ( id )
    {
        case STDRENDERCAPTURE_RECORD_VBSTATE:
        {
            uint32_t handle = StdRenderReplayCursor_GetU32(pCursor);
            tRasterInfo rasterInfo;
            tVBuffer* pVBuffer;
            StdRenderReplayVBuffer* pEntry;
            size_t allocationSize;
            uint64_t required;

            StdRenderReplay_ReadRasterOut(pCursor, &rasterInfo);
            if ( pCursor->bFailed )
            {
                return false;
            }

            pVBuffer = handle ? StdRenderReplay_FindVBuffer(pReplay, handle) : NULL;
            if ( !pVBuffer )
            {
                return false;
            }

            if ( handle == 1u )
            {
                if ( !pReplay->bBackBufferAllocationKnown )
                {
                    pReplay->backBufferAllocationSize    = pVBuffer->rasterInfo.size;
                    pReplay->bBackBufferAllocationKnown = true;
                }

                allocationSize = pReplay->backBufferAllocationSize;
            }
            else
            {
                pEntry = StdRenderReplay_FindVBufferEntry(pReplay, handle);
                if ( !pEntry )
                {
                    return false;
                }

                allocationSize = pEntry->allocationSize;
            }

            if ( rasterInfo.rowSize != 0u
                && (uint64_t)rasterInfo.height > UINT64_MAX / (uint64_t)rasterInfo.rowSize )
            {
                return false;
            }

            required = (uint64_t)rasterInfo.height * (uint64_t)rasterInfo.rowSize;
            if ( required > (uint64_t)allocationSize )
            {
                return false;
            }

            pVBuffer->rasterInfo = rasterInfo;
            return true;
        }

        case STDRENDERCAPTURE_RECORD_GLOBAL:
        {
            uint32_t globalId  = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t valueBits = StdRenderReplayCursor_GetU32(pCursor);
            float value;

            if ( pCursor->bFailed || globalId != 1u )
            {
                return false;
            }

            memcpy(&value, &valueBits, sizeof(value));
            std3D_g_fogDensity = value;
            return true;
        }

        case STDRENDERCAPTURE_RECORD_FRAMES:
        {
            uint8_t enabled = StdRenderReplayCursor_GetU8(pCursor);

            if ( pCursor->bFailed || enabled > 1u )
            {
                return false;
            }

            stdRenderCapture_SetFramesEnabled(enabled != 0u);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_STARTUP:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = std3D_Startup();
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_SHUTDOWN:
            std3D_Shutdown();
            return true;

        case STDRENDERCAPTURE_ID_STD3D_GETCURRENTDEVICE:
        {
            uint8_t expected = StdRenderReplayCursor_GetU8(pCursor);
            const Device3D* pResult;

            if ( pCursor->bFailed )
            {
                return false;
            }

            pResult = std3D_GetCurrentDevice();
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, pResult != NULL);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_GETNUMDEVICES:
        {
            uint64_t expected = StdRenderReplayCursor_GetU64(pCursor);
            size_t result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = std3D_GetNumDevices();
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, (uint64_t)result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_GETALLDEVICES:
        {
            uint8_t expected = StdRenderReplayCursor_GetU8(pCursor);
            const Device3D* pResult;

            if ( pCursor->bFailed )
            {
                return false;
            }

            pResult = std3D_GetAllDevices();
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, pResult != NULL);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_SETFINDALLDEVICES:
        {
            int32_t bFindAll = StdRenderReplayCursor_GetI32(pCursor);

            if ( pCursor->bFailed )
            {
                return false;
            }

            std3D_SetFindAllDevices(bFindAll);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_OPEN:
        {
            uint64_t deviceNum = StdRenderReplayCursor_GetU64(pCursor);
            int32_t expected   = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed || deviceNum > (uint64_t)SIZE_MAX )
            {
                return false;
            }

            result = std3D_Open((size_t)deviceNum);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_CLOSE:
            std3D_Close();
            return true;

        case STDRENDERCAPTURE_ID_STD3D_GETTEXTUREFORMAT:
        {
            int32_t type = StdRenderReplayCursor_GetI32(pCursor);
            ColorInfo colorInfo;
            int bColorKeySet = 0;
            LPDDCOLORKEY pColorKey = NULL;

            if ( pCursor->bFailed )
            {
                return false;
            }

            STD_ZEROMEM(&colorInfo, sizeof(colorInfo));
            std3D_GetTextureFormat((StdColorFormatType)type, &colorInfo, &bColorKeySet, &pColorKey);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_GETCOLORFORMAT:
        {
            ColorInfo colorInfo;
            bool bHasColorInfo;
            int32_t expected;
            StdColorFormatType result;

            if ( !StdRenderReplay_ReadOptColorInfo(pCursor, &colorInfo, &bHasColorInfo) )
            {
                return false;
            }

            expected = StdRenderReplayCursor_GetI32(pCursor);
            if ( pCursor->bFailed )
            {
                return false;
            }

            result = std3D_GetColorFormat(bHasColorInfo ? &colorInfo : NULL);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, (int32_t)result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_GETNUMTEXTUREFORMATS:
        {
            uint64_t expected = StdRenderReplayCursor_GetU64(pCursor);
            size_t result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = std3D_GetNumTextureFormats();
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, (uint64_t)result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_STARTSCENE:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = std3D_StartScene();
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_ENDSCENE:
            std3D_EndScene();
            return true;

        case STDRENDERCAPTURE_ID_STD3D_DRAWRENDERLIST:
        {
            uint32_t texHandle = StdRenderReplayCursor_GetU32(pCursor);
            int32_t rdflags    = StdRenderReplayCursor_GetI32(pCursor);
            StdVertexTL* pVertices;
            size_t numVertices;
            uint16_t* pIndices;
            size_t numIndices;
            tSysTexture* pTexture = NULL;

            if ( pCursor->bFailed
                || !StdRenderReplay_ReadVertices(pReplay, pCursor, &pVertices, &numVertices)
                || !StdRenderReplay_ReadIndices(pReplay, pCursor, &pIndices, &numIndices) )
            {
                return false;
            }

            if ( texHandle == STDRENDERREPLAY_UNKNOWNHANDLE )
            {
                if ( !pReplay->bWarnedUnknownTexture )
                {
                    pReplay->bWarnedUnknownTexture = true;
                    STDLOG_WARNING("Render replay: an unknown cached texture is passed as NULL.\n");
                }
            }
            else if ( texHandle != 0u )
            {
                tSystemTexture* pSysTexture = StdRenderReplay_EnsureSysTexture(pReplay, texHandle);
                if ( !pSysTexture )
                {
                    return false;
                }

                pTexture = pSysTexture->pCachedTexture;
            }

            std3D_DrawRenderList(
                pTexture,
                (Std3DRenderState)rdflags,
                pVertices,
                numVertices,
                pIndices,
                numIndices
            );
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_DRAWLINESTRIP:
        {
            StdVertexTL* pVertices;
            size_t numVertices;

            if ( !StdRenderReplay_ReadVertices(pReplay, pCursor, &pVertices, &numVertices) )
            {
                return false;
            }

            std3D_DrawLineStrip(pVertices, numVertices);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_DRAWPOINTLIST:
        {
            StdVertexTL* pVertices;
            size_t numVertices;

            if ( !StdRenderReplay_ReadVertices(pReplay, pCursor, &pVertices, &numVertices) )
            {
                return false;
            }

            std3D_DrawPointList(pVertices, numVertices);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_SETRENDERSTATE:
        {
            int32_t rdflags = StdRenderReplayCursor_GetI32(pCursor);

            if ( pCursor->bFailed )
            {
                return false;
            }

            std3D_SetRenderState((Std3DRenderState)rdflags);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_SETWIREFRAMERENDERSTATE:
            std3D_SetWireframeRenderState();
            return true;

        case STDRENDERCAPTURE_ID_STD3D_ALLOCSYSTEMTEXTURE:
        {
            uint32_t textureHandle = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t numMipmaps    = StdRenderReplayCursor_GetU32(pCursor);
            tSystemTexture* pTexture;
            uint32_t i;
            int32_t formatType;

            if ( pCursor->bFailed || textureHandle == 0u )
            {
                return false;
            }

            if ( (uint64_t)numMipmaps * 8u + 4u > (uint64_t)(pCursor->size - pCursor->pos) )
            {
                return false;
            }

            if ( !StdRenderReplay_ReserveMipmaps(pReplay, numMipmaps) )
            {
                return false;
            }

            for ( i = 0; i < numMipmaps; ++i )
            {
                uint32_t vbufferHandle = StdRenderReplayCursor_GetU32(pCursor);
                uint32_t blobId        = StdRenderReplayCursor_GetU32(pCursor);
                tVBuffer* pVBuffer;

                if ( pCursor->bFailed )
                {
                    return false;
                }

                pVBuffer = vbufferHandle ? StdRenderReplay_FindVBuffer(pReplay, vbufferHandle) : NULL;
                if ( vbufferHandle != 0u && !pVBuffer )
                {
                    return false;
                }

                if ( !StdRenderReplay_RestoreVBufferBlob(pReplay, pVBuffer, blobId) )
                {
                    return false;
                }

                pReplay->ppMipmaps[i] = pVBuffer;
            }

            formatType = StdRenderReplayCursor_GetI32(pCursor);
            if ( pCursor->bFailed )
            {
                return false;
            }

            pTexture = StdRenderReplay_EnsureSysTexture(pReplay, textureHandle);
            if ( !pTexture )
            {
                return false;
            }

            std3D_AllocSystemTexture(
                pTexture,
                numMipmaps ? pReplay->ppMipmaps : NULL,
                (size_t)numMipmaps,
                (StdColorFormatType)formatType
            );
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_GETVALIDDIMENSIONS:
        {
            uint32_t width          = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t height         = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t expectedWidth  = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t expectedHeight = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t outWidth       = 0u;
            uint32_t outHeight      = 0u;

            if ( pCursor->bFailed )
            {
                return false;
            }

            std3D_GetValidDimensions(width, height, &outWidth, &outHeight);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "outWidth", expectedWidth, outWidth);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "outHeight", expectedHeight, outHeight);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_CLEARSYSTEMTEXTURE:
        {
            uint32_t textureHandle = StdRenderReplayCursor_GetU32(pCursor);
            tSystemTexture* pTexture;

            if ( pCursor->bFailed || textureHandle == 0u )
            {
                return false;
            }

            pTexture = StdRenderReplay_EnsureSysTexture(pReplay, textureHandle);
            if ( !pTexture )
            {
                return false;
            }

            std3D_ClearSystemTexture(pTexture);
            STDFREE(pTexture);
            StdRenderReplay_RemoveSysTexture(pReplay, textureHandle);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_ADDTOTEXTURECACHE:
        {
            uint32_t textureHandle = StdRenderReplayCursor_GetU32(pCursor);
            int32_t format         = StdRenderReplayCursor_GetI32(pCursor);
            tSystemTexture* pTexture;

            if ( pCursor->bFailed || textureHandle == 0u )
            {
                return false;
            }

            pTexture = StdRenderReplay_EnsureSysTexture(pReplay, textureHandle);
            if ( !pTexture )
            {
                return false;
            }

            std3D_AddToTextureCache(pTexture, (StdColorFormatType)format);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_RESETTEXTURECACHE:
            std3D_ResetTextureCache();
            return true;

        case STDRENDERCAPTURE_ID_STD3D_GETMIPMAPCOUNT:
        {
            uint32_t textureHandle = StdRenderReplayCursor_GetU32(pCursor);
            uint64_t expected      = StdRenderReplayCursor_GetU64(pCursor);
            tSystemTexture* pTexture;
            size_t result;

            if ( pCursor->bFailed || textureHandle == 0u )
            {
                return false;
            }

            pTexture = StdRenderReplay_EnsureSysTexture(pReplay, textureHandle);
            if ( !pTexture )
            {
                return false;
            }

            result = std3D_GetMipMapCount(pTexture);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, (uint64_t)result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_UPDATEFRAMECOUNT:
        {
            uint32_t textureHandle = StdRenderReplayCursor_GetU32(pCursor);
            tSystemTexture* pTexture;

            if ( pCursor->bFailed || textureHandle == 0u )
            {
                return false;
            }

            pTexture = StdRenderReplay_EnsureSysTexture(pReplay, textureHandle);
            if ( !pTexture )
            {
                return false;
            }

            std3D_UpdateFrameCount(pTexture);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_FINDCLOSESTFORMAT:
        {
            ColorInfo colorInfo;
            bool bHasColorInfo;
            uint64_t expected;
            size_t result;

            if ( !StdRenderReplay_ReadOptColorInfo(pCursor, &colorInfo, &bHasColorInfo) )
            {
                return false;
            }

            expected = StdRenderReplayCursor_GetU64(pCursor);
            if ( pCursor->bFailed )
            {
                return false;
            }

            result = std3D_FindClosestFormat(bHasColorInfo ? &colorInfo : NULL);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, (uint64_t)result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_SETMIPMAPFILTER:
        {
            int32_t mode     = StdRenderReplayCursor_GetI32(pCursor);
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = std3D_SetMipmapFilter((Std3DMipmapFilterType)mode);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_SETPROJECTION:
        {
            float fov          = StdRenderReplayCursor_GetF32(pCursor);
            float nearPlane    = StdRenderReplayCursor_GetF32(pCursor);
            float farPlane     = StdRenderReplayCursor_GetF32(pCursor);
            int32_t expected   = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = std3D_SetProjection(fov, nearPlane, farPlane);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_ENABLEFOG:
        {
            int32_t bEnabled = StdRenderReplayCursor_GetI32(pCursor);
            float density    = StdRenderReplayCursor_GetF32(pCursor);

            if ( pCursor->bFailed )
            {
                return false;
            }

            std3D_EnableFog(bEnabled, density);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_SETFOG:
        {
            float red        = StdRenderReplayCursor_GetF32(pCursor);
            float green      = StdRenderReplayCursor_GetF32(pCursor);
            float blue       = StdRenderReplayCursor_GetF32(pCursor);
            float startDepth = StdRenderReplayCursor_GetF32(pCursor);
            float endDepth   = StdRenderReplayCursor_GetF32(pCursor);

            if ( pCursor->bFailed )
            {
                return false;
            }

            std3D_SetFog(red, green, blue, startDepth, endDepth);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_CLEARZBUFFER:
            std3D_ClearZBuffer();
            return true;

        case STDRENDERCAPTURE_ID_STD3D_BUILDDISPLAYENVIRONMENT:
        {
            uint8_t expected = StdRenderReplayCursor_GetU8(pCursor);
            StdDisplayEnvironment* pResult;

            if ( pCursor->bFailed )
            {
                return false;
            }

            pResult = std3D_BuildDisplayEnvironment();
            pReplay->pDisplayEnvironment = pResult;
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, pResult != NULL);
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_FREEDISPLAYENVIRONMENT:
        {
            uint8_t expected = StdRenderReplayCursor_GetU8(pCursor);
            bool bHasEnv     = expected != 0u;

            if ( pCursor->bFailed )
            {
                return false;
            }

            if ( bHasEnv && !pReplay->pDisplayEnvironment )
            {
                return false;
            }

            std3D_FreeDisplayEnvironment(bHasEnv ? pReplay->pDisplayEnvironment : NULL);
            pReplay->pDisplayEnvironment = NULL;
            return true;
        }

        case STDRENDERCAPTURE_ID_STD3D_RESETGLOBALS:
            std3D_ResetGlobals();
            return true;

        case STDRENDERCAPTURE_ID_STDDISPLAY_STARTUP:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_Startup();
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_SHUTDOWN:
            stdDisplay_Shutdown();
            return true;

        case STDRENDERCAPTURE_ID_STDDISPLAY_OPEN:
        {
            uint64_t deviceNum = StdRenderReplayCursor_GetU64(pCursor);
            int32_t expected   = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed || deviceNum > (uint64_t)SIZE_MAX )
            {
                return false;
            }

            result = stdDisplay_Open((size_t)deviceNum);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_ISOPEN:
        {
            uint8_t expected = StdRenderReplayCursor_GetU8(pCursor);
            bool result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_IsOpen();
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_CLOSE:
            stdDisplay_Close();
            return true;

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETADAPTERNUM:
        {
            uint64_t deviceNum = StdRenderReplayCursor_GetU64(pCursor);
            uint64_t expected  = StdRenderReplayCursor_GetU64(pCursor);
            size_t result;

            if ( pCursor->bFailed || deviceNum > (uint64_t)SIZE_MAX )
            {
                return false;
            }

            result = stdDisplay_GetAdapterNum((size_t)deviceNum);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, (uint64_t)result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETSYSTEMDEVICE:
        {
            uint8_t expected = StdRenderReplayCursor_GetU8(pCursor);
            tSysDisplayDevice* pResult;

            if ( pCursor->bFailed )
            {
                return false;
            }

            pResult = stdDisplay_GetSystemDevice();
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, pResult != NULL);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_CREATEZBUFFER:
        {
            uint8_t present     = StdRenderReplayCursor_GetU8(pCursor);
            int32_t bSystemMemory = StdRenderReplayCursor_GetI32(pCursor);
            int32_t expected    = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_CreateZBuffer(
                present ? &stdRenderReplay_zeroPixelFormat : NULL,
                bSystemMemory
            );
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_DISABLEVSYNC:
        {
            uint8_t bDisable = StdRenderReplayCursor_GetU8(pCursor);

            if ( pCursor->bFailed )
            {
                return false;
            }

            stdDisplay_DisableVSync(bDisable != 0u);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_UPDATE:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_Update();
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_REFRESH:
        {
            int32_t bReload = StdRenderReplayCursor_GetI32(pCursor);

            if ( pCursor->bFailed )
            {
                return false;
            }

            stdDisplay_Refresh(bReload);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETNUMDEVICES:
        {
            uint64_t expected = StdRenderReplayCursor_GetU64(pCursor);
            size_t result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_GetNumDevices();
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, (uint64_t)result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETDEVICE:
        {
            uint64_t deviceNum = StdRenderReplayCursor_GetU64(pCursor);
            uint8_t present    = StdRenderReplayCursor_GetU8(pCursor);
            int32_t expected   = StdRenderReplayCursor_GetI32(pCursor);
            StdDisplayDevice device;
            int result;

            if ( pCursor->bFailed || deviceNum > (uint64_t)SIZE_MAX )
            {
                return false;
            }

            STD_ZEROMEM(&device, sizeof(device));
            result = stdDisplay_GetDevice((size_t)deviceNum, present ? &device : NULL);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETCURRENTDEVICE:
        {
            uint8_t present  = StdRenderReplayCursor_GetU8(pCursor);
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            StdDisplayDevice device;
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            STD_ZEROMEM(&device, sizeof(device));
            result = stdDisplay_GetCurrentDevice(present ? &device : NULL);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETALLDEVICES:
        {
            uint8_t expected = StdRenderReplayCursor_GetU8(pCursor);
            const StdDisplayDevice* pResult;

            if ( pCursor->bFailed )
            {
                return false;
            }

            pResult = stdDisplay_GetAllDevices();
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, pResult != NULL);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_SETMODE:
        {
            uint64_t modeNum       = StdRenderReplayCursor_GetU64(pCursor);
            int32_t bFullscreen    = StdRenderReplayCursor_GetI32(pCursor);
            uint64_t numBackBuffers = StdRenderReplayCursor_GetU64(pCursor);
            int32_t expected       = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed || modeNum > (uint64_t)SIZE_MAX || numBackBuffers > (uint64_t)SIZE_MAX )
            {
                return false;
            }

            result = stdDisplay_SetMode((size_t)modeNum, bFullscreen, (size_t)numBackBuffers);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_CLEARMODE:
            stdDisplay_ClearMode();
            return true;

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETNUMVIDEOMODES:
        {
            uint64_t expected = StdRenderReplayCursor_GetU64(pCursor);
            size_t result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_GetNumVideoModes();
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, (uint64_t)result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETVIDEOMODE:
        {
            uint64_t modeNum = StdRenderReplayCursor_GetU64(pCursor);
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            StdVideoMode videoMode;
            bool bHasVideoMode;
            int result;

            if ( pCursor->bFailed || modeNum > (uint64_t)SIZE_MAX )
            {
                return false;
            }

            STD_ZEROMEM(&videoMode, sizeof(videoMode));
            if ( !StdRenderReplay_ReadOptVideoMode(pCursor, &videoMode, &bHasVideoMode) )
            {
                return false;
            }

            result = stdDisplay_GetVideoMode((size_t)modeNum, bHasVideoMode ? &videoMode : NULL);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETCURRENTVIDEOMODE:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            StdVideoMode videoMode;
            bool bHasVideoMode;
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            STD_ZEROMEM(&videoMode, sizeof(videoMode));
            if ( !StdRenderReplay_ReadOptVideoMode(pCursor, &videoMode, &bHasVideoMode) )
            {
                return false;
            }

            result = stdDisplay_GetCurrentVideoMode(bHasVideoMode ? &videoMode : NULL);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERNEW:
        {
            tRasterInfo rasterInfo;
            bool bHasRasterInfo;
            int32_t bUseVSurface;
            int32_t bUseVideoMemory;
            uint32_t handle;
            tVBuffer* pResult;
            size_t allocationSize;

            if ( !StdRenderReplay_ReadOptRasterInfo(pCursor, &rasterInfo, &bHasRasterInfo) )
            {
                return false;
            }

            bUseVSurface    = StdRenderReplayCursor_GetI32(pCursor);
            bUseVideoMemory = StdRenderReplayCursor_GetI32(pCursor);
            handle          = StdRenderReplayCursor_GetU32(pCursor);
            if ( pCursor->bFailed )
            {
                return false;
            }

            pResult = stdDisplay_VBufferNew(bHasRasterInfo ? &rasterInfo : NULL, bUseVSurface, bUseVideoMemory);
            if ( handle == 0u )
            {
                if ( pResult )
                {
                    StdRenderReplay_MismatchU(pReplay, recordIndex, id, "handle", 0u, 1u);
                }
            }
            else if ( !pResult )
            {
                StdRenderReplay_MismatchU(pReplay, recordIndex, id, "handle", handle, 0u);
            }
            else
            {
                if ( !StdRenderReplay_ComputeAllocationSize(pResult, &allocationSize)
                    || !StdRenderReplay_DefineVBuffer(pReplay, handle, pResult, allocationSize) )
                {
                    return false;
                }
            }

            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERFREE:
        {
            uint32_t handle = StdRenderReplayCursor_GetU32(pCursor);
            tVBuffer* pVBuffer;

            if ( pCursor->bFailed || handle == 0u )
            {
                return false;
            }

            pVBuffer = StdRenderReplay_FindVBuffer(pReplay, handle);
            if ( !pVBuffer )
            {
                return false;
            }

            stdDisplay_VBufferFree(pVBuffer);
            StdRenderReplay_RemoveVBuffer(pReplay, handle);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERLOCK:
        {
            uint32_t handle = StdRenderReplayCursor_GetU32(pCursor);
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            tVBuffer* pVBuffer;
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            pVBuffer = handle ? StdRenderReplay_FindVBuffer(pReplay, handle) : NULL;
            if ( handle != 0u && !pVBuffer )
            {
                return false;
            }

            result = stdDisplay_VBufferLock(pVBuffer);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERLOCKREADONLY:
        {
            uint32_t handle = StdRenderReplayCursor_GetU32(pCursor);
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            tVBuffer* pVBuffer;
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            pVBuffer = handle ? StdRenderReplay_FindVBuffer(pReplay, handle) : NULL;
            if ( handle != 0u && !pVBuffer )
            {
                return false;
            }

            result = stdDisplay_VBufferLockReadOnly(pVBuffer);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERUNLOCK:
        {
            uint32_t handle = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t blobId = StdRenderReplayCursor_GetU32(pCursor);
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            tVBuffer* pVBuffer;
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            pVBuffer = handle ? StdRenderReplay_FindVBuffer(pReplay, handle) : NULL;
            if ( handle != 0u && !pVBuffer )
            {
                return false;
            }

            if ( !StdRenderReplay_RestoreVBufferBlob(pReplay, pVBuffer, blobId) )
            {
                return false;
            }

            result = stdDisplay_VBufferUnlock(pVBuffer);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERFILL:
        {
            uint32_t handle   = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t fillColor = StdRenderReplayCursor_GetU32(pCursor);
            StdRect rect;
            bool bHasRect;
            int32_t expected;
            tVBuffer* pVBuffer;
            int result;

            if ( pCursor->bFailed || !StdRenderReplay_ReadOptRect(pCursor, &rect, &bHasRect) )
            {
                return false;
            }

            expected = StdRenderReplayCursor_GetI32(pCursor);
            if ( pCursor->bFailed )
            {
                return false;
            }

            pVBuffer = handle ? StdRenderReplay_FindVBuffer(pReplay, handle) : NULL;
            if ( handle != 0u && !pVBuffer )
            {
                return false;
            }

            result = stdDisplay_VBufferFill(pVBuffer, fillColor, bHasRect ? &rect : NULL);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_VBUFFERCONVERTCOLORFORMAT:
        {
            ColorInfo colorInfo;
            bool bHasColorInfo;
            uint32_t srcHandle;
            uint32_t srcBlobId;
            int32_t bColorKey;
            DDCOLORKEY colorKey;
            bool bHasColorKey;
            uint32_t resultHandle;
            tVBuffer* pSrc;
            tVBuffer* pResult;
            size_t allocationSize;

            if ( !StdRenderReplay_ReadOptColorInfo(pCursor, &colorInfo, &bHasColorInfo) )
            {
                return false;
            }

            srcHandle = StdRenderReplayCursor_GetU32(pCursor);
            srcBlobId = StdRenderReplayCursor_GetU32(pCursor);
            bColorKey = StdRenderReplayCursor_GetI32(pCursor);
            if ( pCursor->bFailed || !StdRenderReplay_ReadOptColorKey(pCursor, &colorKey, &bHasColorKey) )
            {
                return false;
            }

            resultHandle = StdRenderReplayCursor_GetU32(pCursor);
            if ( pCursor->bFailed )
            {
                return false;
            }

            pSrc = srcHandle ? StdRenderReplay_FindVBuffer(pReplay, srcHandle) : NULL;
            if ( srcHandle != 0u && !pSrc )
            {
                return false;
            }

            if ( !StdRenderReplay_RestoreVBufferBlob(pReplay, pSrc, srcBlobId) )
            {
                return false;
            }

            pResult = stdDisplay_VBufferConvertColorFormat(
                bHasColorInfo ? &colorInfo : NULL,
                pSrc,
                bColorKey,
                bHasColorKey ? &colorKey : NULL
            );

            // Fixed: A different non-NULL result means the backend already freed pSrc; drop its
            // handle without freeing it again so cleanup and later calls stay valid.
            if ( ( resultHandle != srcHandle ) != ( pResult != pSrc ) )
            {
                STDLOG_ERROR(
                    "Render replay: ConvertColorFormat handle/pointer mismatch at record %llu (id %u).\n",
                    (unsigned long long)recordIndex,
                    (unsigned)id
                );
                return false;
            }

            if ( pResult && pResult != pSrc && srcHandle != 0u )
            {
                StdRenderReplay_RemoveVBuffer(pReplay, srcHandle);
            }

            if ( resultHandle == 0u )
            {
                if ( pResult )
                {
                    StdRenderReplay_MismatchU(pReplay, recordIndex, id, "handle", 0u, 1u);
                }
            }
            else if ( !pResult )
            {
                StdRenderReplay_MismatchU(pReplay, recordIndex, id, "handle", resultHandle, 0u);
            }
            else
            {
                if ( !StdRenderReplay_ComputeAllocationSize(pResult, &allocationSize)
                    || !StdRenderReplay_DefineVBuffer(pReplay, resultHandle, pResult, allocationSize) )
                {
                    return false;
                }
            }

            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETTEXTUREMEMORY:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            size_t total = 0u;
            size_t freeMemory = 0u;
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_GetTextureMemory(&total, &freeMemory);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETTOTALMEMORY:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            size_t total = 0u;
            size_t freeMemory = 0u;
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_GetTotalMemory(&total, &freeMemory);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_GETBACKBUFFERSIZE:
        {
            uint32_t expectedWidth  = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t expectedHeight = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t width          = 0u;
            uint32_t height         = 0u;

            if ( pCursor->bFailed )
            {
                return false;
            }

            stdDisplay_GetBackBufferSize(&width, &height);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "width", expectedWidth, width);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "height", expectedHeight, height);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_BACKBUFFERFILL:
        {
            uint32_t color = StdRenderReplayCursor_GetU32(pCursor);
            StdRect rect;
            bool bHasRect;
            int32_t expected;
            int result;

            if ( pCursor->bFailed || !StdRenderReplay_ReadOptRect(pCursor, &rect, &bHasRect) )
            {
                return false;
            }

            expected = StdRenderReplayCursor_GetI32(pCursor);
            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_BackBufferFill(color, bHasRect ? &rect : NULL);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_SETDEFAULTRESOLUTION:
        {
            uint32_t width  = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t height = StdRenderReplayCursor_GetU32(pCursor);

            if ( pCursor->bFailed )
            {
                return false;
            }

            stdDisplay_SetDefaultResolution(width, height);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_FLIPTOGDISURFACE:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_FlipToGDISurface();
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_SETBUFFERCLIPPER:
        {
            int32_t bFrontBuffer = StdRenderReplayCursor_GetI32(pCursor);
            int32_t expected     = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_SetBufferClipper(bFrontBuffer);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_REMOVEBUFFERCLIPPER:
        {
            int32_t bFrontBuffer = StdRenderReplayCursor_GetI32(pCursor);
            int32_t expected     = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_RemoveBufferClipper(bFrontBuffer);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_CANRENDERWINDOWED:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_CanRenderWindowed();
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_ISFULLSCREEN:
        {
            int32_t expected = StdRenderReplayCursor_GetI32(pCursor);
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_IsFullscreen();
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_LOCKBACKBUFFER:
        case STDRENDERCAPTURE_ID_STDDISPLAY_LOCKBACKBUFFERREADONLY:
        {
            int32_t expectedResult = StdRenderReplayCursor_GetI32(pCursor);
            uint32_t expectedWidth = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t expectedHeight = StdRenderReplayCursor_GetU32(pCursor);
            int32_t expectedPitch  = StdRenderReplayCursor_GetI32(pCursor);
            void* pSurface         = NULL;
            uint32_t width         = 0u;
            uint32_t height        = 0u;
            int32_t pitch          = 0;
            int result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = id == STDRENDERCAPTURE_ID_STDDISPLAY_LOCKBACKBUFFER
                ? stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch)
                : stdDisplay_LockBackBufferReadOnly(&pSurface, &width, &height, &pitch);

            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "result", expectedResult, result);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "width", expectedWidth, width);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "height", expectedHeight, height);
            StdRenderReplay_MismatchI(pReplay, recordIndex, id, "pitch", expectedPitch, pitch);

            pReplay->pBackBufferPixels = ( result == 0 && pSurface ) ? pSurface : NULL;
            pReplay->backBufferHeight  = pReplay->pBackBufferPixels ? height : 0u;
            pReplay->backBufferPitch   = pReplay->pBackBufferPixels ? pitch : 0;
            pReplay->bHasBackBufferLock = pReplay->pBackBufferPixels != NULL;
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_UNLOCKBACKBUFFER:
        {
            uint32_t blobId = StdRenderReplayCursor_GetU32(pCursor);

            if ( pCursor->bFailed )
            {
                return false;
            }

            if ( blobId != 0u )
            {
                uint64_t size;

                if ( !pReplay->bHasBackBufferLock || !pReplay->pBackBufferPixels
                    || pReplay->backBufferPitch <= 0 )
                {
                    return false;
                }

                size = (uint64_t)pReplay->backBufferHeight * (uint64_t)pReplay->backBufferPitch;
                if ( size > (uint64_t)SIZE_MAX
                    || !StdRenderReplay_RestorePixels(pReplay, (uint8_t*)pReplay->pBackBufferPixels, (size_t)size, blobId) )
                {
                    return false;
                }
            }

            stdDisplay_UnlockBackBuffer();
            pReplay->pBackBufferPixels  = NULL;
            pReplay->backBufferHeight   = 0u;
            pReplay->backBufferPitch    = 0;
            pReplay->bHasBackBufferLock = false;
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_ENCODEFROMRGB565:
        {
            uint16_t pixel   = StdRenderReplayCursor_GetU16(pCursor);
            uint32_t expected = StdRenderReplayCursor_GetU32(pCursor);
            uint32_t result;

            if ( pCursor->bFailed )
            {
                return false;
            }

            result = stdDisplay_EncodeFromRGB565(pixel);
            StdRenderReplay_MismatchU(pReplay, recordIndex, id, "result", expected, result);
            return true;
        }

        case STDRENDERCAPTURE_ID_STDDISPLAY_RESETGLOBALS:
            stdDisplay_ResetGlobals();
            return true;

        default:
            return false;
    }
}

static void StdRenderReplay_FreeResources(StdRenderReplay* pReplay)
{
    size_t i;

    if ( pReplay->pDisplayEnvironment )
    {
        std3D_FreeDisplayEnvironment(pReplay->pDisplayEnvironment);
        pReplay->pDisplayEnvironment = NULL;
    }

    for ( i = 0; i < pReplay->numSysTextures; ++i )
    {
        tSystemTexture* pTexture = pReplay->pSysTextures[i].pTexture;
        if ( pTexture )
        {
            std3D_ClearSystemTexture(pTexture);
            STDFREE(pTexture);
        }
    }

    for ( i = 0; i < pReplay->numVBuffers; ++i )
    {
        tVBuffer* pVBuffer = pReplay->pVBuffers[i].pVBuffer;
        if ( pVBuffer && pVBuffer != &stdDisplay_g_backBuffer )
        {
            stdDisplay_VBufferFree(pVBuffer);
        }
    }

    for ( i = 0; i < pReplay->numBlobs; ++i )
    {
        if ( pReplay->pBlobs[i].pData )
        {
            STDFREE(pReplay->pBlobs[i].pData);
        }
    }

    if ( pReplay->pBlobs )
    {
        STDFREE(pReplay->pBlobs);
    }

    if ( pReplay->pVBuffers )
    {
        STDFREE(pReplay->pVBuffers);
    }

    if ( pReplay->pSysTextures )
    {
        STDFREE(pReplay->pSysTextures);
    }

    if ( pReplay->pVertices )
    {
        STDFREE(pReplay->pVertices);
    }

    if ( pReplay->pIndices )
    {
        STDFREE(pReplay->pIndices);
    }

    if ( pReplay->ppMipmaps )
    {
        STDFREE(pReplay->ppMipmaps);
    }
}

int stdRenderReplay_Run(const char* pInPath, const char* pOutPath)
{
    StdRenderReplay replay;
    uint8_t aHeader[16];
    char aColumn[STDRENDERREPLAY_MAXCOLUMNLEN + 1u];
    uint32_t columnLength;
    size_t recordIndex = 0u;
    int result = 1;

    if ( !pInPath )
    {
        STDLOG_ERROR("Render replay: no input file.\n");
        return 2;
    }

    if ( !std_g_pHS )
    {
        STDLOG_ERROR("Render replay: the std host services are not set up.\n");
        return 1;
    }

    STD_ZEROMEM(&replay, sizeof(replay));
    replay.reader.pHS = std_g_pHS;

    replay.reader.file = replay.reader.pHS->pFileOpen(pInPath, "rb");
    if ( !replay.reader.file )
    {
        STDLOG_ERROR("Render replay: could not open '%s'.\n", pInPath);
        return 1;
    }

    if ( StdRenderReplayReader_Read(&replay.reader, aHeader, sizeof(aHeader)) != sizeof(aHeader) )
    {
        STDLOG_ERROR("Render replay: '%s' has no complete header.\n", pInPath);
        goto done;
    }

    if ( memcmp(aHeader, stdRenderReplay_aSignature, sizeof(stdRenderReplay_aSignature)) != 0 )
    {
        STDLOG_ERROR("Render replay: '%s' has a bad signature.\n", pInPath);
        goto done;
    }

    {
        uint32_t version = (uint32_t)aHeader[8]
            | (uint32_t)aHeader[9] << 8
            | (uint32_t)aHeader[10] << 16
            | (uint32_t)aHeader[11] << 24;

        if ( version != STDRENDERREPLAY_VERSION )
        {
            STDLOG_ERROR("Render replay: '%s' has version %u, expected %u.\n", pInPath, version, STDRENDERREPLAY_VERSION);
            goto done;
        }
    }

    columnLength = (uint32_t)aHeader[12]
        | (uint32_t)aHeader[13] << 8
        | (uint32_t)aHeader[14] << 16
        | (uint32_t)aHeader[15] << 24;
    if ( columnLength == 0u || columnLength > STDRENDERREPLAY_MAXCOLUMNLEN )
    {
        STDLOG_ERROR("Render replay: '%s' has a column name of %u bytes.\n", pInPath, columnLength);
        goto done;
    }

    if ( StdRenderReplayReader_Read(&replay.reader, aColumn, columnLength) != columnLength )
    {
        STDLOG_ERROR("Render replay: '%s' ends inside the column name.\n", pInPath);
        goto done;
    }

    aColumn[columnLength] = '\0';

    // Added: the output header must start from the default frame state; the input's FRAMES records
    // then reproduce every change exactly where the original wrote it.
    stdRenderCapture_SetFramesEnabled(true);

    if ( pOutPath )
    {
        if ( !stdDisplayCapture_Open(pOutPath, aColumn) )
        {
            STDLOG_ERROR("Render replay: could not open the output capture '%s'.\n", pOutPath);
            goto done;
        }

        replay.bOutputOpen = true;
    }

    for ( ;; )
    {
        uint64_t recordOffset = replay.reader.offset;
        uint8_t aRecord[STDRENDERREPLAY_RECORDHEADERSIZE];
        uint16_t id;
        uint32_t payloadSize;
        size_t numRead;

        numRead = StdRenderReplayReader_Read(&replay.reader, aRecord, sizeof(aRecord));
        if ( numRead == 0u )
        {
            break;
        }

        if ( numRead != sizeof(aRecord) )
        {
            STDLOG_ERROR(
                "Render replay: truncated record header at offset %llu.\n",
                (unsigned long long)recordOffset
            );
            goto done;
        }

        id = (uint16_t)aRecord[0] | (uint16_t)((uint16_t)aRecord[1] << 8);
        payloadSize = (uint32_t)aRecord[4]
            | (uint32_t)aRecord[5] << 8
            | (uint32_t)aRecord[6] << 16
            | (uint32_t)aRecord[7] << 24;

        ++replay.numRecords;

        if ( payloadSize > STDRENDERREPLAY_MAXPAYLOADSIZE )
        {
            StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the payload is too large.");
            goto done;
        }

        if ( id == STDRENDERCAPTURE_RECORD_BLOB )
        {
            uint8_t aPrefix[STDRENDERREPLAY_BLOBPREFIXSIZE];
            uint32_t blobId;
            uint64_t blobSize;
            uint8_t* pBlobData = NULL;

            if ( payloadSize < STDRENDERREPLAY_BLOBPREFIXSIZE )
            {
                StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the BLOB prefix is missing.");
                goto done;
            }

            if ( StdRenderReplayReader_Read(&replay.reader, aPrefix, sizeof(aPrefix)) != sizeof(aPrefix) )
            {
                StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the BLOB prefix is truncated.");
                goto done;
            }

            blobId = (uint32_t)aPrefix[0]
                | (uint32_t)aPrefix[1] << 8
                | (uint32_t)aPrefix[2] << 16
                | (uint32_t)aPrefix[3] << 24;
            blobSize = (uint64_t)aPrefix[4]
                | (uint64_t)aPrefix[5] << 8
                | (uint64_t)aPrefix[6] << 16
                | (uint64_t)aPrefix[7] << 24
                | (uint64_t)aPrefix[8] << 32
                | (uint64_t)aPrefix[9] << 40
                | (uint64_t)aPrefix[10] << 48
                | (uint64_t)aPrefix[11] << 56;

            if ( blobSize > (uint64_t)SIZE_MAX
                || blobSize != (uint64_t)payloadSize - STDRENDERREPLAY_BLOBPREFIXSIZE )
            {
                StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the BLOB size does not match its payload.");
                goto done;
            }

            if ( blobId != (uint32_t)replay.numBlobs + 1u )
            {
                StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the blob ids are not in order.");
                goto done;
            }

            if ( blobSize > 0u )
            {
                pBlobData = (uint8_t*)STDMALLOC((size_t)blobSize);
                if ( !pBlobData )
                {
                    STDLOG_ERROR("Render replay: could not allocate a %llu-byte blob.\n", (unsigned long long)blobSize);
                    goto done;
                }

                if ( StdRenderReplayReader_Read(&replay.reader, pBlobData, (size_t)blobSize) != (size_t)blobSize )
                {
                    STDFREE(pBlobData);
                    StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the BLOB content is truncated.");
                    goto done;
                }
            }

            if ( !StdRenderReplay_AppendBlob(&replay, pBlobData, (size_t)blobSize) )
            {
                if ( pBlobData )
                {
                    STDFREE(pBlobData);
                }

                STDLOG_ERROR("Render replay: could not grow the blob table.\n");
                goto done;
            }

            ++replay.numBlobRecords;
            ++recordIndex;
            continue;
        }

        if ( !StdRenderReplay_IsKnownId(id) )
        {
            STDLOG_WARNING(
                "Render replay: skipping unknown record id %u at record %llu.\n",
                (unsigned)id,
                (unsigned long long)recordIndex
            );
            if ( !StdRenderReplayReader_Skip(&replay.reader, payloadSize) )
            {
                StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the unknown payload is truncated.");
                goto done;
            }

            ++replay.numSkipped;
            ++recordIndex;
            continue;
        }

        if ( StdRenderReplay_IsSkippedId(id) )
        {
            if ( !StdRenderReplayReader_Skip(&replay.reader, payloadSize) )
            {
                StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the skipped payload is truncated.");
                goto done;
            }

            ++replay.numSkipped;
            ++recordIndex;
            continue;
        }

        {
            uint8_t* pPayload = NULL;
            StdRenderReplayCursor cursor;

            if ( payloadSize > 0u )
            {
                pPayload = (uint8_t*)STDMALLOC(payloadSize);
                if ( !pPayload )
                {
                    STDLOG_ERROR("Render replay: could not allocate a %u-byte payload.\n", payloadSize);
                    goto done;
                }

                if ( StdRenderReplayReader_Read(&replay.reader, pPayload, payloadSize) != payloadSize )
                {
                    STDFREE(pPayload);
                    StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the payload is truncated.");
                    goto done;
                }
            }

            cursor.pData    = pPayload;
            cursor.size     = payloadSize;
            cursor.pos      = 0u;
            cursor.bFailed  = false;

            if ( !StdRenderReplay_ProcessRecord(&replay, id, &cursor, recordIndex) )
            {
                if ( pPayload )
                {
                    STDFREE(pPayload);
                }

                StdRenderReplay_LogRecordError(&replay, recordIndex, id, recordOffset, "the payload could not be decoded.");
                goto done;
            }

            if ( cursor.bFailed || cursor.pos != cursor.size )
            {
                if ( pPayload )
                {
                    STDFREE(pPayload);
                }

                StdRenderReplay_LogRecordError(
                    &replay,
                    recordIndex,
                    id,
                    recordOffset,
                    cursor.bFailed ? "the payload ended early." : "the payload has leftover bytes."
                );
                goto done;
            }

            if ( pPayload )
            {
                STDFREE(pPayload);
            }
        }

        ++recordIndex;
    }

    result = 0;

done:
    if ( replay.bOutputOpen )
    {
        stdDisplayCapture_Close();
        replay.bOutputOpen = false;
    }

    if ( replay.reader.file )
    {
        replay.reader.pHS->pFileClose(replay.reader.file);
        replay.reader.file = 0;
    }

    // Added: leftover objects are released only after the output capture is closed, so the cleanup
    // cannot add records the input did not have.
    StdRenderReplay_FreeResources(&replay);

    if ( result == 0 )
    {
        printf(
            "replayed %llu records (%llu blobs, %llu skipped, %llu mismatches)\n",
            (unsigned long long)replay.numRecords,
            (unsigned long long)replay.numBlobRecords,
            (unsigned long long)replay.numSkipped,
            (unsigned long long)replay.numMismatches
        );
    }

    return result;
}
