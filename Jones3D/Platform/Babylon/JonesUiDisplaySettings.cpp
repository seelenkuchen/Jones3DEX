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
#include <Jones3D/Platform/Babylon/JonesUiBrightnessCalibration.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <sith/Engine/sithCamera.h>
#include <sith/Engine/sithRender.h>
#include <sith/World/sithModel.h>
#include <sith/World/sithWorld.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/std3DBabylon.h>
#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdConfig.h>
#include <std/General/stdUtil.h>

#include <babylon/ui/Screen.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <cmath>
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
    constexpr const char* kJonesDisplayFOVText = "Field of view"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayRenderScaleText = "Render scale"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayAntiAliasingText = "Anti-aliasing"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayBloomText = "Bloom"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayLightShaftsText = "Light shafts"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayLensFlareText = "Lens flare"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayVignetteText = "Vignette"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayFilmGrainText = "Film grain"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayWaterRipplesText = "Water ripples"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayDofCutscenesText = "Depth of field (cutscenes)"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayDofGameplayText = "Depth of field (gameplay)"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplaySharpeningText = "Sharpening"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayToneMappingText = "Tone mapping"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayExposureText = "Exposure"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayColorGradingText = "Colour grading"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayAnisotropicText = "Anisotropic filtering"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayCrispPixelsText = "Crisp pixels"; // TODO: [COMPAT]
    constexpr const char* kJonesDisplayBrightnessText = "Brightness..."; // TODO: [COMPAT]

    // Added: M9.4 render scale choices. The value is a multiple of the game's 3D resolution; the
    // label is what the Options row shows. 100% (index 3) is the default.
    constexpr float kJonesDisplayRenderScaleValues[] = { 0.5f, 0.67f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    constexpr const char* kJonesDisplayRenderScaleLabels[] = {
        "50%", "67%", "75%", "100%", "125%", "150%", "200%" };
    constexpr std::size_t kJonesDisplayRenderScaleDefault = 3;

    // Added: M9.8 anti-aliasing choices; SMAA (index 1) is the Enhanced default, off in Retail.
    constexpr int kJonesDisplayAntiAliasingValues[] = { 0, 1 };
    constexpr const char* kJonesDisplayAntiAliasingLabels[] = { "Off", "SMAA" };
    constexpr std::size_t kJonesDisplayAntiAliasingDefault = J3D_QOL_VALUE(1, 0);

    // Added: M9.12 bloom choices; Low (1) is the Enhanced default, off in Retail/QOL-off builds.
    constexpr int kJonesDisplayBloomValues[] = { 0, 1, 2, 3 };
    constexpr const char* kJonesDisplayBloomLabels[] = { "Off", "Low", "Medium", "High" };
    constexpr std::size_t kJonesDisplayBloomDefault = J3D_QOL_VALUE(1, 0);

    // Added: M9.16 light-shaft choices; Low (1) is the Enhanced default, off in Retail/QOL-off
    // builds.
    constexpr int kJonesDisplayLightShaftsValues[] = { 0, 1, 2, 3 };
    constexpr const char* kJonesDisplayLightShaftsLabels[] = { "Off", "Low", "Medium", "High" };
    constexpr std::size_t kJonesDisplayLightShaftsDefault = J3D_QOL_VALUE(1, 0);

    // Added: M9.18 lens flare; on in Enhanced builds and off in Retail/QOL-off ones. The vignette
    // and film grain are off in every build.
    constexpr bool kJonesDisplayLensFlareDefault = J3D_QOL_VALUE(1, 0) != 0;
    constexpr bool kJonesDisplayVignetteDefault  = false;
    constexpr bool kJonesDisplayFilmGrainDefault = false;

    // Added: M9.17 water ripples; on in Enhanced builds and off in Retail/QOL-off ones.
    constexpr bool kJonesDisplayWaterRipplesDefault = J3D_QOL_VALUE(1, 0) != 0;

    // Added: M9.14 depth-of-field rows. Both are on in Enhanced builds and off in Retail/QOL-off
    // ones. The strength has no row; the Options apply path leaves its key as it is.
    constexpr bool kJonesDisplayDofCutscenesDefault = J3D_QOL_VALUE(1, 0) != 0;
    constexpr bool kJonesDisplayDofGameplayDefault  = J3D_QOL_VALUE(1, 0) != 0;

    // Added: M9.9 contrast-adaptive sharpening choices. Low (0.3) is the Enhanced default, off in
    // Retail/QOL-off builds.
    constexpr float kJonesDisplaySharpeningValues[] = { 0.0f, 0.3f, 0.5f, 0.8f };
    constexpr const char* kJonesDisplaySharpeningLabels[] = { "Off", "Low", "Medium", "High" };
    constexpr std::size_t kJonesDisplaySharpeningDefault = J3D_QOL_VALUE(1, 0);

    // Added: M9.10 tone-mapping operators; Neutral (index 1) is the Enhanced default, clamp in
    // Retail/QOL-off builds.
    constexpr int kJonesDisplayToneMappingValues[] = { 0, 1, 2 };
    constexpr const char* kJonesDisplayToneMappingLabels[] = { "Clamp", "Neutral", "Filmic" };
    constexpr std::size_t kJonesDisplayToneMappingDefault = J3D_QOL_VALUE(1, 0);
    constexpr double kJonesDisplayExposureMin = 0.5;
    constexpr double kJonesDisplayExposureMax = 2.0;
    constexpr double kJonesDisplayExposureStep = 0.1;

    // Added: M9.13 color-grading presets; Neutral (index 0) is the default in every build.
    constexpr int kJonesDisplayColorGradingValues[] = { 0, 1, 2, 3, 4, 5 };
    constexpr const char* kJonesDisplayColorGradingLabels[] = {
        "Neutral", "Warm", "Cool", "Adventure", "Vivid", "Muted" };
    constexpr std::size_t kJonesDisplayColorGradingDefault = 0;

    // Added: M9.7 anisotropic filtering choices; 8x (index 3) is the Enhanced default.
    constexpr int kJonesDisplayAnisotropicValues[] = { 1, 2, 4, 8, 16 };
    constexpr const char* kJonesDisplayAnisotropicLabels[] = { "Off", "2x", "4x", "8x", "16x" };
    constexpr std::size_t kJonesDisplayAnisotropicDefault = 3;

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

    float JonesUiDisplay_ReadConfigFloat(const char* pKey, float defaultValue)
    {
        return stdConfig_HasStarted() ? stdConfig_GetFloat(pKey, defaultValue) : defaultValue;
    }

    // The render scale choice nearest to scale; 100% when the value is not finite.
    std::size_t JonesUiDisplay_RenderScaleIndex(float scale)
    {
        if ( !std::isfinite(scale) )
        {
            return kJonesDisplayRenderScaleDefault;
        }

        std::size_t best = 0;
        float bestDistance = std::fabs(scale - kJonesDisplayRenderScaleValues[0]);
        for ( std::size_t cur = 1; cur < STD_ARRAYLEN(kJonesDisplayRenderScaleValues); ++cur )
        {
            const float distance = std::fabs(scale - kJonesDisplayRenderScaleValues[cur]);
            if ( distance < bestDistance )
            {
                best         = cur;
                bestDistance = distance;
            }
        }

        return best;
    }

    // The anti-aliasing choice of a stored value: SMAA for a nonzero one, Off otherwise.
    std::size_t JonesUiDisplay_AntiAliasingIndex(int value)
    {
        return value != 0 ? 1u : 0u;
    }

    // The bloom choice of a stored level; Low when it is not one of the offered ones.
    std::size_t JonesUiDisplay_BloomIndex(int level)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayBloomValues); ++cur )
        {
            if ( kJonesDisplayBloomValues[cur] == level )
            {
                return cur;
            }
        }

        return kJonesDisplayBloomDefault;
    }

    // The light-shaft choice of a stored level; Low when it is not one of the offered ones.
    std::size_t JonesUiDisplay_LightShaftsIndex(int level)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayLightShaftsValues); ++cur )
        {
            if ( kJonesDisplayLightShaftsValues[cur] == level )
            {
                return cur;
            }
        }

        return kJonesDisplayLightShaftsDefault;
    }

    // The sharpening choice nearest to strength; Low when the value is not finite.
    std::size_t JonesUiDisplay_SharpeningIndex(float strength)
    {
        if ( !std::isfinite(strength) )
        {
            return kJonesDisplaySharpeningDefault;
        }

        std::size_t best = 0;
        float bestDistance = std::fabs(strength - kJonesDisplaySharpeningValues[0]);
        for ( std::size_t cur = 1; cur < STD_ARRAYLEN(kJonesDisplaySharpeningValues); ++cur )
        {
            const float distance = std::fabs(strength - kJonesDisplaySharpeningValues[cur]);
            if ( distance < bestDistance )
            {
                best         = cur;
                bestDistance = distance;
            }
        }

        return best;
    }

    // The tone-mapping choice of a stored operator; Neutral when it is not one of the offered ones.
    std::size_t JonesUiDisplay_ToneMappingIndex(int oper)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayToneMappingValues); ++cur )
        {
            if ( kJonesDisplayToneMappingValues[cur] == oper )
            {
                return cur;
            }
        }

        return kJonesDisplayToneMappingDefault;
    }

    // The color-grading choice of a stored preset; Neutral when it is not one of the offered ones.
    std::size_t JonesUiDisplay_ColorGradingIndex(int preset)
    {
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayColorGradingValues); ++cur )
        {
            if ( kJonesDisplayColorGradingValues[cur] == preset )
            {
                return cur;
            }
        }

        return kJonesDisplayColorGradingDefault;
    }

    // The anisotropic choice of a level; the nearest offered step, 8x when it is not usable.
    std::size_t JonesUiDisplay_AnisotropicIndex(int level)
    {
        const int normalized = std3DBabylon_NormalizeAnisotropicFilterLevel(level);
        for ( std::size_t cur = 0; cur < STD_ARRAYLEN(kJonesDisplayAnisotropicValues); ++cur )
        {
            if ( kJonesDisplayAnisotropicValues[cur] == normalized )
            {
                return cur;
            }
        }

        return kJonesDisplayAnisotropicDefault;
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

        // Added: the player's Field of view, horizontal degrees at 4:3. It only changes what the
        // player sees; the simulation keeps its default visibility frustum.
        layout.gameplayFOV = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Slider{
            JonesUiDisplay_ResolveLabel(kJonesDisplayFOVText), std::string(),
            (double)SITHCAMERA_GAMEPLAYFOVMIN, (double)SITHCAMERA_GAMEPLAYFOVMAX, 1.0, true,
            values.gameplayFOV });

        // Added: M9.4. The internal 3D resolution heads up into supersampling. Screen coordinates,
        // menus and the read-back image keep the display size; the in-world HUD is drawn with the
        // scene.
        std::vector<std::string> renderScaleOptions;
        for ( const char* pLabel : kJonesDisplayRenderScaleLabels )
        {
            renderScaleOptions.emplace_back(pLabel);
        }
        layout.renderScale = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayRenderScaleText), std::string(),
            std::move(renderScaleOptions), true,
            values.renderScale < STD_ARRAYLEN(kJonesDisplayRenderScaleValues)
                ? values.renderScale : kJonesDisplayRenderScaleDefault });

        // Added: M9.8. SMAA smooths the 3D scene's edges after the tone map; the HUD, menus and
        // prompts are never anti-aliased.
        std::vector<std::string> antiAliasingOptions;
        for ( const char* pLabel : kJonesDisplayAntiAliasingLabels )
        {
            antiAliasingOptions.emplace_back(pLabel);
        }
        layout.antiAliasing = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayAntiAliasingText), std::string(),
            std::move(antiAliasingOptions), true,
            values.antiAliasing < STD_ARRAYLEN(kJonesDisplayAntiAliasingValues)
                ? values.antiAliasing : kJonesDisplayAntiAliasingDefault });

        // Added: M9.12. Bloom adds a soft glow around bright parts of the 3D scene before the tone
        // map; the HUD, menus and prompts are never bloomed.
        std::vector<std::string> bloomOptions;
        for ( const char* pLabel : kJonesDisplayBloomLabels )
        {
            bloomOptions.emplace_back(pLabel);
        }
        layout.bloom = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayBloomText), std::string(),
            std::move(bloomOptions), true,
            values.bloom < STD_ARRAYLEN(kJonesDisplayBloomValues)
                ? values.bloom : kJonesDisplayBloomDefault });

        // Added: M9.16. Light shafts smear the bright sky into rays; the HUD, menus and prompts
        // are never touched.
        std::vector<std::string> lightShaftsOptions;
        for ( const char* pLabel : kJonesDisplayLightShaftsLabels )
        {
            lightShaftsOptions.emplace_back(pLabel);
        }
        layout.lightShafts = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayLightShaftsText), std::string(),
            std::move(lightShaftsOptions), true,
            values.lightShafts < STD_ARRAYLEN(kJonesDisplayLightShaftsValues)
                ? values.lightShafts : kJonesDisplayLightShaftsDefault });

        // Added: M9.18. Lens flare adds tinted ghosts and a halo from the visible sky; the HUD,
        // menus and prompts are never touched.
        layout.lensFlare = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayLensFlareText), std::string(), true,
            values.bLensFlare });

        // Added: M9.18. The vignette darkens the corners of the 3D scene.
        layout.vignette = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayVignetteText), std::string(), true,
            values.bVignette });

        // Added: M9.18. Film grain adds deterministic luminance noise to the 3D scene.
        layout.filmGrain = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayFilmGrainText), std::string(), true,
            values.bFilmGrain });

        // Added: M9.17. Water ripples animate water-tagged pixels; the HUD, menus and prompts are
        // never touched.
        layout.waterRipples = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayWaterRipplesText), std::string(), true,
            values.bWaterRipples });

        // Added: M9.14. Depth of field softens the background; cutscenes blur both sides of the
        // focus. The HUD, menus and prompts are never blurred.
        layout.dofCutscenes = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayDofCutscenesText), std::string(), true,
            values.bDofCutscenes });

        layout.dofGameplay = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayDofGameplayText), std::string(), true,
            values.bDofGameplay });

        // Added: M9.9. Contrast-adaptive sharpening restores detail lost to a low render scale or
        // to SMAA; the HUD, menus and prompts are never sharpened.
        std::vector<std::string> sharpeningOptions;
        for ( const char* pLabel : kJonesDisplaySharpeningLabels )
        {
            sharpeningOptions.emplace_back(pLabel);
        }
        layout.sharpening = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplaySharpeningText), std::string(),
            std::move(sharpeningOptions), true,
            values.sharpening < STD_ARRAYLEN(kJonesDisplaySharpeningValues)
                ? values.sharpening : kJonesDisplaySharpeningDefault });

        // Added: M9.10. The operator maps the display-referred scene tone into 0..1; Neutral compresses
        // highlights while leaving midtones, Filmic is the classic ACES curve. Exposure scales the
        // color before the curve.
        std::vector<std::string> toneMappingOptions;
        for ( const char* pLabel : kJonesDisplayToneMappingLabels )
        {
            toneMappingOptions.emplace_back(pLabel);
        }
        layout.toneMapping = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayToneMappingText), std::string(),
            std::move(toneMappingOptions), true,
            values.toneMapping < STD_ARRAYLEN(kJonesDisplayToneMappingValues)
                ? values.toneMapping : kJonesDisplayToneMappingDefault });

        layout.exposure = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Slider{
            JonesUiDisplay_ResolveLabel(kJonesDisplayExposureText), std::string(),
            kJonesDisplayExposureMin, kJonesDisplayExposureMax, kJonesDisplayExposureStep, true,
            std::clamp(values.exposure, kJonesDisplayExposureMin, kJonesDisplayExposureMax) });

        // Added: M9.13. A subtle color-grading preset on the 3D scene after the tone-map operator;
        // Neutral leaves the image exactly as it was.
        std::vector<std::string> colorGradingOptions;
        for ( const char* pLabel : kJonesDisplayColorGradingLabels )
        {
            colorGradingOptions.emplace_back(pLabel);
        }
        layout.colorGrading = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayColorGradingText), std::string(),
            std::move(colorGradingOptions), true,
            values.colorGrading < STD_ARRAYLEN(kJonesDisplayColorGradingValues)
                ? values.colorGrading : kJonesDisplayColorGradingDefault });

        // Added: M9.7. Anisotropic filtering sharpens world and model textures seen at a slant;
        // off keeps the plain linear filter. Crisp pixels drops min/mag filtering to nearest for
        // the original look, while the mip filter follows the Mipmap filter row.
        std::vector<std::string> anisotropicOptions;
        for ( const char* pLabel : kJonesDisplayAnisotropicLabels )
        {
            anisotropicOptions.emplace_back(pLabel);
        }
        layout.anisotropic = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayAnisotropicText), std::string(),
            std::move(anisotropicOptions), true,
            values.anisotropic < STD_ARRAYLEN(kJonesDisplayAnisotropicValues)
                ? values.anisotropic : kJonesDisplayAnisotropicDefault });

        layout.crispPixels = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiDisplay_ResolveLabel(kJonesDisplayCrispPixelsText), std::string(), true,
            values.bCrispPixels });

        // Added: opens the brightness calibration screen; the value is written there, not by
        // this screen's OK.
        layout.brightness = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiDisplay_ResolveLabel(kJonesDisplayBrightnessText),
            kJonesDisplayActionBrightness, true });

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
    model.gameplayFOV = sithCamera_GetGameplayFOV();
    model.renderScale = std::clamp(JonesUiDisplay_ReadConfigFloat(
        JONESDISPLAY_CFG_ENGINE_RENDER_RENDERSCALE, 1.0f), 0.5f, 2.0f);
    model.antiAliasing = JonesUiDisplay_ReadConfigInt(
        JONESDISPLAY_CFG_ENGINE_RENDER_ANTIALIASING, J3D_QOL_VALUE(1, 0));
    model.bloom = std::clamp(JonesUiDisplay_ReadConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_BLOOM,
        J3D_QOL_VALUE(1, 0)), 0, (int)STD_ARRAYLEN(kJonesDisplayBloomValues) - 1);
    model.lightShafts = std::clamp(JonesUiDisplay_ReadConfigInt(
        JONESDISPLAY_CFG_ENGINE_RENDER_LIGHTSHAFTS, J3D_QOL_VALUE(1, 0)), 0,
        (int)STD_ARRAYLEN(kJonesDisplayLightShaftsValues) - 1);
    model.bLensFlare = JonesUiDisplay_ReadConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_LENSFLARE,
        kJonesDisplayLensFlareDefault);
    model.bVignette = JonesUiDisplay_ReadConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_VIGNETTE,
        kJonesDisplayVignetteDefault);
    model.bFilmGrain = JonesUiDisplay_ReadConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_FILMGRAIN,
        kJonesDisplayFilmGrainDefault);
    model.bWaterRipples = JonesUiDisplay_ReadConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_WATER_RIPPLES,
        kJonesDisplayWaterRipplesDefault);
    model.bDofCutscenes = JonesUiDisplay_ReadConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_DOF_CUTSCENES,
        kJonesDisplayDofCutscenesDefault);
    model.bDofGameplay = JonesUiDisplay_ReadConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_DOF_GAMEPLAY,
        kJonesDisplayDofGameplayDefault);
    model.sharpening = std::clamp(JonesUiDisplay_ReadConfigFloat(
        JONESDISPLAY_CFG_ENGINE_RENDER_SHARPENING, J3D_QOL_VALUE(0.3f, 0.0f)), 0.0f, 1.0f);
    model.toneMapping = JonesUiDisplay_ReadConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_TONEMAPPING,
        J3D_QOL_VALUE(1, 0));
    model.exposure = (float)std::clamp((double)JonesUiDisplay_ReadConfigFloat(
        JONESDISPLAY_CFG_ENGINE_RENDER_EXPOSURE, 1.0f), kJonesDisplayExposureMin,
        kJonesDisplayExposureMax);
    model.colorGrading = std::clamp(JonesUiDisplay_ReadConfigInt(
        JONESDISPLAY_CFG_ENGINE_RENDER_COLORGRADING, 0), 0,
        (int)STD_ARRAYLEN(kJonesDisplayColorGradingValues) - 1);
    model.anisotropic = std3DBabylon_ReadAnisotropicFilterLevel();
    model.bCrispPixels = JonesUiDisplay_ReadConfigBool(STD3D_CFG_TEXTUREPIXELATED, false);

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
    values.gameplayFOV = (double)model.gameplayFOV;
    values.renderScale = JonesUiDisplay_RenderScaleIndex(model.renderScale);
    values.antiAliasing = JonesUiDisplay_AntiAliasingIndex(model.antiAliasing);
    values.bloom = JonesUiDisplay_BloomIndex(model.bloom);
    values.lightShafts = JonesUiDisplay_LightShaftsIndex(model.lightShafts);
    values.bLensFlare = model.bLensFlare;
    values.bVignette = model.bVignette;
    values.bFilmGrain = model.bFilmGrain;
    values.bWaterRipples = model.bWaterRipples;
    values.bDofCutscenes = model.bDofCutscenes;
    values.bDofGameplay = model.bDofGameplay;
    values.sharpening = JonesUiDisplay_SharpeningIndex(model.sharpening);
    values.toneMapping = JonesUiDisplay_ToneMappingIndex(model.toneMapping);
    values.exposure = (double)model.exposure;
    values.colorGrading = JonesUiDisplay_ColorGradingIndex(model.colorGrading);
    values.anisotropic = JonesUiDisplay_AnisotropicIndex(model.anisotropic);
    values.bCrispPixels = model.bCrispPixels;
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
    static_cast<void>(JonesUiDisplay_ReadSlider(ui, screen, layout.gameplayFOV,
        values.gameplayFOV));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.renderScale,
        values.renderScale));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.antiAliasing,
        values.antiAliasing));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.bloom,
        values.bloom));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.lightShafts,
        values.lightShafts));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.lensFlare,
        values.bLensFlare));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.vignette,
        values.bVignette));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.filmGrain,
        values.bFilmGrain));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.waterRipples,
        values.bWaterRipples));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.dofCutscenes,
        values.bDofCutscenes));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.dofGameplay,
        values.bDofGameplay));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.sharpening,
        values.sharpening));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.toneMapping,
        values.toneMapping));
    static_cast<void>(JonesUiDisplay_ReadSlider(ui, screen, layout.exposure,
        values.exposure));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.colorGrading,
        values.colorGrading));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.anisotropic,
        values.anisotropic));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.crispPixels,
        values.bCrispPixels));
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

    const float gameplayFOV = (float)std::clamp(values.gameplayFOV,
        (double)SITHCAMERA_GAMEPLAYFOVMIN, (double)SITHCAMERA_GAMEPLAYFOVMAX);
    sithCamera_SetGameplayFOV(gameplayFOV);
    JonesUiDisplay_SetConfigFloat(JONESDISPLAY_CFG_ENGINE_RENDER_FOV, gameplayFOV);

    // Added: M9.4. The scale is read when the display opens; the Options OK already asks for a
    // display restart, the same way the other presentation keys take effect.
    const std::size_t renderScale = std::min(values.renderScale,
        STD_ARRAYLEN(kJonesDisplayRenderScaleValues) - 1);
    JonesUiDisplay_SetConfigFloat(JONESDISPLAY_CFG_ENGINE_RENDER_RENDERSCALE,
        kJonesDisplayRenderScaleValues[renderScale]);

    // Added: M9.8. Read when the frame owner opens, like the render scale.
    const std::size_t antiAliasing = std::min(values.antiAliasing,
        STD_ARRAYLEN(kJonesDisplayAntiAliasingValues) - 1);
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_ANTIALIASING,
        kJonesDisplayAntiAliasingValues[antiAliasing]);

    // Added: M9.12. Read when the frame owner opens, like anti-aliasing.
    const std::size_t bloom = std::min(values.bloom,
        STD_ARRAYLEN(kJonesDisplayBloomValues) - 1);
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_BLOOM,
        kJonesDisplayBloomValues[bloom]);

    // Added: M9.16. Read when the frame owner opens, like bloom.
    const std::size_t lightShafts = std::min(values.lightShafts,
        STD_ARRAYLEN(kJonesDisplayLightShaftsValues) - 1);
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_LIGHTSHAFTS,
        kJonesDisplayLightShaftsValues[lightShafts]);

    // Added: M9.18. Read when the frame owner opens, like bloom.
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_LENSFLARE,
        values.bLensFlare);
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_VIGNETTE,
        values.bVignette);
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_FILMGRAIN,
        values.bFilmGrain);

    // Added: M9.17. Read when the frame owner opens, like bloom.
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_WATER_RIPPLES,
        values.bWaterRipples);

    // Added: M9.14. Read when the frame owner opens, like bloom. The strength key is only read
    // there, so this screen leaves it unchanged.
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_DOF_CUTSCENES,
        values.bDofCutscenes);
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_DOF_GAMEPLAY,
        values.bDofGameplay);

    // Added: M9.9. Read when the frame owner opens, like anti-aliasing.
    const std::size_t sharpening = std::min(values.sharpening,
        STD_ARRAYLEN(kJonesDisplaySharpeningValues) - 1);
    JonesUiDisplay_SetConfigFloat(JONESDISPLAY_CFG_ENGINE_RENDER_SHARPENING,
        kJonesDisplaySharpeningValues[sharpening]);

    // Added: M9.10. Read when the frame owner opens, like the render scale.
    const std::size_t toneMapping = std::min(values.toneMapping,
        STD_ARRAYLEN(kJonesDisplayToneMappingValues) - 1);
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_TONEMAPPING,
        kJonesDisplayToneMappingValues[toneMapping]);
    JonesUiDisplay_SetConfigFloat(JONESDISPLAY_CFG_ENGINE_RENDER_EXPOSURE,
        (float)std::clamp(values.exposure, kJonesDisplayExposureMin,
            kJonesDisplayExposureMax));

    // Added: M9.13. Read when the frame owner opens, like the tone map.
    const std::size_t colorGrading = std::min(values.colorGrading,
        STD_ARRAYLEN(kJonesDisplayColorGradingValues) - 1);
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_COLORGRADING,
        kJonesDisplayColorGradingValues[colorGrading]);

    // Added: M9.7. Both are read by std3D_Open, so they take effect with the same display restart
    // the other presentation rows ask for.
    const std::size_t anisotropic = std::min(values.anisotropic,
        STD_ARRAYLEN(kJonesDisplayAnisotropicValues) - 1);
    JonesUiDisplay_SetConfigInt(STD3D_CFG_ANISOTROPICFILTER,
        kJonesDisplayAnisotropicValues[anisotropic]);
    JonesUiDisplay_SetConfigBool(STD3D_CFG_TEXTUREPIXELATED, values.bCrispPixels);

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
        if ( !pAction || pAction->screen != screenId )
        {
            return false;
        }

        if ( pAction->action == kJonesDisplayActionBrightness )
        {
            // Added: the calibration keeps its own value and closes back to this screen.
            static_cast<void>(JonesUiBrightness_Run(uiRef, pPainter));
            return true;
        }

        if ( pAction->action != kJonesDisplayActionOk )
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
