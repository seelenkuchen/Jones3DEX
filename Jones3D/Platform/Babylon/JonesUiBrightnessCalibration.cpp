// Added: the Babylon column's brightness calibration screen (M9.11); see
// JonesUiBrightnessCalibration.h.
#include <Jones3D/Platform/Babylon/JonesUiBrightnessCalibration.h>

#include <Jones3D/Display/JonesDisplay.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdRenderBabylon.h>
#include <std/General/stdConfig.h>

#include <babylon/ui/Widget.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>

namespace
{
    constexpr int kJonesBrightnessBack    = 2;
    constexpr int kJonesBrightnessDefault = 3;

    constexpr babylon::ui::ActionId kJonesBrightnessActionOk      = 1;
    constexpr babylon::ui::ActionId kJonesBrightnessActionDefault = 3;

    // Added: no JONES_STR key exists for the calibration text yet.
    constexpr const char* kJonesBrightnessTitleText = "Brightness"; // TODO: [COMPAT]
    constexpr const char* kJonesBrightnessHintText =
        "Adjust until the left symbol is barely visible"; // TODO: [COMPAT]
    constexpr const char* kJonesBrightnessValueText = "Brightness"; // TODO: [COMPAT]
    constexpr const char* kJonesBrightnessDefaultText = "Default"; // TODO: [COMPAT]

    constexpr float kJonesBrightnessSceneValues[] = { 0.03f, 0.06f, 0.12f };

    float Saturate(float value)
    {
        return std::clamp(value, 0.0f, 1.0f);
    }

    // Added: Khronos PBR Neutral, the same reference formula as tonemap.frag.hlsl (the game's
    // display-referred scene color omits its constant shadow offset).
    void ToneMapNeutral(float rgb[3])
    {
        const float startCompression = 0.8f - 0.04f;
        const float desaturation     = 0.15f;

        const float peak = std::max(rgb[0], std::max(rgb[1], rgb[2]));
        if ( peak <= startCompression )
        {
            return;
        }

        const float d       = 1.0f - startCompression;
        const float newPeak = 1.0f - d * d / (peak + d - startCompression);
        for ( int cur = 0; cur < 3; ++cur )
        {
            rgb[cur] *= newPeak / peak;
        }

        const float g = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
        for ( int cur = 0; cur < 3; ++cur )
        {
            rgb[cur] = rgb[cur] + (newPeak - rgb[cur]) * g;
        }
    }

    // Added: Narkowicz 2015 ACES fitted curve, the same formula as tonemap.frag.hlsl.
    void ToneMapAces(float rgb[3])
    {
        const float a = 2.51f;
        const float b = 0.03f;
        const float c = 2.43f;
        const float d = 0.59f;
        const float e = 0.14f;
        for ( int cur = 0; cur < 3; ++cur )
        {
            const float color = rgb[cur];
            rgb[cur] = Saturate((color * (a * color + b)) / (color * (c * color + d) + e));
        }
    }

