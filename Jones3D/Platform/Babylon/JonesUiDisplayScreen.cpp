// Added: the row building and read-back of the Display settings screen.
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>

#include <Jones3D/Platform/Babylon/JonesUiContext.h>

#include <sith/Engine/sithCamera.h>

#include <std/General/stdUtil.h>

#include <babylon/ui/Screen.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Jones3D/Platform/Babylon/JonesUiDisplaySettingsInternal.h>

using namespace JonesUiDisplaySettingsInternal;

namespace
{
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

        // Added: M10.2 Lighting: Classic keeps the CPU's baked vertex lighting, Per-pixel adds the
        // camera's dynamic lights per pixel in the lit shader. It applies at once and only when lit
        // faces are on; Retail/QOL-off builds stay Classic.
        std::vector<std::string> lightingOptions;
        for ( const char* pLabel : kJonesDisplayLightingLabels )
        {
            lightingOptions.emplace_back(pLabel);
        }
        layout.lighting = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayLightingText), std::string(),
            std::move(lightingOptions), true,
            values.bPerPixelLighting ? 1u : 0u });

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

        // Added: M9.17b. Ambient occlusion darkens the creases where geometry meets itself; the
        // HUD, menus and prompts are never touched.
        std::vector<std::string> ssaoOptions;
        for ( const char* pLabel : kJonesDisplaySsaoLabels )
        {
            ssaoOptions.emplace_back(pLabel);
        }
        layout.ssao = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplaySsaoText), std::string(),
            std::move(ssaoOptions), true,
            values.ssao < STD_ARRAYLEN(kJonesDisplaySsaoValues)
                ? values.ssao : kJonesDisplaySsaoDefault });

        // Added: M9.19. Motion blur smears moving parts of the 3D scene along the camera's motion;
        // the HUD, menus and prompts are never blurred.
        std::vector<std::string> motionBlurOptions;
        for ( const char* pLabel : kJonesDisplayMotionBlurLabels )
        {
            motionBlurOptions.emplace_back(pLabel);
        }
        layout.motionBlur = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayMotionBlurText), std::string(),
            std::move(motionBlurOptions), true,
            values.motionBlur < STD_ARRAYLEN(kJonesDisplayMotionBlurValues)
                ? values.motionBlur : kJonesDisplayMotionBlurDefault });

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

        // Added: M9.23. V-Sync is the default; a numeric cap presents immediately and the
        // frame loop waits for the next deadline. Unlimited turns V-Sync off with no cap.
        std::vector<std::string> frameLimitOptions;
        for ( const char* pLabel : kJonesDisplayFrameLimitLabels )
        {
            frameLimitOptions.emplace_back(pLabel);
        }
        layout.frameLimit = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiDisplay_ResolveLabel(kJonesDisplayFrameLimitText), std::string(),
            std::move(frameLimitOptions), true,
            values.frameLimit < STD_ARRAYLEN(kJonesDisplayFrameLimitValues)
                ? values.frameLimit : kJonesDisplayFrameLimitDefault });

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
    {
        std::size_t lighting = values.bPerPixelLighting ? 1u : 0u;
        static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.lighting, lighting));
        values.bPerPixelLighting = lighting != 0u;
    }
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
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.ssao,
        values.ssao));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.motionBlur,
        values.motionBlur));
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
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.frameLimit,
        values.frameLimit));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.colorGrading,
        values.colorGrading));
    static_cast<void>(JonesUiDisplay_ReadChoice(ui, screen, layout.anisotropic,
        values.anisotropic));
    static_cast<void>(JonesUiDisplay_ReadToggle(ui, screen, layout.crispPixels,
        values.bCrispPixels));
}
