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

    std::string StdRenderReplayBabylon_ImagePath(const char* pDirectory, const char* pPrefix, std::size_t frameIndex)
    {
        char aName[40];
        std::snprintf(aName, sizeof(aName), "%s_%05zu.bmp", pPrefix, frameIndex);

        std::string path = pDirectory;
        if ( !path.empty() && path.back() != '/' && path.back() != '\\' )
        {
            path += '/';
        }

        path += aName;
        return path;
    }

    // The back buffer's size, checked for a BMP and for a width * height * 4 byte buffer.
    bool StdRenderReplayBabylon_ImageSize(std::uint32_t* pWidth, std::uint32_t* pHeight)
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

        *pWidth  = width;
        *pHeight = height;
        return true;
    }

    // Writes width x height Bgra8 pixels, rows top first, as a 24-bit BMP.
    bool StdRenderReplayBabylon_WriteBmpFile(const std::string& path, std::uint32_t width, std::uint32_t height, const std::uint8_t* pPixels)
    {
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
            const std::uint8_t* pSrc = pPixels + static_cast<std::size_t>(height - 1u - y) * width * 4u;
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

    bool StdRenderReplayBabylon_WriteFrame(const char* pDirectory, std::size_t frameIndex)
    {
        std::uint32_t width, height;
        if ( !StdRenderReplayBabylon_ImageSize(&width, &height) )
        {
            return false;
        }

        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
        if ( !stdRenderBabylon_ReadBackBuffer(pixels.data(), nullptr, nullptr) )
        {
            return false;
        }

        return StdRenderReplayBabylon_WriteBmpFile(StdRenderReplayBabylon_ImagePath(pDirectory, "frame", frameIndex), width, height,
            pixels.data());
    }

    // The normal target, read after StdRenderReplayBabylon_WriteFrame flushed the frame, decoded
    // from R10G10B10A2 or Rgba8 to Bgra8.
    bool StdRenderReplayBabylon_WriteNormals(const char* pDirectory, std::size_t frameIndex)
    {
        std::uint32_t width, height;
        if ( !StdRenderReplayBabylon_ImageSize(&width, &height) )
        {
            return false;
        }

        babylon::render::Device* pDevice = stdRenderBabylon_GetDevice();
        const babylon::render::TextureHandle normals = stdRenderBabylon_GetNormalTarget();
        if ( !pDevice || normals.isNull() )
        {
            STDLOG_ERROR("Render replay: no normal target to write.\n");
            return false;
        }

        const std::vector<std::byte> texels = pDevice->readTexture(normals, 0);
        const std::size_t numTexels         = static_cast<std::size_t>(width) * height;
        if ( texels.size() != numTexels * 4u )
        {
            STDLOG_ERROR("Render replay: the device returned %zu bytes for the %ux%u normal target.\n", texels.size(), width, height);
            return false;
        }

        const bool b1010102 = stdRenderBabylon_GetNormalFormat() == babylon::render::TextureFormat::R10G10B10A2;
        std::vector<std::uint8_t> pixels(numTexels * 4u);
        for ( std::size_t i = 0; i < numTexels; ++i )
        {
            const std::uint8_t* pTexel = reinterpret_cast<const std::uint8_t*>(texels.data()) + i * 4u;
            std::uint32_t r, g, b;
            if ( b1010102 )
            {
                const std::uint32_t word = static_cast<std::uint32_t>(pTexel[0]) | (static_cast<std::uint32_t>(pTexel[1]) << 8)
                    | (static_cast<std::uint32_t>(pTexel[2]) << 16) | (static_cast<std::uint32_t>(pTexel[3]) << 24);
                r = ((word & 0x3FFu) * 255u + 511u) / 1023u;
                g = (((word >> 10) & 0x3FFu) * 255u + 511u) / 1023u;
                b = (((word >> 20) & 0x3FFu) * 255u + 511u) / 1023u;
            }
            else
            {
                r = pTexel[0];
                g = pTexel[1];
                b = pTexel[2];
            }

            pixels[i * 4u + 0u] = static_cast<std::uint8_t>(b);
            pixels[i * 4u + 1u] = static_cast<std::uint8_t>(g);
            pixels[i * 4u + 2u] = static_cast<std::uint8_t>(r);
            pixels[i * 4u + 3u] = 255u;
        }

        return StdRenderReplayBabylon_WriteBmpFile(StdRenderReplayBabylon_ImagePath(pDirectory, "normals", frameIndex), width,
            height, pixels.data());
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
        else if ( std::strcmp(argv[i], "--normals") == 0 )
        {
            output.bNormals = true;
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

    if ( output.bNormals && !output.pDirectory )
    {
        if ( ppError )
        {
            *ppError = "Jones3D-replay: --normals needs --frames-out";
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

        if ( std::strcmp(argv[i], "--normals") == 0 )
        {
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

    if ( StdRenderReplayBabylon_WriteFrame(pOutput->pDirectory, frameIndex) )
    {
        ++pOutput->images;
        if ( pOutput->bNormals )
        {
            StdRenderReplayBabylon_WriteNormals(pOutput->pDirectory, frameIndex);
        }
    }
}
