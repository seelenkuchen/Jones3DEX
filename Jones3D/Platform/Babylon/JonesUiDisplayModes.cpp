// Added: the video-mode and bit-depth helpers of the Display settings screen.
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>

#include <Jones3D/Main/jonesString.h>

#include <std/General/stdUtil.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Jones3D/Platform/Babylon/JonesUiDisplaySettingsInternal.h>

using namespace JonesUiDisplaySettingsInternal;

namespace
{
    std::string JonesUiDisplay_ResolutionText(std::uint32_t width, std::uint32_t height,
        std::uint32_t refreshRate)
    {
        // Added: the original used JONES_STR_BYPIXELS as the format. A missing or non-format
        // value falls back to the same shape so a test without the string table still gets a
        // readable label.
        const char* pFormat = jonesString_GetString("JONES_STR_BYPIXELS");
        if ( !pFormat || !pFormat[0] || std::strchr(pFormat, '%') == nullptr )
        {
            pFormat = "%ux%u";
        }

        char aText[256] = { 0 };
        std::snprintf(aText, sizeof(aText), pFormat, (unsigned)width, (unsigned)height);

        if ( refreshRate > 0 )
        {
            const std::size_t pos = std::strlen(aText);
            std::snprintf(&aText[pos], sizeof(aText) - pos, " (%u Hz)", (unsigned)refreshRate);
        }

        return std::string(aText);
    }
}

namespace JonesUiDisplaySettingsInternal
{
    std::string JonesUiDisplay_BitDepthLabel(int bpp)
    {
        const char* pKey = nullptr;
        switch ( bpp )
        {
            case 16: pKey = "JONES_STR_16BBP"; break;
            case 24: pKey = "JONES_STR_24BBP"; break;
            case 32: pKey = "JONES_STR_32BBP"; break;
            default: break;
        }

        if ( pKey )
        {
            const char* pValue = jonesString_GetString(pKey);
            // The string table returns the key itself when it has no entry, which is the case in
            // a unit test with no game assets.
            if ( pValue && pValue[0] && std::strcmp(pValue, pKey) != 0 )
            {
                return std::string(pValue);
            }
        }

        char aText[32] = { 0 };
        std::snprintf(aText, sizeof(aText), "%d-bit", bpp);
        return std::string(aText);
    }

    const StdDisplayInfo* JonesUiDisplay_GetInfo(const StdDisplayEnvironment* pEnv,
        int displayDeviceNum)
    {
        if ( !pEnv || !pEnv->aDisplayInfos || displayDeviceNum < 0
            || (std::size_t)displayDeviceNum >= pEnv->numInfos )
        {
            return nullptr;
        }

        return &pEnv->aDisplayInfos[displayDeviceNum];
    }

    bool JonesUiDisplay_FindCurrentMode(const StdDisplayEnvironment* pEnv, int displayDeviceNum,
        const JonesDisplaySettings* pSettings, std::size_t& modeNum)
    {
        const StdDisplayInfo* pInfo = JonesUiDisplay_GetInfo(pEnv, displayDeviceNum);
        if ( !pInfo || !pInfo->aModes || pInfo->numModes == 0 )
        {
            return false;
        }

        if ( pSettings->videoModeNum < pInfo->numModes )
        {
            modeNum = pSettings->videoModeNum;
            return true;
        }

        for ( std::size_t cur = 0; cur < pInfo->numModes; ++cur )
        {
            if ( pInfo->aModes[cur].rasterInfo.width == pSettings->width
                && pInfo->aModes[cur].rasterInfo.height == pSettings->height )
            {
                modeNum = cur;
                return true;
            }
        }

        modeNum = 0;
        return true;
    }
}

bool JonesUiDisplay_CollectVideoModes(const StdDisplayEnvironment* pEnv, int displayDeviceNum,
    std::vector<JonesUiDisplayVideoMode>& modes)
{
    modes.clear();

    const StdDisplayInfo* pInfo = JonesUiDisplay_GetInfo(pEnv, displayDeviceNum);
    if ( !pInfo || !pInfo->aModes || pInfo->numModes == 0 )
    {
        return false;
    }

    for ( std::size_t modeNum = 0; modeNum < pInfo->numModes; ++modeNum )
    {
        const StdVideoMode& mode = pInfo->aModes[modeNum];
        const int bpp = (int)mode.rasterInfo.colorInfo.bpp;

        JonesUiDisplayVideoMode* pEntry = nullptr;
        for ( JonesUiDisplayVideoMode& entry : modes )
        {
            if ( entry.width == mode.rasterInfo.width && entry.height == mode.rasterInfo.height )
            {
                pEntry = &entry;
                break;
            }
        }

        if ( !pEntry )
        {
            JonesUiDisplayVideoMode entry;
            entry.width       = mode.rasterInfo.width;
            entry.height      = mode.rasterInfo.height;
            entry.refreshRate = mode.refreshRate;
            entry.text        = JonesUiDisplay_ResolutionText(entry.width, entry.height,
                entry.refreshRate);
            modes.push_back(std::move(entry));
            pEntry = &modes.back();
        }

        if ( std::find(pEntry->aBitDepths.begin(), pEntry->aBitDepths.end(), bpp)
            == pEntry->aBitDepths.end() )
        {
            pEntry->aBitDepths.push_back(bpp);
            pEntry->aModeNums.push_back(modeNum);
        }
    }

    return true;
}
