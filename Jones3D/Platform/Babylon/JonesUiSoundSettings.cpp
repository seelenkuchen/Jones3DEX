// Added: the Babylon column's Sound settings screen; see JonesUiSoundSettings.h.
//
// Original behavior, from (git show 82b8f607^:Jones3D/Platform/Win32/jonesConfig.c):
// - jonesConfig_ShowSoundSettingsDialog (line 7938) returns 1 when the dialog's OK is pressed and
//   2 on cancel (EndDialog with the command id).
// - Its OK handler (line 8048) writes pData->maxSoundVolume, pData->b3DHWSupport and
//   pData->bReverseSound and the sound.volume, sound.hw and sound.reverse keys. The volume slider
//   previewed a test sound on WM_HSCROLL; it never changed the master volume, so the volume applies
//   when the caller sets it after OK.
// - The 3D hardware checkbox only enabled when Sound_Has3DHW(), which the Babylon driver never
//   reports. It changes nothing now, so it is not offered and b3DHWSupport keeps its value.
//
// Added: the output-device choice from the M7.5 sound driver. The original dialog had none;
// selecting a device switches the driver on OK and stores its id in engine.audio.outputDevice.
#include <Jones3D/Platform/Babylon/JonesUiSoundSettings.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <sound/DriverOutput.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/std.h>
#include <std/General/stdConfig.h>

#include <babylon/ui/Screen.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr int kJonesDialogOk     = 1;
    constexpr int kJonesDialogCancel = 2;

    constexpr babylon::ui::ActionId kJonesSoundActionOk   = 1;
    constexpr babylon::ui::ActionId kJonesSoundActionBack = 2;

    // Added: the original dialog's captions came from the dialog resource, which has no JONES_STR
    // key. The output device is new in the Babylon column.
    constexpr const char* kJonesSoundTitleText        = "Sound settings"; // TODO: [COMPAT]
    constexpr const char* kJonesSoundVolumeText       = "Volume";         // TODO: [COMPAT]
    constexpr const char* kJonesSoundReverseText      = "Reverse stereo"; // TODO: [COMPAT]
    constexpr const char* kJonesSoundOutputDeviceText = "Output device";  // TODO: [COMPAT]
    constexpr const char* kJonesSoundDefaultText      = "Default";        // TODO: [COMPAT]

    struct JonesUiSoundLayout
    {
        std::size_t volume = 0;
        std::size_t reverse = 0;
        std::size_t outputDevice = 0;
        std::size_t ok = 0;
        std::size_t back = 0;
    };

    struct JonesUiSoundValues
    {
        double volume = 1.0;
        bool bReverse = false;
        std::size_t outputDevice = 0;
    };

    bool JonesUiSound_ReadSlider(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
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

    bool JonesUiSound_ReadToggle(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
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

    bool JonesUiSound_ReadChoice(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
        std::size_t index, std::size_t& value)
    {
        const std::optional<babylon::ui::WidgetValue> pValue = ui.widgetValue(screen, index);
        if ( !pValue || !std::holds_alternative<std::int64_t>(*pValue) )
        {
            return false;
        }

        value = (std::size_t)std::max<std::int64_t>(0, std::get<std::int64_t>(*pValue));
        return true;
    }

    void JonesUiSound_SetConfigFloat(const char* pKey, float value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetFloat(pKey, value));
        }
    }

    void JonesUiSound_SetConfigBool(const char* pKey, bool value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetBool(pKey, value));
        }
    }

    void JonesUiSound_SetConfigString(const char* pKey, const char* pValue)
    {
        if ( stdConfig_HasStarted() && pValue )
        {
            static_cast<void>(stdConfig_SetString(pKey, pValue));
        }
    }
}

void JonesUiSound_CollectOutputDevices(std::vector<JonesUiSoundOutputDevice>& devices)
{
    devices.clear();
    devices.push_back(JonesUiSoundOutputDevice{ std::string(), kJonesSoundDefaultText });

    const std::size_t numDevices = SoundDriver_GetNumOutputDevices();
    for ( std::size_t cur = 0; cur < numDevices; ++cur )
    {
        char aId[SOUNDDRIVER_OUTPUTDEVICE_ID_MAX + 1] = { 0 };
        char aName[SOUNDDRIVER_OUTPUTDEVICE_ID_MAX + 1] = { 0 };
        if ( SoundDriver_GetOutputDevice(cur, aId, sizeof(aId), aName, sizeof(aName)) )
        {
            devices.push_back(JonesUiSoundOutputDevice{ aId, aName });
        }
    }
}

