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

#include <rdroid/Main/rdroid.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/std3DBabylon.h>
#include <std/Babylon/stdDisplayBabylon.h>
#include <std/Babylon/stdRenderBabylon.h>
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

#include <Jones3D/Platform/Babylon/JonesUiDisplaySettingsInternal.h>

using namespace JonesUiDisplaySettingsInternal;

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
    // Added: M10.2. Retail/QOL-off builds always report Classic, whatever the file says.
    model.bPerPixelLighting = J3D_QOL_VALUE(JonesUiDisplay_ReadConfigBool(
        JONESDISPLAY_CFG_ENGINE_RENDER_PERPIXELLIGHTING, JONESDISPLAY_DEFAULT_PERPIXELLIGHTING) != 0, 0) != 0;
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
    model.ssao = std::clamp(JonesUiDisplay_ReadConfigInt(
        JONESDISPLAY_CFG_ENGINE_RENDER_SSAO, J3D_QOL_VALUE(1, 0)), 0,
        (int)STD_ARRAYLEN(kJonesDisplaySsaoValues) - 1);
    // Added: M11.5. Retail/QOL-off builds always report both shadows off, whatever the file says.
    model.bShadows = J3D_QOL_VALUE(JonesUiDisplay_ReadConfigBool(
        JONESDISPLAY_CFG_ENGINE_RENDER_SHADOWS, kJonesDisplayShadowsDefault) != 0, 0) != 0;
    model.bContactShadows = J3D_QOL_VALUE(JonesUiDisplay_ReadConfigBool(
        JONESDISPLAY_CFG_ENGINE_RENDER_CONTACTSHADOWS, kJonesDisplayContactShadowsDefault) != 0,
        0) != 0;
    model.motionBlur = std::clamp(JonesUiDisplay_ReadConfigInt(
        JONESDISPLAY_CFG_ENGINE_RENDER_MOTIONBLUR, J3D_QOL_VALUE(1, 0)), 0,
        (int)STD_ARRAYLEN(kJonesDisplayMotionBlurValues) - 1);
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
    model.frameLimit = JonesUiDisplay_ReadConfigInt(
        JONESDISPLAY_CFG_ENGINE_RENDER_FRAMELIMIT, 0);

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
    values.bPerPixelLighting = model.bPerPixelLighting;
    values.gameplayFOV = (double)model.gameplayFOV;
    values.renderScale = JonesUiDisplay_RenderScaleIndex(model.renderScale);
    values.antiAliasing = JonesUiDisplay_AntiAliasingIndex(model.antiAliasing);
    values.bloom = JonesUiDisplay_BloomIndex(model.bloom);
    values.lightShafts = JonesUiDisplay_LightShaftsIndex(model.lightShafts);
    values.ssao = JonesUiDisplay_SsaoIndex(model.ssao);
    values.bShadows = model.bShadows;
    values.bContactShadows = model.bContactShadows;
    values.motionBlur = JonesUiDisplay_MotionBlurIndex(model.motionBlur);
    values.bLensFlare = model.bLensFlare;
    values.bVignette = model.bVignette;
    values.bFilmGrain = model.bFilmGrain;
    values.bWaterRipples = model.bWaterRipples;
    values.bDofCutscenes = model.bDofCutscenes;
    values.bDofGameplay = model.bDofGameplay;
    values.sharpening = JonesUiDisplay_SharpeningIndex(model.sharpening);
    values.toneMapping = JonesUiDisplay_ToneMappingIndex(model.toneMapping);
    values.exposure = (double)model.exposure;
    values.frameLimit = JonesUiDisplay_FrameLimitIndex(model.frameLimit);
    values.colorGrading = JonesUiDisplay_ColorGradingIndex(model.colorGrading);
    values.anisotropic = JonesUiDisplay_AnisotropicIndex(model.anisotropic);
    values.bCrispPixels = model.bCrispPixels;
    return model;
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

    // Added: M10.2. Writes the Lighting option and applies it at once; Retail/QOL-off builds stay
    // Classic. It lights only faces that already use the lit path, so lit faces must be on.
    const int perPixelLighting = J3D_QOL_VALUE(values.bPerPixelLighting ? 1 : 0, 0);
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_PERPIXELLIGHTING,
        perPixelLighting != 0);
    rdSetPerPixelLighting(perPixelLighting != 0 && rdGetLitFaces());

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

    // Added: M9.17b. Read when the frame owner opens, like bloom.
    const std::size_t ssao = std::min(values.ssao,
        STD_ARRAYLEN(kJonesDisplaySsaoValues) - 1);
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_SSAO,
        kJonesDisplaySsaoValues[ssao]);

    // Added: M11.5. The keys are read when the frame owner opens; while it is open the setters
    // switch the stages at once. Retail/QOL-off builds keep both off, like the Lighting row.
    const bool bShadows = J3D_QOL_VALUE(values.bShadows ? 1 : 0, 0) != 0;
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_SHADOWS, bShadows);
    const bool bContactShadows = J3D_QOL_VALUE(values.bContactShadows ? 1 : 0, 0) != 0;
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_CONTACTSHADOWS, bContactShadows);
    if ( stdRenderBabylon_IsOpen() )
    {
        static_cast<void>(stdRenderBabylon_SetShadows(bShadows));
        static_cast<void>(stdRenderBabylon_SetContactShadows(bContactShadows));
    }

    // Added: M9.18. Read when the frame owner opens, like bloom.
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_LENSFLARE,
        values.bLensFlare);
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_VIGNETTE,
        values.bVignette);
    JonesUiDisplay_SetConfigBool(JONESDISPLAY_CFG_ENGINE_RENDER_FILMGRAIN,
        values.bFilmGrain);

    // Added: M9.19. Read when the frame owner opens, like bloom.
    const std::size_t motionBlur = std::min(values.motionBlur,
        STD_ARRAYLEN(kJonesDisplayMotionBlurValues) - 1);
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_MOTIONBLUR,
        kJonesDisplayMotionBlurValues[motionBlur]);

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

    // Added: M9.23. Frame pacing applies at once, unlike the scene stages that read their key
    // when the display reopens; the adapter switches the present mode and the limiter.
    const std::size_t frameLimit = std::min(values.frameLimit,
        STD_ARRAYLEN(kJonesDisplayFrameLimitValues) - 1);
    const int frameLimitValue = kJonesDisplayFrameLimitValues[frameLimit];
    JonesUiDisplay_SetConfigInt(JONESDISPLAY_CFG_ENGINE_RENDER_FRAMELIMIT, frameLimitValue);
    stdDisplayBabylon_SetFrameLimit(frameLimitValue);

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
