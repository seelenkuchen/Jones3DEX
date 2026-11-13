// Added: the Babylon column's Display settings screen; see JonesUiDisplaySettings.h.
//
// Original behavior, from the merged dialogs in
// (git show 82b8f607^:Jones3D/Platform/Win32/jonesConfig.c):
// - jonesConfig_ShowDisplaySettingsDialog (line 6955) returns 1 when the dialog's OK is pressed
//   and 2 on cancel (EndDialog with the command id). Its Cancel path restores the settings the
//   nested Advanced Display dialog had already written; this screen is one dialog, so nothing is
//   written before OK.
// - The Display dialog's OK (line 7276) writes pSettings->videoModeNum, width and height, and the
//   graphics.display, graphics.device, graphics.width, graphics.height, graphics.bpp,
//   graphics.refreshRate, engine.renderer.fog.density, engine.renderer.fog.enabled,
//   graphics.buffering and graphics.mipmapFilter keys, then applies fog to the world.
// - The Advanced Display dialog's OK (line 7845) writes pSettings->bFog and fogDensity,
//   pSettings->filter, graphics.performanceLevel, the high-poly setting and its key; MSAA,
//   automatic mipmap generation and anisotropic filtering were only offered when the removed DX9
//   backend supported them and have no effect now. The Babylon display ignores buffering, and its
//   one display device and one device make the device and rasterizer choices meaningless, so those
//   controls are not offered; their stored values and keys are still written unchanged on OK.
// - The resolution list (line 7075) is the device's modes. Two modes of the same size share one
//   row here, as the original shared one row across bit depths; the bit depth is its own choice
//   when the column offers more than one.
//
// Options added for the Babylon column: window mode maps to pSettings->bWindowMode and
// graphics.window, and engine.renderer.litFaces.enabled is a presentation toggle read when the
// display opens (Docs/Jones.cfg.md).
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>

#include <Jones3D/Display/JonesDisplay.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <sith/Engine/sithRender.h>
#include <sith/World/sithModel.h>
#include <sith/World/sithWorld.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdConfig.h>