    float LoadBrightness(void)
    {
        if ( !stdConfig_HasStarted() )
        {
            return kJonesUiBrightnessDefault;
        }

        const float value = stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS,
            kJonesUiBrightnessDefault);
        return std::isfinite(value)
            ? std::clamp(value, kJonesUiBrightnessMin, kJonesUiBrightnessMax)
            : kJonesUiBrightnessDefault;
    }

    void StoreBrightness(float value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS,
                std::clamp(value, kJonesUiBrightnessMin, kJonesUiBrightnessMax)));
        }
    }

    // Applies the value to the open frame owner so the calibration previews the 3D view's change.
    void ApplyLive(float value)
    {
        if ( !stdRenderBabylon_IsOpen() )
        {
            return;
        }

        StdRenderBabylonToneMap toneMap = stdRenderBabylon_GetToneMap();
        toneMap.brightness = std::clamp(value, kJonesUiBrightnessMin, kJonesUiBrightnessMax);
        stdRenderBabylon_SetToneMap(toneMap);
    }

    // The dark pattern sits below the rows, inside the transparent Options panel. Its three
    // squares use the final pass's formula with the value being edited.
    void DrawTestPattern(babylon::ui::Painter& painter, const babylon::ui::Size& canvas,
        float exposure, std::uint32_t oper, float brightness)
    {
        const float margin = 16.0f;
        const float width  = std::max(1.0f, std::min(canvas.width - 2.0f * margin, 420.0f));
        const float height = 96.0f;
        const float x      = (canvas.width - width) * 0.5f;
        const float y      = std::max(220.0f, canvas.height - height - 56.0f);

        painter.fillRect(babylon::ui::Rect{ x, y, width, height },
            babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 1.0f });

        const float cell = height * 0.56f;
        const float gap  = (width - 3.0f * cell) * 0.25f;
        std::size_t cur = 0;
        for ( const float sceneValue : kJonesBrightnessSceneValues )
        {
            const babylon::ui::Color color = JonesUiBrightness_MapSceneColor(sceneValue,
                exposure, oper, brightness);
            painter.fillRect(babylon::ui::Rect{ x + gap + (cell + gap) * (float)cur,
                y + (height - cell) * 0.5f, cell, cell }, color);
            ++cur;
        }
    }

    babylon::ui::Screen BuildScreen(float value, std::size_t& slider)
    {
        babylon::ui::Screen screen;
        screen.title = kJonesBrightnessTitleText;

        screen.widgets.emplace_back(babylon::ui::Label{ kJonesBrightnessHintText,
            std::string() });

        slider = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Slider{
            kJonesBrightnessValueText, std::string(),
            (double)kJonesUiBrightnessMin, (double)kJonesUiBrightnessMax,
            (double)kJonesUiBrightnessStep, true, (double)value });

        const std::size_t defaultButton = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            kJonesBrightnessDefaultText, kJonesBrightnessActionDefault, true });

        const std::size_t okButton = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{ "OK", kJonesBrightnessActionOk, true });

        screen.backResult   = kJonesBrightnessBack;
        screen.initialFocus = slider;

        screen.help.assign(screen.widgets.size(), std::string());
        screen.help[slider] = "Set the scene's brightness; 1.0 leaves it unchanged.";
        screen.help[defaultButton] = "Return to the default brightness, 1.0.";
        screen.help[okButton] = "Keep the value and close the calibration.";

        return screen;
    }

    // The overlay draws the pattern after the background and dim layer but before the screen, so
    // it works the same from the first start and from Options. The value is read each frame, so
    // the swatches follow the slider.
    struct PatternOverlay
    {
        JonesUiContext* pContext = nullptr;
        JonesUiModalBackground saved;
        bool bSavedStarted = false;
        bool bHadContext = false;

        PatternOverlay(JonesUiContext* pContext_, babylon::ui::Painter* pPainter, float& value)
            : pContext(pContext_)
        {
            if ( !pContext || !pPainter )
            {
                return;
            }

            bHadContext = true;
            saved = pContext->modalBackground;
            bSavedStarted = pContext->bModalBackgroundStarted;
            const std::function<void()> previous = saved.overlay;
            pContext->bModalBackgroundStarted = true;
            pContext->modalBackground.overlay = [pPainter, previous, this, &value]()
            {
                if ( previous )
                {
                    previous();
                }

                const StdRenderBabylonToneMap toneMap = stdRenderBabylon_GetToneMap();
                const babylon::ui::Size canvas = pContext->pUi->canvasSize();
                DrawTestPattern(*pPainter, canvas, toneMap.exposure, toneMap.oper, value);
            };
        }

        ~PatternOverlay()
        {
            if ( bHadContext )
            {
                pContext->modalBackground = saved;
                pContext->bModalBackgroundStarted = bSavedStarted;
            }
        }
    };
}

