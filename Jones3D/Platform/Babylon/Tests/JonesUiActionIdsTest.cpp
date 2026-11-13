// Added: the action-id audit of every Babylon column screen. babylon::ui::kNoAction (0) is a
// valid widget action that reports nothing when accepted, so a button or list with it looks
// enabled but does nothing. This walks every screen's widgets and checks that every interactive
// widget carries a non-zero action id.
#include <doctest.h>

#include <vector>

#include <babylon/ui/Screen.h>
#include <babylon/ui/Widget.h>

#include <Jones3D/Platform/Babylon/JonesUiControls.h>
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiEndScreens.h>
#include <Jones3D/Platform/Babylon/JonesUiGameplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiMainMenu.h>
#include <Jones3D/Platform/Babylon/JonesUiOptions.h>
#include <Jones3D/Platform/Babylon/JonesUiPauseMenu.h>
#include <Jones3D/Platform/Babylon/JonesUiSaveLoad.h>
#include <Jones3D/Platform/Babylon/JonesUiSoundSettings.h>
#include <Jones3D/Platform/Babylon/JonesUiStore.h>

#include <sith/types.h>

// This module has no header of its own; only the action-id audit uses its screens.
void JonesUiModalMessage_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens);

namespace
{
    void CheckScreenActions(const babylon::ui::Screen& screen, std::size_t& numInteractive)
    {
        for ( const babylon::ui::Widget& widget : screen.widgets )
        {
            if ( const babylon::ui::Button* pButton = std::get_if<babylon::ui::Button>(&widget) )
            {
                ++numInteractive;
                CHECK_MESSAGE(pButton->action != babylon::ui::kNoAction, screen.title.c_str());
            }
            else if ( const babylon::ui::List* pList = std::get_if<babylon::ui::List>(&widget) )
            {
                ++numInteractive;
                CHECK_MESSAGE(pList->action != babylon::ui::kNoAction, screen.title.c_str());
            }
        }
    }
}

TEST_CASE("every screen's button and list uses a non-zero action id")
{
    std::vector<babylon::ui::Screen> screens;

    JonesUiMainMenu_AppendScreensForTest(screens);
    JonesUiModalMessage_AppendScreensForTest(screens);
    JonesUiSaveLoad_AppendScreensForTest(screens);
    JonesUiStore_AppendScreensForTest(screens);
    JonesUiControls_AppendScreensForTest(screens);
    JonesUiDisplay_AppendScreensForTest(screens);
    JonesUiGameplay_AppendScreensForTest(screens);
    JonesUiSound_AppendScreensForTest(screens);

    screens.push_back(JonesUiPauseMenu_BuildScreen());

    const JonesUiOptionsData optionsData{};
    for ( std::size_t tab = 0; tab < 4; ++tab )
    {
        screens.push_back(JonesUiOptions_BuildTabScreenForTest(tab, optionsData));
    }

    const SithGameStatistics statistics{};
    screens.push_back(JonesUiStatistics_BuildScreen(statistics, 0));
    screens.push_back(JonesUiLevelCompleted_BuildScreen(0, 0, 0, 0, 0));
    screens.push_back(JonesUiGameOver_BuildScreen(true));

    std::size_t numInteractive = 0;
    for ( const babylon::ui::Screen& screen : screens )
    {
        CheckScreenActions(screen, numInteractive);
    }

    // A screen list that lost its builders would otherwise pass without walking anything.
    CHECK(screens.size() >= 16u);
    CHECK(numInteractive >= 40u);
}