#include <babylon/ui/Screen.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr int kJonesDialogOk     = 1;
    constexpr int kJonesDialogCancel = 2;

    constexpr babylon::ui::ActionId kJonesDisplayActionOk   = 1;
    constexpr babylon::ui::ActionId kJonesDisplayActionBack = 2;

    // The original dialog's performance radio buttons wrote these values.
    constexpr int kJonesDisplayPerformanceValues[] = { 0, 2, 4 };

    // Added: the original dialog took its captions from the dialog resource, which holds no
    // JONES_STR key. The English text is used until one exists.
    constexpr const char* kJonesDisplayBitDepthText    = "Bit depth";   // TODO: [COMPAT]
    constexpr const char* kJonesDisplayResolutionText  = "Resolution";  // TODO: [COMPAT]
    constexpr const char* kJonesDisplayWindowedText    = "Windowed";    // TODO: [COMPAT]
    constexpr const char* kJonesDisplayFilterText      = "Mipmap filter"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayFogText         = "Fog";         // TODO: [COMPAT]
    constexpr const char* kJonesDisplayFogDensityText  = "Fog density"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayPerformanceText = "Performance"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayHiPolyText      = "Hi-poly models"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayLitFacesText    =
        "Lit faces (applies when the display reopens)"; // TODO: [COMPAT]

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

    int JonesUiDisplay_ReadConfigInt(const char* pKey, int defaultValue)
    {
        return stdConfig_HasStarted() ? stdConfig_GetInt(pKey, defaultValue) : defaultValue;
    }

    bool JonesUiDisplay_ReadConfigBool(const char* pKey, bool defaultValue)
    {
        return stdConfig_HasStarted() ? stdConfig_GetBool(pKey, defaultValue) : defaultValue;
    }

    void JonesUiDisplay_SetConfigInt(const char* pKey, int value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetInt(pKey, value));
        }
    }

    void JonesUiDisplay_SetConfigBool(const char* pKey, bool value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetBool(pKey, value));
        }
    }

    void JonesUiDisplay_SetConfigFloat(const char* pKey, float value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetFloat(pKey, value));
        }
    }

    void JonesUiDisplay_SetConfigString(const char* pKey, const char* pValue)
    {
        if ( stdConfig_HasStarted() && pValue )
        {
            static_cast<void>(stdConfig_SetString(pKey, pValue));
        }
    }

    std::string JonesUiDisplay_ResolveLabel(const char* pText)
    {
        return JonesUiContext_ResolveString(pText);
    }

    babylon::ui::Screen BuildDisplayScreen(const JonesUiDisplayModel& model,
        const JonesUiDisplayValues& values, JonesUiDisplayLayout& layout,
        bool bShowPerformance)
    {
        babylon::ui::Screen screen;
        screen.title = JonesUiDisplay_ResolveLabel("JONES_STR_DPLY_OPTS");

        // A bit-depth choice is only useful when the column offers more than one.
        if ( model.bitDepths.size() > 1 )
        {
            std::vector<std::string> options;
            options.reserve(model.bitDepths.size());
            for ( int bpp : model.bitDepths )
            {
                options.push_back(JonesUiDisplay_BitDepthLabel(bpp));
            }

            layout.bitDepth = screen.widgets.size();
            screen.widgets.emplace_back(babylon::ui::Choice{
                JonesUiDisplay_ResolveLabel(kJonesDisplayBitDepthText), std::string(),
                std::move(options), true, values.bitDepth });
        }

        if ( !model.modes.empty() )
        {
            std::vector<std::string> options;
            options.reserve(model.modes.size());
            for ( const JonesUiDisplayVideoMode& mode : model.modes )
            {
                options.push_back(mode.text);
            }

            layout.resolution = screen.widgets.size();
            screen.widgets.emplace_back(babylon::ui::Choice{
                JonesUiDisplay_ResolveLabel(kJonesDisplayResolutionText), std::string(),
                std::move(options), true, values.resolution });
        }

        layout.windowed = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayWindowedText), std::string(), true,
            values.bWindowed });

        // Added: the original radio-button captions came from the dialog resource, which has no
        // JONES_STR key.
        std::vector<std::string> filterOptions = {
            JonesUiDisplay_ResolveLabel("None"),      // TODO: [COMPAT]
            JonesUiDisplay_ResolveLabel("Bilinear"),  // TODO: [COMPAT]
            JonesUiDisplay_ResolveLabel("Trilinear"), // TODO: [COMPAT]
        };
        layout.filter = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayFilterText), std::string(),
            std::move(filterOptions), true, values.filter });

        layout.fog = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayFogText), std::string(), true,
            values.bFog });

        layout.fogDensity = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Slider{
            JonesUiDisplay_ResolveLabel(kJonesDisplayFogDensityText), std::string(), 0.0, 1.0,
            0.01, true, values.fogDensity });

        // The Options Display tab drops the Performance row (ADR-0028); the value is still
        // written back unchanged by the apply path.
        if ( bShowPerformance )
        {
            std::vector<std::string> performanceOptions = {
                JonesUiDisplay_ResolveLabel("Average"),   // TODO: [COMPAT]
                JonesUiDisplay_ResolveLabel("Fast"),      // TODO: [COMPAT]
                JonesUiDisplay_ResolveLabel("Very fast"), // TODO: [COMPAT]
            };
            layout.performance = screen.widgets.size();
            screen.widgets.emplace_back(babylon::ui::Choice{
                JonesUiDisplay_ResolveLabel(kJonesDisplayPerformanceText), std::string(),
                std::move(performanceOptions), true, values.performance });
        }

        layout.hiPoly = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayHiPolyText), std::string(), true,
            values.bHiPoly });

        layout.litFaces = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayLitFacesText), std::string(), true,
            values.bLitFaces });

        layout.ok = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiDisplay_ResolveLabel("OK"), kJonesDisplayActionOk, true });

        layout.back = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_CANCEL"), kJonesDisplayActionBack, true });

        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = 0;
        return screen;
    }

    bool JonesUiDisplay_ReadChoice(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
        std::optional<std::size_t> index, std::size_t& value)
    {
        if ( !index )
        {
            return false;
        }

        const std::optional<babylon::ui::WidgetValue> pValue = ui.widgetValue(screen, *index);
        if ( !pValue || !std::holds_alternative<std::int64_t>(*pValue) )
        {
            return false;
        }

        value = (std::size_t)std::max<std::int64_t>(0, std::get<std::int64_t>(*pValue));
        return true;
    }

    bool JonesUiDisplay_ReadToggle(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
        std::size_t index, bool& value)
    {
        const std::optional<babylon::ui::WidgetValue> pValue = ui.widgetValue(screen, index);
        if ( !pValue || !std::holds_alternative<bool>(*pValue) )
        {
            return false;
        }

        value = std::get<bool>(*pValue);
        return true;
    }

    bool JonesUiDisplay_ReadSlider(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
        std::size_t index, double& value)
    {
        const std::optional<babylon::ui::WidgetValue> pValue = ui.widgetValue(screen, index);
        if ( !pValue || !std::holds_alternative<double>(*pValue) )
        {
            return false;
        }

        value = std::get<double>(*pValue);
        return true;
    }
}

