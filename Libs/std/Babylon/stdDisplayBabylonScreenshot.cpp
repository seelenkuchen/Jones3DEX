// The BMP writers and stdDisplay_SaveScreen.
// Part of the Babylon column's display adapter; see stdDisplayBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#include <std/General/stdRenderBackend.h>
#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>

#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdConfig.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>
#include <std/Null/stdPlatformNull.h>

#include <babylon/platform/Window.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/time/FrameLimiter.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <babylon/platform/Keys.h>

#include "stdBabylon.h"
#include "stdBabylonEvents.h"
#include "stdBabylonTour.h"
#include "stdDisplayBabylon.h"
#include "stdRenderBabylon.h"
#include "stdDisplayBabylonInternal.h"

namespace render = babylon::render;
namespace babylontime = babylon::time;

using namespace stdDisplayBabylonInternal;

namespace
{
    // A 24-bit BMP of the back buffer, as std/Win95/stdBmpWin32.c's WriteVBuffer wrote it through
    // the host file services (before 82b8f607): bottom row first, rows padded to 4 bytes, 2834
    // pixels per meter. 0 on success, 1 on failure. The headers are written field by field.
    std::array<uint8_t, 54> stdDisplay_MakeBmpHeader(uint32_t width, uint32_t height,
        uint32_t imageSize)
    {
        std::array<uint8_t, 54> aHeader{};
        const auto putU16 = [&aHeader](size_t at, uint32_t value)
        {
            aHeader[at]      = (uint8_t)value;
            aHeader[at + 1u] = (uint8_t)(value >> 8);
        };
        const auto putU32 = [&aHeader](size_t at, uint32_t value)
        {
            for ( size_t i = 0; i < 4u; ++i )
            {
                aHeader[at + i] = (uint8_t)(value >> (8u * i));
            }
        };
        putU16(0u, 0x4D42u);                            // bfType "BM"
        putU32(2u, 54u + imageSize);                    // bfSize
        putU32(10u, 54u);                               // bfOffBits
        putU32(14u, 40u);                               // biSize
        putU32(18u, width);                             // biWidth
        putU32(22u, height);                            // biHeight, positive: bottom up
        putU16(26u, 1u);                                // biPlanes
        putU16(28u, stdColor_cfRGB888.bpp);             // biBitCount
        putU32(34u, imageSize);                         // biSizeImage (biCompression 0 = BI_RGB)
        putU32(38u, 2834u);                             // biXPelsPerMeter
        putU32(42u, 2834u);                             // biYPelsPerMeter
        return aHeader;
    }

    int stdDisplay_WriteBackBufferBmp(const char* pFilename)
    {
        tVBuffer* pVBuffer = &stdDisplay_g_backBuffer;
        if ( pVBuffer->rasterInfo.colorInfo.colorMode == STDCOLOR_PAL )
        {
            return 1;
        }

        const ColorInfo ciBmp = stdColor_cfRGB888;
        const uint64_t width  = pVBuffer->rasterInfo.width;
        const uint64_t height = pVBuffer->rasterInfo.height;
        if ( !width || !height || width > INT32_MAX || height > INT32_MAX )
        {
            return 1;
        }

        const uint64_t stride    = (width * 3u + 3u) & ~uint64_t(3u);
        const uint64_t imageSize = stride * height;
        if ( imageSize > UINT32_MAX - 54u )
        {
            return 1;
        }

        const std::array<uint8_t, 54> aHeader = stdDisplay_MakeBmpHeader((uint32_t)width,
            (uint32_t)height, (uint32_t)imageSize);

        tFileHandle fh = std_g_pHS->pFileOpen(pFilename, "wb");
        if ( !fh )
        {
            STDLOG_ERROR("Unable to open file '%s' for writing.\n", pFilename);
            return 1;
        }

        if ( std_g_pHS->pFileWrite(fh, aHeader.data(), aHeader.size()) != aHeader.size() )
        {
            STDLOG_ERROR("Error attempting to write %zu bytes to file '%s'.\n", aHeader.size(), pFilename);
            std_g_pHS->pFileClose(fh);
            return 1;
        }

        if ( !stdDisplay_VBufferLockReadOnly(pVBuffer) )
        {
            std_g_pHS->pFileClose(fh);
            return 1;
        }

        std::vector<uint8_t> aRow((size_t)stride, 0u);
        int result = 0;
        for ( uint64_t row = height; row-- > 0u; )
        {
            stdColor_ColorConvertOneRow(aRow.data(), &ciBmp, &pVBuffer->pPixels[pVBuffer->rasterInfo.rowSize * row],
                &pVBuffer->rasterInfo.colorInfo, (int)width, 0, nullptr);
            if ( std_g_pHS->pFileWrite(fh, aRow.data(), aRow.size()) != aRow.size() )
            {
                STDLOG_ERROR("Error attempting to write %zu bytes to file '%s'.\n", aRow.size(), pFilename);
                result = 1;
                break;
            }
        }

        stdDisplay_VBufferUnlock(pVBuffer);
        std_g_pHS->pFileClose(fh);
        return result;
    }
}

