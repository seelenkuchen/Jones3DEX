// Added: the Babylon column's image output for the render capture replay
// (stdRenderReplayBabylon.h). Adapter code: no Fixed/Added markers inside.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <std/General/std.h>

#include "stdRenderBabylon.h"
#include "stdRenderReplayBabylon.h"


namespace
{
    void StdRenderReplayBabylon_PutU16(std::uint8_t* pData, std::uint16_t value)
    {
        pData[0] = static_cast<std::uint8_t>(value);
        pData[1] = static_cast<std::uint8_t>(value >> 8);
    }

    void StdRenderReplayBabylon_PutU32(std::uint8_t* pData, std::uint32_t value)
    {
        pData[0] = static_cast<std::uint8_t>(value);
        pData[1] = static_cast<std::uint8_t>(value >> 8);
        pData[2] = static_cast<std::uint8_t>(value >> 16);
        pData[3] = static_cast<std::uint8_t>(value >> 24);
    }

    bool StdRenderReplayBabylon_ParsePositive(const char* pText, std::uint32_t* pValue)
    {
        if ( !pText || !*pText )
        {
            return false;
        }

        std::uint64_t value = 0;
        for ( const char* p = pText; *p; ++p )
        {
            if ( *p < '0' || *p > '9' )
            {
                return false;
            }

            value = value * 10u + static_cast<std::uint64_t>(*p - '0');
            if ( value > UINT32_MAX )
            {
                return false;
            }
        }

        if ( value == 0u )
        {
            return false;
        }

        *pValue = static_cast<std::uint32_t>(value);
        return true;
    }

    std::string StdRenderReplayBabylon_FramePath(const char* pDirectory, std::size_t frameIndex)
    {
        char aName[32];
        std::snprintf(aName, sizeof(aName), "frame_%05zu.bmp", frameIndex);

        std::string path = pDirectory;
        if ( !path.empty() && path.back() != '/' && path.back() != '\\' )
        {
            path += '/';
        }

        path += aName;
        return path;
    }

    bool StdRenderReplayBabylon_WriteBmp(const char* pDirectory, std::size_t frameIndex)
    {
        const std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
        const std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
        if ( width == 0u || height == 0u || width > INT32_MAX || height > INT32_MAX )
        {
            STDLOG_ERROR("Render replay: no %ux%u back buffer to write.\n", width, height);
            return false;
        }

        const std::uint64_t pixelBytes = static_cast<std::uint64_t>(width) * height * 4u;
        if ( pixelBytes > SIZE_MAX )
        {
            STDLOG_ERROR("Render replay: the %ux%u back buffer is too large.\n", width, height);
            return false;
        }

        std::vector<std::byte> pixels(static_cast<std::size_t>(pixelBytes));
        if ( !stdRenderBabylon_ReadBackBuffer(pixels.data(), nullptr, nullptr) )
        {
            return false;
        }

        const std::uint64_t rowBytes   = (static_cast<std::uint64_t>(width) * 3u + 3u) & ~std::uint64_t(3u);
        const std::uint64_t imageBytes = rowBytes * height;
        const std::uint64_t fileBytes  = 54u + imageBytes;
        if ( imageBytes > UINT32_MAX || fileBytes > UINT32_MAX )
        {
            STDLOG_ERROR("Render replay: a %ux%u BMP is too large.\n", width, height);
            return false;
        }

        std::uint8_t aHeader[54] = {};
        aHeader[0] = 'B';
        aHeader[1] = 'M';
        StdRenderReplayBabylon_PutU32(&aHeader[2], static_cast<std::uint32_t>(fileBytes));
        StdRenderReplayBabylon_PutU32(&aHeader[10], 54u);
        StdRenderReplayBabylon_PutU32(&aHeader[14], 40u);
        StdRenderReplayBabylon_PutU32(&aHeader[18], width);
        StdRenderReplayBabylon_PutU32(&aHeader[22], height);
        StdRenderReplayBabylon_PutU16(&aHeader[26], 1u);
        StdRenderReplayBabylon_PutU16(&aHeader[28], 24u);
        StdRenderReplayBabylon_PutU32(&aHeader[34], static_cast<std::uint32_t>(imageBytes));

        const std::string path = StdRenderReplayBabylon_FramePath(pDirectory, frameIndex);
        // std::ofstream: an image output directory is an ordinary host path (no engine fopen shim), and
        // MSVC's /sdl rejects fopen.
        std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if ( !file )
        {
            STDLOG_ERROR("Render replay: could not open '%s'.\n", path.c_str());
            return false;
        }

        file.write(reinterpret_cast<const char*>(aHeader), sizeof(aHeader));
        bool bOk = static_cast<bool>(file);
        std::vector<std::uint8_t> row(static_cast<std::size_t>(rowBytes));
        for ( std::uint32_t y = 0; bOk && y < height; ++y )
        {
            const std::uint8_t* pSrc = reinterpret_cast<const std::uint8_t*>(pixels.data())
                + static_cast<std::size_t>(height - 1u - y) * width * 4u;
            std::memset(row.data(), 0, row.size());
            for ( std::uint32_t x = 0; x < width; ++x )
            {
                row[x * 3u + 0u] = pSrc[x * 4u + 0u];
                row[x * 3u + 1u] = pSrc[x * 4u + 1u];
                row[x * 3u + 2u] = pSrc[x * 4u + 2u];
            }

            file.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
            bOk = static_cast<bool>(file);
        }

        file.close();
        if ( !file )
        {
            bOk = false;
        }

        if ( !bOk )
        {
            STDLOG_ERROR("Render replay: could not write '%s'.\n", path.c_str());
            std::remove(path.c_str());
        }

        return bOk;
    }
}