JonesUiDisplayModel JonesUiDisplay_LoadModel(const StdDisplayEnvironment* pDisplayEnv,
    JonesDisplaySettings* pSettings, JonesUiDisplayValues& values)
{
    JonesUiDisplayModel model;
    model.pDisplayEnv = pDisplayEnv;
    model.pSettings   = pSettings;
    if ( !pSettings )
    {
        return model;
    }

    const StdDisplayInfo* pInfo = JonesUiDisplay_GetInfo(pDisplayEnv, pSettings->displayDeviceNum);
    static_cast<void>(JonesUiDisplay_CollectVideoModes(pDisplayEnv,
        pSettings->displayDeviceNum, model.modes));

    for ( const JonesUiDisplayVideoMode& mode : model.modes )
    {
        for ( int bpp : mode.aBitDepths )
        {
            if ( std::find(model.bitDepths.begin(), model.bitDepths.end(), bpp)
                == model.bitDepths.end() )
            {
                model.bitDepths.push_back(bpp);
            }
        }
    }
    std::sort(model.bitDepths.begin(), model.bitDepths.end());

    model.bHasCurMode = JonesUiDisplay_FindCurrentMode(pDisplayEnv,
        pSettings->displayDeviceNum, pSettings, model.curModeNum);
    if ( model.bHasCurMode && pInfo )
    {
        model.curBpp = (int)pInfo->aModes[model.curModeNum].rasterInfo.colorInfo.bpp;
    }

    const auto bitDepthIt = std::find(model.bitDepths.begin(), model.bitDepths.end(),
        model.curBpp);
    if ( bitDepthIt != model.bitDepths.end() )
    {
        model.curBitDepth = (std::size_t)(bitDepthIt - model.bitDepths.begin());
    }

    model.curResolution = 0;
    if ( model.bHasCurMode )
    {
        for ( std::size_t cur = 0; cur < model.modes.size(); ++cur )
        {
            if ( model.modes[cur].width == pInfo->aModes[model.curModeNum].rasterInfo.width
                && model.modes[cur].height == pInfo->aModes[model.curModeNum].rasterInfo.height )
            {
                model.curResolution = cur;
                break;
            }
        }
    }

    model.perfLevel = JonesUiDisplay_ReadConfigInt(
        JONESDISPLAY_CFG_GRAPHICS_PERFORMANCELEVEL, 4);
    // Anything outside the original radio values keeps its "very fast" default (4).
    int performance = (int)STD_ARRAYLEN(kJonesDisplayPerformanceValues) - 1;
    for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayPerformanceValues); ++cur )
    {
        if ( kJonesDisplayPerformanceValues[cur] == model.perfLevel )
        {
            performance = (int)cur;
            break;
        }
    }

    // The original Advanced Display init showed the runtime high-poly state, not the config key.
    model.bHiPoly = sithModel_IsHiPolyEnabled();
    model.bLitFaces = JonesUiDisplay_ReadConfigBool(
        JONESDISPLAY_CFG_ENGINE_RENDER_LITFACESENABLED, true);

    values.bBitDepth   = model.bitDepths.size() > 1;
    values.bitDepth    = model.curBitDepth;
    values.bResolution = !model.modes.empty();
    values.resolution  = model.curResolution;
    values.bWindowed   = pSettings->bWindowMode != 0;
    values.filter      = (std::size_t)std::clamp((int)pSettings->filter, 0, 2);
    values.bFog        = pSettings->bFog != 0;
    values.fogDensity  = (double)std::clamp(pSettings->fogDensity, 0.0f, 1.0f);
    values.performance = (std::size_t)performance;
    values.bHiPoly     = model.bHiPoly;
    values.bLitFaces   = model.bLitFaces;
    return model;
}

babylon::ui::Screen JonesUiDisplay_BuildScreen(const JonesUiDisplayModel& model,
    const JonesUiDisplayValues& values, JonesUiDisplayLayout& layout, bool bShowPerformance)
{
    return BuildDisplayScreen(model, values, layout, bShowPerformance);
}