int JonesUiSound_RunSoundScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesSoundSettings* pData)
{
    if ( !pData )
    {
        return kJonesDialogCancel;
    }

    std::vector<JonesUiSoundOutputDevice> devices;
    JonesUiSound_CollectOutputDevices(devices);

    const std::string currentDevice = SoundDriver_GetCurrentOutputDevice();
    std::size_t curDevice = 0;
    for ( std::size_t cur = 0; cur < devices.size(); ++cur )
    {
        if ( devices[cur].id == currentDevice )
        {
            curDevice = cur;
            break;
        }
    }

    babylon::ui::Screen screen;
    screen.title = JonesUiContext_ResolveString(kJonesSoundTitleText);

    JonesUiSoundLayout layout;
    layout.volume = screen.widgets.size();
    screen.widgets.emplace_back(babylon::ui::Slider{
        JonesUiContext_ResolveString(kJonesSoundVolumeText), std::string(), 0.0, 1.0, 0.01, true,
        (double)std::clamp(pData->maxSoundVolume, 0.0f, 1.0f) });

    layout.reverse = screen.widgets.size();
    screen.widgets.emplace_back(babylon::ui::Toggle{
        JonesUiContext_ResolveString(kJonesSoundReverseText), std::string(), true,
        pData->bReverseSound != 0 });

    std::vector<std::string> deviceOptions;
    deviceOptions.reserve(devices.size());
    for ( const JonesUiSoundOutputDevice& device : devices )
    {
        deviceOptions.push_back(device.name);
    }

    layout.outputDevice = screen.widgets.size();
    screen.widgets.emplace_back(babylon::ui::Choice{
        JonesUiContext_ResolveString(kJonesSoundOutputDeviceText), std::string(),
        std::move(deviceOptions), true, curDevice });

    layout.ok = screen.widgets.size();
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("OK"), kJonesSoundActionOk, true });

    layout.back = screen.widgets.size();
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_CANCEL"), kJonesSoundActionBack, true });

    screen.backResult   = kJonesDialogCancel;
    screen.initialFocus = 0;

    JonesUiSoundValues values;
    values.volume       = (double)std::clamp(pData->maxSoundVolume, 0.0f, 1.0f);
    values.bReverse     = pData->bReverseSound != 0;
    values.outputDevice = curDevice;

    bool bApply = false;
    const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
        babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
    {
        const babylon::ui::ActionPressed* pAction =
            std::get_if<babylon::ui::ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId || pAction->action != kJonesSoundActionOk )
        {
            return false;
        }

        // Read the unbound widget values while the screen is still open.
        static_cast<void>(JonesUiSound_ReadSlider(uiRef, screenId, layout.volume, values.volume));
        static_cast<void>(JonesUiSound_ReadToggle(uiRef, screenId, layout.reverse,
            values.bReverse));
        static_cast<void>(JonesUiSound_ReadChoice(uiRef, screenId, layout.outputDevice,
            values.outputDevice));

        bApply = true;
        return false;
    };

    const int result = JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler);
    if ( result != kJonesDialogOk || !bApply )
    {
        return result;
    }

    // The original Sound OK wrote these two fields and their keys. b3DHWSupport has no effect any
    // more and keeps its value; its key is written back unchanged to keep the original write set.
    pData->maxSoundVolume = (float)std::clamp(values.volume, 0.0, 1.0);
    pData->bReverseSound  = values.bReverse ? 1 : 0;

    JonesUiSound_SetConfigFloat(JONESCONFIG_CFG_SOUND_VOLUME, pData->maxSoundVolume);
    JonesUiSound_SetConfigBool(JONESCONFIG_CFG_SOUND_REVERSE, pData->bReverseSound != 0);
    JonesUiSound_SetConfigBool(JONESCONFIG_CFG_SOUND_HW, pData->b3DHWSupport != 0);

    const std::size_t selected = std::min(values.outputDevice, devices.size() - 1);
    const std::string& selectedId = devices[selected].id;
    if ( SoundDriver_SetOutputDevice(selectedId.c_str()) )
    {
        JonesUiSound_SetConfigString(JONESCONFIG_CFG_ENGINE_AUDIO_OUTPUTDEVICE,
            selectedId.c_str());
    }

    return kJonesDialogOk;
}

extern "C" int J3DAPI jonesConfig_ShowSoundSettingsDialog(tSysWindow hWnd, JonesSoundSettings* pData)
{
    (void)hWnd;

    if ( !pData )
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

    const int result = JonesUiSound_RunSoundScreen(*pUiContext->pUi, &pUiContext->painter,
        pData);
    JonesUiContext_Release(pUiContext);
    return result;
}