bool stdRenderReplayBabylon_TakeFrameOptions(
    int* pArgc,
    char** argv,
    bool bRenderGpu,
    StdRenderReplayBabylonFrameOutput* pOutput,
    const char** ppError
)
{
    if ( ppError )
    {
        *ppError = nullptr;
    }

    if ( !pArgc || !argv || !pOutput )
    {
        if ( ppError )
        {
            *ppError = "Jones3D-replay: the frame options have no argument list";
        }
        return false;
    }

    StdRenderReplayBabylonFrameOutput output;
    bool bHasEvery = false;
    for ( int i = 1; i < *pArgc; ++i )
    {
        if ( std::strcmp(argv[i], "--frames-out") == 0 )
        {
            if ( i + 1 >= *pArgc )
            {
                if ( ppError )
                {
                    *ppError = "Jones3D-replay: --frames-out needs a directory";
                }
                return false;
            }

            const char* pDirectory = argv[++i];
            if ( !pDirectory[0] )
            {
                if ( ppError )
                {
                    *ppError = "Jones3D-replay: --frames-out needs a directory";
                }
                return false;
            }

            output.pDirectory = pDirectory;
        }
        else if ( std::strcmp(argv[i], "--every") == 0 )
        {
            if ( i + 1 >= *pArgc )
            {
                if ( ppError )
                {
                    *ppError = "Jones3D-replay: --every needs a positive number";
                }
                return false;
            }

            if ( !StdRenderReplayBabylon_ParsePositive(argv[++i], &output.every) )
            {
                if ( ppError )
                {
                    *ppError = "Jones3D-replay: --every needs a positive number";
                }
                return false;
            }

            bHasEvery = true;
        }
    }

    if ( output.pDirectory && !bRenderGpu )
    {
        if ( ppError )
        {
            *ppError = "Jones3D-replay: --frames-out needs --render gpu";
        }
        return false;
    }

    if ( bHasEvery && !output.pDirectory )
    {
        if ( ppError )
        {
            *ppError = "Jones3D-replay: --every needs --frames-out";
        }
        return false;
    }

    int numKept = 1;
    for ( int i = 1; i < *pArgc; ++i )
    {
        if ( std::strcmp(argv[i], "--frames-out") == 0 || std::strcmp(argv[i], "--every") == 0 )
        {
            ++i;
            continue;
        }

        argv[numKept++] = argv[i];
    }

    argv[numKept] = nullptr;
    *pArgc        = numKept;
    *pOutput      = output;
    return true;
}

bool stdRenderReplayBabylon_BeginFrames(StdRenderReplayBabylonFrameOutput* pOutput)
{
    if ( !pOutput || !pOutput->pDirectory )
    {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(pOutput->pDirectory, error);
    if ( error )
    {
        STDLOG_ERROR("Render replay: could not create '%s': %s.\n", pOutput->pDirectory, error.message().c_str());
        return false;
    }

    return true;
}

void stdRenderReplayBabylon_CaptureFrame(std::size_t frameIndex, void* pUserData)
{
    StdRenderReplayBabylonFrameOutput* pOutput = static_cast<StdRenderReplayBabylonFrameOutput*>(pUserData);
    if ( !pOutput || !pOutput->pDirectory )
    {
        return;
    }

    ++pOutput->frames;
    if ( pOutput->every == 0u || frameIndex % pOutput->every != 0u )
    {
        return;
    }

    if ( StdRenderReplayBabylon_WriteBmp(pOutput->pDirectory, frameIndex) )
    {
        ++pOutput->images;
    }
}
