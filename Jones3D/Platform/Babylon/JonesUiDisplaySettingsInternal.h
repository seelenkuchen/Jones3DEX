// Added: private constants and declarations shared by the split JonesUiDisplaySettings translation units.
#pragma once

#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>

#include <cstddef>
#include <string>

namespace JonesUiDisplaySettingsInternal
{
    inline constexpr int kJonesDialogOk     = 1;
    inline constexpr int kJonesDialogCancel = 2;

    inline constexpr babylon::ui::ActionId kJonesDisplayActionOk   = 1;
    inline constexpr babylon::ui::ActionId kJonesDisplayActionBack = 2;

    // The original dialog's performance radio buttons wrote these values.
    inline constexpr int kJonesDisplayPerformanceValues[] = { 0, 2, 4 };

    // Added: the original dialog took its captions from the dialog resource, which holds no
    // JONES_STR key. The English text is used until one exists.
    inline constexpr const char* kJonesDisplayBitDepthText    = "Bit depth";   // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayResolutionText  = "Resolution";  // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayWindowedText    = "Windowed";    // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayFilterText      = "Mipmap filter"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayFogText         = "Fog";         // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayFogDensityText  = "Fog density"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayPerformanceText = "Performance"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayHiPolyText      = "Hi-poly models"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayLitFacesText    =
        "Lit faces (applies when the display reopens)"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayLightingText = "Lighting"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayFOVText = "Field of view"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayRenderScaleText = "Render scale"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayAntiAliasingText = "Anti-aliasing"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayBloomText = "Bloom"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayLightShaftsText = "Light shafts"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplaySsaoText = "Ambient occlusion"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayShadowsText = "Shadows"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayContactShadowsText = "Contact shadows"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayMotionBlurText = "Motion blur"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayLensFlareText = "Lens flare"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayVignetteText = "Vignette"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayFilmGrainText = "Film grain"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayWaterRipplesText = "Water ripples"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayDofCutscenesText = "Depth of field (cutscenes)"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayDofGameplayText = "Depth of field (gameplay)"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplaySharpeningText = "Sharpening"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayToneMappingText = "Tone mapping"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayExposureText = "Exposure"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayFrameLimitText = "Frame rate limit"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayColorGradingText = "Colour grading"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayAnisotropicText = "Anisotropic filtering"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayCrispPixelsText = "Crisp pixels"; // TODO: [COMPAT]
    inline constexpr const char* kJonesDisplayBrightnessText = "Brightness..."; // TODO: [COMPAT]

