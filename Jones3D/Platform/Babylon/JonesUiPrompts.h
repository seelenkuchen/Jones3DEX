// Added: the button prompts of the Babylon column's menus (ADR-0028). The context loads one icon
// set per device, and the modal loop builds the bar every frame from the focused widget and the
// device the player used last.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIPROMPTS_H
#define JONES3D_PLATFORM_BABYLON_JONESUIPROMPTS_H
#ifndef __cplusplus
#error "JonesUiPrompts.h is C++ only: it names babylon::ui and babylon::input types."
#endif

#include <babylon/input/InputState.h>
#include <babylon/ui/Painter.h>
#include <babylon/ui/Ui.h>

#include <vector>

// One device's prompt icons. accept and back are the South and East buttons by position; change
// and tabs each have a left and a right icon, and the right one is 0 where one icon covers both
// directions. 0 means the file did not load, and the label is drawn alone.
struct JonesUiPromptIcons
{
    babylon::ui::ImageId accept = 0;
    babylon::ui::ImageId back = 0;
    babylon::ui::ImageId changeLeft = 0;
    babylon::ui::ImageId changeRight = 0;
    babylon::ui::ImageId tabsLeft = 0;
    babylon::ui::ImageId tabsRight = 0;
};

// The icon set of every device class. A gamepad whose family SDL could not tell uses the Xbox
// set, as ADR-0028 says.
struct JonesUiPromptAssets
{
    JonesUiPromptIcons keyboard;
    JonesUiPromptIcons xbox;
    JonesUiPromptIcons playStation;
    JonesUiPromptIcons nintendo;
    JonesUiPromptIcons steamDeck;
};

// The icon set for the last used device; Unknown gamepads use Xbox.
const JonesUiPromptIcons& JonesUiPrompts_IconsFor(const JonesUiPromptAssets& assets,
    const babylon::input::LastDevice& device);

// Builds the bar entries in draw order: Select always, Back when the screen has a back result,
// Change when the focused widget takes left/right, and Tabs when the screen has tabs. The labels
// are English because the original dialogs hold no string key for them.
std::vector<babylon::ui::Prompt> JonesUiPrompts_Build(const JonesUiPromptAssets& assets,
    const babylon::input::LastDevice& device, bool bHasBack, bool bChangesValue, bool bHasTabs);

#endif // JONES3D_PLATFORM_BABYLON_JONESUIPROMPTS_H
