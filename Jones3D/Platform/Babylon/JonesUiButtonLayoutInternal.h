// Added: private declarations shared by the split JonesUiButtonLayout translation units.
#pragma once

#include <Jones3D/Platform/Babylon/JonesUiButtonLayout.h>

#include <babylon/files/FileSystem.h>
#include <babylon/input/InputState.h>
#include <babylon/platform/Keys.h>
#include <babylon/ui/Ui.h>
#include <babylon/uirender/RenderPainter.h>

#include <cstddef>
#include <string>

namespace JonesUiButtonLayoutInternal
{
    inline constexpr const char* kTitleText          = "BUTTON LAYOUT";       // TODO: [COMPAT]
    inline constexpr const char* kResetText          = "Reset to defaults";   // TODO: [COMPAT]

    std::string ActionName(JonesControlActions action);
    std::string CapActionName(JonesControlActions action);
    std::string ControlName(std::size_t controlId);

    std::size_t ControllerControlId(babylon::platform::GamepadButton button);
    std::size_t AxisControlId(babylon::platform::GamepadAxis axis, int direction);

    std::string ControllerIconPath(std::size_t controlId, const char* pSet);
    std::string KeyboardIconPath(std::size_t controlId);
    const char* PromptSet(const babylon::input::LastDevice& device);

    bool EnsureCustomScheme(JonesUiButtonLayoutModel& model, std::size_t& schemeIndex);

    JonesUiButtonLayoutAssets LoadAssets(babylon::uirender::RenderPainter& painter,
        babylon::files::FileSystem& files, const JonesControlsScheme& scheme,
        JonesUiButtonLayoutDevice device, const char* pSet);

    babylon::ui::Rect RowRect(const babylon::ui::Size& canvas, std::size_t visualRow);
    babylon::ui::Rect ResetRowRect(const babylon::ui::Size& canvas);
}
