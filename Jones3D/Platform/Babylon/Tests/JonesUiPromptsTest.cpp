// Added: the prompt bar's device and action selection (ADR-0028). The cases use synthetic icon
// ids, so they need no painter, display or game asset.
#include <doctest.h>

#include <Jones3D/Platform/Babylon/JonesUiPrompts.h>

#include <babylon/input/InputState.h>
#include <babylon/platform/Event.h>
#include <babylon/ui/Ui.h>

#include <string>
#include <vector>

namespace
{
    using babylon::input::DeviceClass;
    using babylon::input::LastDevice;
    using babylon::platform::GamepadType;
    using babylon::ui::Prompt;

    JonesUiPromptAssets MakeAssets()
    {
        JonesUiPromptAssets assets;
        assets.keyboard.accept = 10;
        assets.keyboard.back = 11;
        assets.keyboard.changeLeft = 12;
        assets.keyboard.changeRight = 13;
        assets.xbox.accept = 20;
        assets.xbox.back = 21;
        assets.xbox.changeLeft = 22;
        assets.playStation.accept = 30;
        assets.playStation.back = 31;
        assets.playStation.changeLeft = 32;
        assets.nintendo.accept = 40;
        assets.steamDeck.accept = 50;
        return assets;
    }

    std::vector<std::string> Labels(const std::vector<Prompt>& prompts)
    {
        std::vector<std::string> labels;
        for ( const Prompt& prompt : prompts )
        {
            if ( !prompt.label.empty() )
            {
                labels.push_back(prompt.label);
            }
        }
        return labels;
    }
}

TEST_CASE("the prompt icons follow the last used device, with Xbox for unknown pads")
{
    const JonesUiPromptAssets assets = MakeAssets();

    CHECK(JonesUiPrompts_IconsFor(assets, LastDevice{}).accept == 10);
    CHECK(JonesUiPrompts_IconsFor(assets,
        LastDevice{ DeviceClass::Gamepad, GamepadType::Xbox }).accept == 20);
    CHECK(JonesUiPrompts_IconsFor(assets,
        LastDevice{ DeviceClass::Gamepad, GamepadType::PlayStation }).accept == 30);
    CHECK(JonesUiPrompts_IconsFor(assets,
        LastDevice{ DeviceClass::Gamepad, GamepadType::Nintendo }).accept == 40);
    CHECK(JonesUiPrompts_IconsFor(assets,
        LastDevice{ DeviceClass::Gamepad, GamepadType::SteamDeck }).accept == 50);

    // A pad SDL could not identify shows the Xbox set.
    CHECK(JonesUiPrompts_IconsFor(assets,
        LastDevice{ DeviceClass::Gamepad, GamepadType::Unknown }).accept == 20);

    // Switching back to the keyboard picks the keyboard set even though the
    // last pad family is still remembered.
    CHECK(JonesUiPrompts_IconsFor(assets,
        LastDevice{ DeviceClass::KeyboardMouse, GamepadType::PlayStation }).accept == 10);
}

TEST_CASE("the bar lists Select and Back, adds Change and skips Back without one")
{
    JonesUiPromptAssets assets = MakeAssets();
    const LastDevice xbox{ DeviceClass::Gamepad, GamepadType::Xbox };

    const std::vector<Prompt> plain = JonesUiPrompts_Build(assets, xbox,
        /*bHasBack=*/true, /*bChangesValue=*/false, /*bHasTabs=*/false);
    REQUIRE(plain.size() == 2u);
    CHECK(plain[0].icon == 20);
    CHECK(plain[0].label == "Select");
    CHECK(plain[1].icon == 21);
    CHECK(plain[1].label == "Back");

    const std::vector<Prompt> change = JonesUiPrompts_Build(assets, xbox,
        true, /*bChangesValue=*/true, false);
    REQUIRE(change.size() == 3u);
    CHECK(change[2].icon == 22);
    CHECK(change[2].label == "Change");

    const std::vector<Prompt> noBack = JonesUiPrompts_Build(assets, xbox,
        /*bHasBack=*/false, false, false);
    REQUIRE(noBack.size() == 1u);
    CHECK(noBack[0].label == "Select");
    CHECK(Labels(noBack) == std::vector<std::string>{ "Select" });

    // The keyboard's Change has two arrow keys: the labelled left arrow and
    // the unlabelled right arrow.
    const std::vector<Prompt> keyboard = JonesUiPrompts_Build(assets,
        LastDevice{}, true, true, false);
    REQUIRE(keyboard.size() == 4u);
    CHECK(keyboard[2].icon == 12);
    CHECK(keyboard[2].label == "Change");
    CHECK(keyboard[3].icon == 13);
    CHECK(keyboard[3].label.empty());

    // A tabbed screen adds the Tabs entry; the keyboard has a left and a
    // right key, so the second icon is unlabelled.
    assets.keyboard.tabsLeft  = 14;
    assets.keyboard.tabsRight = 15;
    const std::vector<Prompt> tabs = JonesUiPrompts_Build(assets,
        LastDevice{}, true, false, true);
    REQUIRE(tabs.size() == 4u);
    CHECK(tabs[2].icon == 14);
    CHECK(tabs[2].label == "Tabs");
    CHECK(tabs[3].icon == 15);
    CHECK(tabs[3].label.empty());
}