namespace stdDisplayBabylonInternal
{
    // A 24-bit BMP of Bgra8 pixels, rows top first in pBgra: the same file the back buffer writer
    // makes, for a capture that is not the back buffer. 0 on success, 1 on failure.
    int stdDisplay_WriteBgraBmp(const char* pFilename, const uint8_t* pBgra,
        uint32_t width, uint32_t height)
    {
        if ( !pFilename || !pBgra || !width || !height || width > INT32_MAX
            || height > INT32_MAX )
        {
            return 1;
        }

        const uint64_t stride    = ((uint64_t)width * 3u + 3u) & ~uint64_t(3u);
        const uint64_t imageSize = stride * height;
        if ( imageSize > UINT32_MAX - 54u )
        {
            return 1;
        }

        const std::array<uint8_t, 54> aHeader = stdDisplay_MakeBmpHeader(width, height,
            (uint32_t)imageSize);

        tFileHandle fh = std_g_pHS->pFileOpen(pFilename, "wb");
        if ( !fh )
        {
            STDLOG_ERROR("Unable to open file '%s' for writing.\n", pFilename);
            return 1;
        }

        if ( std_g_pHS->pFileWrite(fh, aHeader.data(), aHeader.size()) != aHeader.size() )
        {
            STDLOG_ERROR("Error attempting to write %zu bytes to file '%s'.\n", aHeader.size(), pFilename);
            std_g_pHS->pFileClose(fh);
            return 1;
        }

        std::vector<uint8_t> aRow((size_t)stride, 0u);
        int result = 0;
        for ( uint32_t row = height; row-- > 0u; )
        {
            const uint8_t* pSrc = pBgra + (size_t)width * 4u * row;
            for ( uint32_t x = 0; x < width; ++x )
            {
                aRow[(size_t)x * 3u + 0u] = pSrc[(size_t)x * 4u + 0u];
                aRow[(size_t)x * 3u + 1u] = pSrc[(size_t)x * 4u + 1u];
                aRow[(size_t)x * 3u + 2u] = pSrc[(size_t)x * 4u + 2u];
            }

            if ( std_g_pHS->pFileWrite(fh, aRow.data(), aRow.size()) != aRow.size() )
            {
                STDLOG_ERROR("Error attempting to write %zu bytes to file '%s'.\n", aRow.size(), pFilename);
                result = 1;
                break;
            }
        }

        std_g_pHS->pFileClose(fh);
        return result;
    }
}

namespace
{
    // The window-sized frame including the present overlay, for a screenshot of a menu. False when
    // the target cannot be made or the capture fails; the caller falls back to the back buffer.
    bool stdDisplay_TrySaveOverlayBmp(const char* pFilename, uint32_t width, uint32_t height)
    {
        StdBabylonContext* pContext = stdBabylon_GetContext();
        if ( !pContext || !pContext->pRenderDevice )
        {
            return false;
        }

        const uint32_t maxSize = pContext->pRenderDevice->info().limits.maxTextureDimension;
        if ( !width || !height || width > maxSize || height > maxSize )
        {
            return false;
        }

        const render::TextureHandle target = pContext->pRenderDevice->createTexture(
            render::TextureDesc{ width, height, 1, render::TextureFormat::Bgra8,
                render::TextureUsage::ColorTarget });
        if ( target.isNull() )
        {
            return false;
        }

        bool ok = stdRenderBabylon_RenderPresentToTexture(target, width, height);
        if ( ok )
        {
            const std::vector<std::byte> pixels =
                pContext->pRenderDevice->readTexture(target, 0);
            ok = pixels.size() == (size_t)width * height * 4u
                && stdDisplay_WriteBgraBmp(pFilename,
                    reinterpret_cast<const uint8_t*>(pixels.data()), width, height) == 0;
        }

        pContext->pRenderDevice->destroyTexture(target);
        return ok;
    }

    // Added: Shift+F12 writes the flight recorder beside the normal screenshot, in a sibling
    // directory named after the screenshot file without its extension plus "-last2s". Shift is read
    // from the adapter's shared keyboard state, the same snapshot stdControl reads.
    void stdDisplay_SaveFlightRecorderIfShiftHeld(const char* pScreenshotPath)
    {
        const babylon::input::InputState& rInputState = stdBabylonEvents_InputState();
        if ( !rInputState.keyDown(babylon::platform::Key::LeftShift)
            && !rInputState.keyDown(babylon::platform::Key::RightShift) )
        {
            return;
        }

        std::string aDirectory(pScreenshotPath);
        const size_t separator = aDirectory.find_last_of("/\\");
        const size_t dot       = aDirectory.find_last_of('.');
        if ( dot != std::string::npos && (separator == std::string::npos || dot > separator) )
        {
            aDirectory.erase(dot);
        }
        aDirectory += "-last2s";

        if ( !stdDisplay_SaveFlightRecorder(aDirectory.c_str()) )
        {
            STDLOG_WARNING("stdDisplay: could not write the flight recorder to '%s'.\n",
                aDirectory.c_str());
        }
    }
}

int J3DAPI stdDisplay_SaveScreen(const char* pFilename)
{
    if ( !pFilename || !stdDisplay_state.bModeSet || stdRenderBabylon_GetColorTarget().isNull() )
    {
        return 1;
    }

    int result = 1;

    // Added: menus and the HUD prompt record onto the swapchain, which a back-buffer read cannot
    // see. While either overlay is installed, save the window-sized frame including them in the
    // present order; otherwise keep the back buffer's pixels and size.
    if ( stdRenderBabylon_HasPresentOverlay() || stdRenderBabylon_HasHudOverlay() )
    {
        StdBabylonContext* pContext = stdBabylon_GetContext();
        if ( pContext && pContext->pWindow )
        {
            const babylon::platform::Size size = pContext->pWindow->size();
            if ( size.width > 0 && size.height > 0
                && stdDisplay_TrySaveOverlayBmp(pFilename, (uint32_t)size.width,
                    (uint32_t)size.height) )
            {
                result = 0;
            }
        }
    }

    if ( result != 0 )
    {
        result = stdDisplay_WriteBackBufferBmp(pFilename);
    }

    stdDisplay_SaveFlightRecorderIfShiftHeld(pFilename);
    return result;
}