babylon::ui::Color JonesUiBrightness_MapSceneColor(float sceneValue, float exposure,
    std::uint32_t oper, float brightness)
{
    const float validExposure = std::isfinite(exposure) && exposure >= 0.0f ? exposure : 1.0f;
    const float validBrightness = std::isfinite(brightness) && brightness > 0.0f
        ? brightness : 1.0f;

    float rgb[3] = { sceneValue * validExposure, sceneValue * validExposure,
        sceneValue * validExposure };
    if ( oper == 1 )
    {
        ToneMapNeutral(rgb);
    }
    else if ( oper == 2 )
    {
        ToneMapAces(rgb);
    }

    for ( int cur = 0; cur < 3; ++cur )
    {
        float value = Saturate(rgb[cur]);
        if ( validBrightness != 1.0f )
        {
            value = std::pow(value, 1.0f / validBrightness);
        }
        rgb[cur] = Saturate(value);
    }

    return babylon::ui::Color{ rgb[0], rgb[1], rgb[2], 1.0f };
}

bool JonesUiBrightness_NeedsCalibration(void)
{
    return stdConfig_HasStarted()
        && !stdConfig_Contains(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS);
}

int JonesUiBrightness_Run(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter)
{
    float value = LoadBrightness();
    ApplyLive(value);

    JonesUiContext* pActive = JonesUiContext_GetActive();
    PatternOverlay overlay(pActive, pPainter, value);

    const JonesUiModalResultMapper mapResult = [](babylon::ui::ResultCode code)
    {
        return (int)code;
    };

    for ( ;; )
    {
        std::size_t slider = 0;
        const babylon::ui::Screen screen = BuildScreen(value, slider);

        const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
            babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
        {
            if ( const babylon::ui::ValueChanged* pChanged =
                    std::get_if<babylon::ui::ValueChanged>(&event) )
            {
                if ( pChanged->screen == screenId && pChanged->widget == slider )
                {
                    if ( const std::optional<babylon::ui::WidgetValue> pWidgetValue =
                            uiRef.widgetValue(screenId, slider) )
                    {
                        if ( std::holds_alternative<double>(*pWidgetValue) )
                        {
                            value = (float)std::get<double>(*pWidgetValue);
                            ApplyLive(value);
                        }
                    }
                }
                return false;
            }

            if ( const babylon::ui::ActionPressed* pAction =
                    std::get_if<babylon::ui::ActionPressed>(&event) )
            {
                if ( pAction->screen == screenId
                    && pAction->action == kJonesBrightnessActionDefault )
                {
                    value = kJonesUiBrightnessDefault;
                    ApplyLive(value);
                    static_cast<void>(uiRef.pop(kJonesBrightnessDefault));
                    return true;
                }
            }

            return false;
        };

        const int result = JonesUiModal_RunHandled(ui, pPainter, screen, mapResult, handler);
        if ( result == kJonesBrightnessDefault )
        {
            // The Default button closes and reopens the screen at 1.0, so the slider shows it
            // without touching the widget's private local value.
            continue;
        }

        StoreBrightness(value);
        return result;
    }
}

int JonesUiBrightness_Show(void)
{
    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless or Null: there is no screen and the key is left unwritten.
        return kJonesBrightnessBack;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return kJonesBrightnessBack;
    }

    const int result = JonesUiBrightness_Run(*pUiContext->pUi, &pUiContext->painter);
    JonesUiContext_Release(pUiContext);
    return result;
}

extern "C" int J3DAPI jonesConfig_ShowBrightnessCalibrationIfUnset(tSysWindow hWnd)
{
    (void)hWnd;

    if ( !JonesUiBrightness_NeedsCalibration() )
    {
        return kJonesBrightnessBack;
    }

    return JonesUiBrightness_Show();
}

#ifdef J3D_TEST
void JonesUiBrightness_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens)
{
    std::size_t slider = 0;
    screens.push_back(BuildScreen(kJonesUiBrightnessDefault, slider));
}
#endif