    // Added: M9.4 render scale choices. The value is a multiple of the game's 3D resolution; the
    // label is what the Options row shows. 100% (index 3) is the default.
    inline constexpr float kJonesDisplayRenderScaleValues[] = { 0.5f, 0.67f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    inline constexpr const char* kJonesDisplayRenderScaleLabels[] = {
        "50%", "67%", "75%", "100%", "125%", "150%", "200%" };
    inline constexpr std::size_t kJonesDisplayRenderScaleDefault = 3;

    // Added: M9.8 anti-aliasing choices; SMAA (index 1) is the Enhanced default, off in Retail.
    inline constexpr int kJonesDisplayAntiAliasingValues[] = { 0, 1 };
    inline constexpr const char* kJonesDisplayAntiAliasingLabels[] = { "Off", "SMAA" };
    inline constexpr std::size_t kJonesDisplayAntiAliasingDefault = J3D_QOL_VALUE(1, 0);

    // Added: M9.12 bloom choices; Low (1) is the Enhanced default, off in Retail/QOL-off builds.
    inline constexpr int kJonesDisplayBloomValues[] = { 0, 1, 2, 3 };
    inline constexpr const char* kJonesDisplayBloomLabels[] = { "Off", "Low", "Medium", "High" };
    inline constexpr std::size_t kJonesDisplayBloomDefault = J3D_QOL_VALUE(1, 0);

    // Added: M9.16 light-shaft choices; Low (1) is the Enhanced default, off in Retail/QOL-off
    // builds.
    inline constexpr int kJonesDisplayLightShaftsValues[] = { 0, 1, 2, 3 };
    inline constexpr const char* kJonesDisplayLightShaftsLabels[] = { "Off", "Low", "Medium", "High" };
    inline constexpr std::size_t kJonesDisplayLightShaftsDefault = J3D_QOL_VALUE(1, 0);

    // Added: M9.17b ambient-occlusion choices; Low (1) is the Enhanced default, off in
    // Retail/QOL-off builds.
    inline constexpr int kJonesDisplaySsaoValues[] = { 0, 1, 2, 3 };
    inline constexpr const char* kJonesDisplaySsaoLabels[] = { "Off", "Low", "Medium", "High" };
    inline constexpr std::size_t kJonesDisplaySsaoDefault = J3D_QOL_VALUE(1, 0);
    // Added: M9.18 lens flare; on in Enhanced builds and off in Retail/QOL-off ones. The vignette
    // and film grain are off in every build.
    inline constexpr bool kJonesDisplayLensFlareDefault = J3D_QOL_VALUE(1, 0) != 0;
    inline constexpr bool kJonesDisplayVignetteDefault  = false;
    inline constexpr bool kJonesDisplayFilmGrainDefault = false;

    // Added: M11.5 character shadows and contact shadows; on in Enhanced builds and off in
    // Retail/QOL-off ones.
    inline constexpr bool kJonesDisplayShadowsDefault        = J3D_QOL_VALUE(1, 0) != 0;
    inline constexpr bool kJonesDisplayContactShadowsDefault = J3D_QOL_VALUE(1, 0) != 0;

    // Added: M10.2 Lighting choices; Classic (0) is the default everywhere for now and the only
    // one Retail/QOL-off builds accept. Per-pixel needs lit faces.
    inline constexpr const char* kJonesDisplayLightingLabels[] = { "Classic", "Per-pixel" };
    inline constexpr std::size_t kJonesDisplayLightingDefault = J3D_QOL_VALUE(0, 0);

    // Added: M9.19 camera motion-blur choices; Low (1) is the Enhanced default, off in
    // Retail/QOL-off builds.
    inline constexpr int kJonesDisplayMotionBlurValues[] = { 0, 1, 2, 3 };
    inline constexpr const char* kJonesDisplayMotionBlurLabels[] = { "Off", "Low", "Medium", "High" };
    inline constexpr std::size_t kJonesDisplayMotionBlurDefault = J3D_QOL_VALUE(1, 0);

    // Added: M9.17 water ripples; on in Enhanced builds and off in Retail/QOL-off ones.
    inline constexpr bool kJonesDisplayWaterRipplesDefault = J3D_QOL_VALUE(1, 0) != 0;

    // Added: M9.14 depth-of-field rows. Both are on in Enhanced builds and off in Retail/QOL-off
    // ones. The strength has no row; the Options apply path leaves its key as it is.
    inline constexpr bool kJonesDisplayDofCutscenesDefault = J3D_QOL_VALUE(1, 0) != 0;
    inline constexpr bool kJonesDisplayDofGameplayDefault  = J3D_QOL_VALUE(1, 0) != 0;

    // Added: M9.9 contrast-adaptive sharpening choices. Low (0.3) is the Enhanced default, off in
    // Retail/QOL-off builds.
    inline constexpr float kJonesDisplaySharpeningValues[] = { 0.0f, 0.3f, 0.5f, 0.8f };
    inline constexpr const char* kJonesDisplaySharpeningLabels[] = { "Off", "Low", "Medium", "High" };
    inline constexpr std::size_t kJonesDisplaySharpeningDefault = J3D_QOL_VALUE(1, 0);

    // Added: M9.10 tone-mapping operators; Neutral (index 1) is the Enhanced default, clamp in
    // Retail/QOL-off builds.
    inline constexpr int kJonesDisplayToneMappingValues[] = { 0, 1, 2 };
    inline constexpr const char* kJonesDisplayToneMappingLabels[] = { "Clamp", "Neutral", "Filmic" };
    inline constexpr std::size_t kJonesDisplayToneMappingDefault = J3D_QOL_VALUE(1, 0);
    inline constexpr double kJonesDisplayExposureMin = 0.5;
    inline constexpr double kJonesDisplayExposureMax = 2.0;
    inline constexpr double kJonesDisplayExposureStep = 0.1;

    // Added: M9.23 frame pacing. 0 is V-Sync (the default), a positive value
    // caps presentation with V-Sync off, and -1 is unlimited with V-Sync off.
    inline constexpr int kJonesDisplayFrameLimitValues[] = { 0, 30, 60, 120, 144, 240, -1 };
    inline constexpr const char* kJonesDisplayFrameLimitLabels[] = {
        "V-Sync", "30", "60", "120", "144", "240", "Unlimited" };
    inline constexpr std::size_t kJonesDisplayFrameLimitDefault = 0;

    // Added: M9.13 color-grading presets; Neutral (index 0) is the default in every build.
    inline constexpr int kJonesDisplayColorGradingValues[] = { 0, 1, 2, 3, 4, 5 };
    inline constexpr const char* kJonesDisplayColorGradingLabels[] = {
        "Neutral", "Warm", "Cool", "Adventure", "Vivid", "Muted" };
    inline constexpr std::size_t kJonesDisplayColorGradingDefault = 0;

    // Added: M9.7 anisotropic filtering choices; 8x (index 3) is the Enhanced default.
    inline constexpr int kJonesDisplayAnisotropicValues[] = { 1, 2, 4, 8, 16 };
    inline constexpr const char* kJonesDisplayAnisotropicLabels[] = { "Off", "2x", "4x", "8x", "16x" };
    inline constexpr std::size_t kJonesDisplayAnisotropicDefault = 3;

    std::string JonesUiDisplay_ResolveLabel(const char* pText);

    std::string JonesUiDisplay_BitDepthLabel(int bpp);

    const StdDisplayInfo* JonesUiDisplay_GetInfo(const StdDisplayEnvironment* pEnv,
        int displayDeviceNum);

    bool JonesUiDisplay_FindCurrentMode(const StdDisplayEnvironment* pEnv, int displayDeviceNum,
        const JonesDisplaySettings* pSettings, std::size_t& modeNum);

    int JonesUiDisplay_ReadConfigInt(const char* pKey, int defaultValue);
    bool JonesUiDisplay_ReadConfigBool(const char* pKey, bool defaultValue);
    float JonesUiDisplay_ReadConfigFloat(const char* pKey, float defaultValue);

    std::size_t JonesUiDisplay_RenderScaleIndex(float scale);
    std::size_t JonesUiDisplay_AntiAliasingIndex(int value);
    std::size_t JonesUiDisplay_BloomIndex(int level);
    std::size_t JonesUiDisplay_LightShaftsIndex(int level);
    std::size_t JonesUiDisplay_SsaoIndex(int level);
    std::size_t JonesUiDisplay_MotionBlurIndex(int level);
    std::size_t JonesUiDisplay_SharpeningIndex(float strength);
    std::size_t JonesUiDisplay_ToneMappingIndex(int oper);
    std::size_t JonesUiDisplay_ColorGradingIndex(int preset);
    std::size_t JonesUiDisplay_FrameLimitIndex(int frameLimit);
    std::size_t JonesUiDisplay_AnisotropicIndex(int level);

    void JonesUiDisplay_SetConfigInt(const char* pKey, int value);
    void JonesUiDisplay_SetConfigBool(const char* pKey, bool value);
    void JonesUiDisplay_SetConfigFloat(const char* pKey, float value);
    void JonesUiDisplay_SetConfigString(const char* pKey, const char* pValue);
}
