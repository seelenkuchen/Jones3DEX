// Added: the button prompts of the Babylon column's menus; see JonesUiPrompts.h.
#include <Jones3D/Platform/Babylon/JonesUiPrompts.h>

#include <string>
#include <utility>

namespace
{
    constexpr const char* kPromptSelect = "Select"; // TODO: [COMPAT] no string key
    constexpr const char* kPromptBack   = "Back";   // TODO: [COMPAT] no string key
    constexpr const char* kPromptChange = "Change"; // TODO: [COMPAT] no string key
    constexpr const char* kPromptTabs   = "Tabs";   // TODO: [COMPAT] no string key

    // Adds the entries of one action. One icon per pad set covers both
    // directions; the keyboard has a separate left and right key, which the
    // bar shows as the labelled icon followed by the unlabelled second one.
    void addAction(std::vector<babylon::ui::Prompt>& prompts, babylon::ui::ImageId left,
        babylon::ui::ImageId right, const char* pLabel)
    {
        prompts.push_back(babylon::ui::Prompt{ left, pLabel });
        if ( right != 0 )
        {
            prompts.push_back(babylon::ui::Prompt{ right, std::string() });
        }
    }
}

const JonesUiPromptIcons& JonesUiPrompts_IconsFor(const JonesUiPromptAssets& assets,
    const babylon::input::LastDevice& device)
{
    if ( device.device == babylon::input::DeviceClass::KeyboardMouse )
    {
        return assets.keyboard;
    }

    switch ( device.gamepadType )
    {
        case babylon::platform::GamepadType::PlayStation:
            return assets.playStation;
        case babylon::platform::GamepadType::Nintendo:
            return assets.nintendo;
        case babylon::platform::GamepadType::SteamDeck:
            return assets.steamDeck;
        case babylon::platform::GamepadType::Xbox:
        case babylon::platform::GamepadType::Unknown:
        default:
            return assets.xbox;
    }
}

std::vector<babylon::ui::Prompt> JonesUiPrompts_Build(const JonesUiPromptAssets& assets,
    const babylon::input::LastDevice& device, bool bHasBack, bool bChangesValue, bool bHasTabs)
{
    const JonesUiPromptIcons& icons = JonesUiPrompts_IconsFor(assets, device);

    std::vector<babylon::ui::Prompt> prompts;
    prompts.reserve(bHasBack ? 4u : 3u);
    addAction(prompts, icons.accept, 0, kPromptSelect);
    if ( bHasBack )
    {
        addAction(prompts, icons.back, 0, kPromptBack);
    }
    if ( bChangesValue )
    {
        addAction(prompts, icons.changeLeft, icons.changeRight, kPromptChange);
    }
    if ( bHasTabs )
    {
        addAction(prompts, icons.tabsLeft, icons.tabsRight, kPromptTabs);
    }
    return prompts;
}