void JonesUiDisplay_ReadValues(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
    const JonesUiDisplayLayout& layout, JonesUiDisplayValues& values)
{
    if ( layout.bitDepth )
    {
        values.bBitDepth = JonesUiDisplay_ReadChoice(ui, screen, layout.bitDepth,
            values.bitDepth);
    }
    if ( layout.resolution )
    {
        values.bResolution = JonesUiDisplay_ReadChoice(ui, screen, layout.resolution,
            values.resolution);
    }
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.windowed,
        values.bWindowed));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.filter,
        values.filter));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.fog,
        values.bFog));
    static_cast<void>(JonesUiDisplay_ReadSlider(ui, screen, layout.fogDensity,
        values.fogDensity));
    if ( layout.performance )
    {
        static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.performance,
            values.performance));
    }
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.hiPoly,
        values.bHiPoly));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.litFaces,
        values.bLitFaces));
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

int JonesUiDisplay_Apply(const JonesUiDisplayModel& model,
    const JonesUiDisplayValues& values)
{
    JonesDisplaySettings* pSettings = model.pSettings;
    if ( !pSettings )
    {
        return kJonesDialogCancel;
    }

    const StdDisplayInfo* pInfo = JonesUiDisplay_GetInfo(model.pDisplayEnv,
        pSettings->displayDeviceNum);

    // The original Display OK wrote the chosen mode and the device's current keys.
    std::size_t newModeNum = pSettings->videoModeNum;
    std::uint32_t newWidth  = pSettings->width;
    std::uint32_t newHeight = pSettings->height;
    if ( model.bHasCurMode && pInfo && !model.modes.empty() )
    {
        const std::size_t resolution = std::min(values.resolution, model.modes.size() - 1);
        const JonesUiDisplayVideoMode& mode = model.modes[resolution];

        std::size_t chosen = mode.aModeNums.empty() ? 0 : mode.aModeNums.front();
        const int bpp = (!model.bitDepths.empty() && values.bBitDepth)
            ? model.bitDepths[std::min(values.bitDepth, model.bitDepths.size() - 1)]
            : model.curBpp;
        for ( std::size_t cur = 0; cur < mode.aBitDepths.size(); ++cur )
        {
            if ( mode.aBitDepths[cur] == bpp )
            {
                chosen = mode.aModeNums[cur];
                break;
            }
        }

        const std::size_t selectedMode = std::min(chosen,
            (std::size_t)(pInfo->numModes ? pInfo->numModes - 1 : 0));
        newModeNum = selectedMode;
        newWidth   = pInfo->aModes[selectedMode].rasterInfo.width;
        newHeight  = pInfo->aModes[selectedMode].rasterInfo.height;
    }

    pSettings->videoModeNum = newModeNum;
    pSettings->width        = newWidth;
    pSettings->height       = newHeight;
    pSettings->bWindowMode  = values.bWindowed ? 1 : 0;
    pSettings->bFog         = values.bFog ? 1 : 0;
    pSettings->fogDensity   = (float)std::clamp(values.fogDensity, 0.0, 1.0);
    pSettings->filter       = (Std3DMipmapFilterType)std::clamp<int>((int)values.filter, 0, 2);
    // Buffering has no effect in the Babylon column and is not offered, but the original wrote it.
    // The field keeps its loaded value and is written back unchanged.

    if ( pInfo )
    {
        JonesUiDisplay_SetConfigString(JONESDISPLAY_CFG_GRAPHICS_DISPLAY,
            pInfo->displayDevice.aDriverName);
        if ( pInfo->aDevices && pSettings->device3DNum >= 0
            && (std::size_t)pSettings->device3DNum < pInfo->numDevices )
        {
            JonesUiDisplay_SetConfigString(JONESDISPLAY_CFG_GRAPHICS_DEVICE,
                pInfo->aDevices[pSettings->device3DNum].deviceDescription);
        }

        if ( pInfo->aModes && newModeNum < pInfo->numModes )
        {
            const StdVideoMode& mode = pInfo->aModes[newModeNum];
            JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_GRAPHICS_WIDTH, (int)newWidth);
            JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_GRAPHICS_HEIGHT, (int)newHeight);
            JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_GRAPHICS_BPP,
                (int)mode.rasterInfo.colorInfo.bpp);
            JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_GRAPHICS_REFRESHRATE,
                (int)mode.refreshRate);
        }
    }

    sithRender_g_fogDensity = pSettings->fogDensity * 100.0f;
    JonesUiDisplay_SetConfigFloat(JONESDISPLAY_CFG_ENGINE_RENDER_FOGDENSITY,
        sithRender_g_fogDensity / 100.0f);
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_FOGENABLED,
        pSettings->bFog != 0);
    std3D_EnableFog(pSettings->bFog, sithRender_g_fogDensity);
    if ( sithWorld_g_pCurrentWorld )
    {
        sithWorld_g_pCurrentWorld->state =
            (SithWorldState)(sithWorld_g_pCurrentWorld->state | SITH_WORLD_STATE_UPDATE_FOG);
    }

    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_GRAPHICS_BUFFERING,
        pSettings->bBuffering != 0);
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_GRAPHICS_MIPMAPFILTER,
        (int)pSettings->filter);
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_GRAPHICS_WINDOW,
        pSettings->bWindowMode != 0);

    const int newPerf = kJonesDisplayPerformanceValues[
        std::min(values.performance, STD_ARRAYLEN(kJonesDisplayPerformanceValues) - 1)];
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_GRAPHICS_PERFORMANCELEVEL, newPerf);
    if ( JonesMain_HasStarted() && newPerf != model.perfLevel )
    {
        const char* pMessage = jonesString_GetString("JONES_STR_PERFORMANCE");
        if ( pMessage )
        {
            static_cast<void>(jonesConfig_ShowMessageDialog(nullptr, "JONES_STR_DPLY_OPTS",
                pMessage, 136));
        }
    }

    const bool bWasHiPoly = sithModel_IsHiPolyEnabled();
    sithModel_EnableHiPoly(values.bHiPoly ? 1 : 0);
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_HIPOLY, values.bHiPoly);
    if ( values.bHiPoly != bWasHiPoly )
    {
        const char* pNote = jonesString_GetString("JONES_STR_HIPOLYGO");
        if ( !pNote )
        {
            // The original dialog's fallback text.
            pNote = "You must quit and restart the game for the high poly option to take effect.";
        }

        static_cast<void>(jonesConfig_ShowMessageDialog(nullptr, "JONES_STR_GMPLY_OPTS", pNote,
            136));
    }

    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_LITFACESENABLED,
        values.bLitFaces);

    return kJonesDialogOk;
}

int JonesUiDisplay_RunDisplayScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const StdDisplayEnvironment* pDisplayEnv, JonesDisplaySettings* pSettings)
{
    if ( !pSettings )
    {
        return kJonesDialogCancel;
    }

    JonesUiDisplayValues values;
    const JonesUiDisplayModel model = JonesUiDisplay_LoadModel(pDisplayEnv, pSettings, values);

    JonesUiDisplayLayout layout;
    const babylon::ui::Screen screen = JonesUiDisplay_BuildScreen(model, values, layout,
        true);

    bool bApply = false;
    const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
        babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
    {
        const babylon::ui::ActionPressed* pAction =
            std::get_if<babylon::ui::ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId
            || pAction->action != kJonesDisplayActionOk )
        {
            return false;
        }

        // Read the unbound widget values while the screen is still open; accepting the OK button
        // pops it right after this handler returns. A missing value keeps the initial one.
        JonesUiDisplay_ReadValues(uiRef, screenId, layout, values);
        bApply = true;
        return false;
    };

    const int result = JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler);
    if ( result != kJonesDialogOk || !bApply )
    {
        return result;
    }

    return JonesUiDisplay_Apply(model, values);
}

extern "C" int J3DAPI jonesConfig_ShowDisplaySettingsDialog(tSysWindow hWnd,
    StdDisplayEnvironment* pDisplayEnv, JonesDisplaySettings* pDSettings)
{
    (void)hWnd;

    if ( !pDSettings )
    {
        return kJonesDialogCancel;
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow )
    {
        // Headless: the Null stub answers cancel without blocking.
        return kJonesDialogCancel;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return kJonesDialogCancel;
    }

    const int result = JonesUiDisplay_RunDisplayScreen(*pUiContext->pUi, &pUiContext->painter,
        pDisplayEnv, pDSettings);
    JonesUiContext_Release(pUiContext);
    return result;
}

#ifdef J3D_TEST
void JonesUiDisplay_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens)
{
    JonesDisplaySettings settings{};
    const std::vector<JonesUiDisplayVideoMode> modes;
    const std::vector<int> bitDepths;

    JonesUiDisplayValues values;
    const JonesUiDisplayModel model = JonesUiDisplay_LoadModel(nullptr, &settings, values);
    JonesUiDisplayLayout layout;
    screens.push_back(JonesUiDisplay_BuildScreen(model, values, layout, true));
}
#endif
