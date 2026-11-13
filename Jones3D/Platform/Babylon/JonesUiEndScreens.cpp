// Added: the Babylon column's Statistics, Level Completed and Game Over screens; see
// JonesUiEndScreens.h.
//
// Return values and nested order, from the original dialogs
// (git show 82b8f607^:Jones3D/Platform/Win32/jonesConfig.c):
// - jonesConfig_ShowStatisticsDialog (line 8252) answers 2 without statistics. Dialog 164's Close
//   button is id 1 and its WM_CLOSE/wParam 2 path answers 2 (lines 8329-8410, 8598-8611).
// - jonesConfig_ShowLevelCompletedDialog (line 8842) answers the store dialog directly when the
//   statistics are missing or cannot be locked; otherwise it copies *pBalance back after the
//   dialog and unlocks (lines 8842-8867). Its handler (lines 8976-8995) opens the store before it
//   ends for both answers 1 and 2 when curLevelNum + 1 < JONESLEVEL_BONUSLEVELNUM.
// - jonesConfig_ShowGameOverDialog (line 8077) plays the passed music in its init (lines 8187-8217),
//   writes the auto save path to pRestoreFilename, returns data.result and copies the music channel
//   to *pSndChnlMus. Its buttons are 1177 (restart), 1178 (load) and 1179 (dead); Load nests
//   jonesConfig_GetLoadGameFilePath and only closes when it answers 1 (lines 8220-8250).
// - The statistics rows and their keys are dialog 164's (Jones3D/Indy3D.rc at the same revision);
//   the level names are the original jonesConfig_gameStatistics__aLevelNames table (lines
//   227-244).
//
// Callers: JonesHud.c 3291-3307 (Statistics), 4174-4205 (Level Completed) and 4240-4270 (Game
// Over).
#include <Jones3D/Platform/Babylon/JonesUiEndScreens.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesLevel.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>
#include <Jones3D/Platform/Babylon/JonesUiStore.h>
#include <Jones3D/Play/jonesInventory.h>

#include <sith/Dss/sithGamesave.h>
#include <sith/Devices/sithSoundMixer.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithWorld.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonPath.h>
#include <std/General/std.h>
#include <std/General/stdFnames.h>

#include <babylon/files/FileSystem.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr int kJonesDialogClose  = 1;
    constexpr int kJonesDialogCancel = 2;

    constexpr int kJonesGameOverRestart = 1177;
    constexpr int kJonesGameOverLoad    = 1178;
    constexpr int kJonesGameOverDead    = 1179;

    constexpr babylon::ui::ActionId kJonesStatisticsActionClose = 1;

    constexpr std::size_t kJonesStatisticsChoiceIndex = 0;
    constexpr std::size_t kJonesStatisticsLevelIndex  = 1;
    constexpr std::size_t kJonesStatisticsTimeIndex   = 2;
    constexpr std::size_t kJonesStatisticsDoneIndex   = 3;
    constexpr std::size_t kJonesStatisticsTreasureIndex = 4;
    constexpr std::size_t kJonesStatisticsHintIndex     = 5;
    constexpr std::size_t kJonesStatisticsDifficultyIndex = 6;
    constexpr std::size_t kJonesStatisticsIQIndex         = 7;

    // The original's cached current level number, set by jonesConfig_UpdateCurrentLevelNum after a
    // bonus map is bought. The statistics title uses it to pick the current level's page.
    int g_statisticsCurrentLevelNum = 0;

    // The level name keys of the original statistics dialog, indexed like its
    // jonesConfig_gameStatistics__aLevelNames table: entry 0 is level 0 and entry 16 is level 16.
    constexpr const char* kJonesStatisticsLevelKeys[] = {
        "JONES_STR_CYN",
        "JONES_STR_BAB",
        "JONES_STR_RIV",
        "JONES_STR_SHS",
        "JONES_STR_LAG",
        "JONES_STR_VOL",
        "JONES_STR_TEM",
        "JONES_STR_JEP",
        "JONES_STR_TEO",
        "JONES_STR_OLV",
        "JONES_STR_SEA",
        "JONES_STR_PYR",
        "JONES_STR_SOL",
        "JONES_STR_NUB",
        "JONES_STR_INF",
        "JONES_STR_AET",
        "JONES_STR_PRU",
    };

    static_assert(STD_ARRAYLEN(kJonesStatisticsLevelKeys) == JONESLEVEL_LASTLEVELNUM + 1,
        "one level name for every playable level");

    int g_storeCallCount = 0;

    std::string JonesUiEndScreens_Number(int value)
    {
        return std::to_string(value) + " ";
    }

    std::string JonesUiEndScreens_LevelName(int levelNum)
    {
        if ( g_statisticsCurrentLevelNum > 0 && levelNum == g_statisticsCurrentLevelNum )
        {
            // The original shows the last level's name for the current level's page.
            return JonesUiContext_ResolveString(
                kJonesStatisticsLevelKeys[JONESLEVEL_LASTLEVELNUM]);
        }

        return JonesUiContext_ResolveString(JonesUiEndScreens_LevelNameKey(levelNum));
    }

    JonesUiStatisticsRow JonesUiEndScreens_MakeStatisticsRow(
        const SithGameStatistics& statistics, const SithLevelStatistic& levelStatistics,
        int levelNum, bool bSummary, int totalIQPoints)
    {
        JonesUiStatisticsRow row;
        row.levelNum = levelNum;

        row.levelName = bSummary ? JonesUiContext_ResolveString("JONES_STR_SUMMARY")
                                 : JonesUiEndScreens_LevelName(levelNum);
        row.optionName = row.levelName;

        row.timePassed        = JonesUiEndScreens_FormatTime(levelStatistics.elapsedTime);
        row.numLevelsFinished = JonesUiEndScreens_Number(statistics.curLevelNum);
        row.treasuresFound    = JonesUiEndScreens_Number((int)levelStatistics.numFoundTreasures);
        row.hintsSeen         = JonesUiEndScreens_Number((int)levelStatistics.numSeenHints);
        row.difficultyDrop    = JonesUiEndScreens_Number(levelStatistics.difficultyPenalty);
        row.iqPoints          = JonesUiEndScreens_Number(bSummary ? totalIQPoints
                                                                  : levelStatistics.iqPoints);
        return row;
    }

    void JonesUiEndScreens_AppendStatisticsRow(babylon::ui::Screen& screen,
        const JonesUiStatisticsRow& row)
    {
        screen.widgets.emplace_back(babylon::ui::Label{ row.levelName, std::string() });
        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString("JONES_STR_TIME_PASSED"), row.timePassed });
        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString("JONES_STR_CHS_DONE"), row.numLevelsFinished });
        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString("JONES_STR_TREASURES"), row.treasuresFound });
        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString("JONES_STR_HINTS_SEEN"), row.hintsSeen });
        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString("JONES_STR_DFCLTYDROP"), row.difficultyDrop });
        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString("JONES_STR_LEVELIQ"), row.iqPoints });
    }

    void JonesUiEndScreens_SetStatisticsRow(babylon::ui::Ui& ui,
        babylon::ui::ScreenId screen, const JonesUiStatisticsRow& row)
    {
        static_cast<void>(ui.setWidget(screen, kJonesStatisticsLevelIndex,
            babylon::ui::Label{ row.levelName, std::string() }));
        static_cast<void>(ui.setWidget(screen, kJonesStatisticsTimeIndex,
            babylon::ui::Label{ JonesUiContext_ResolveString("JONES_STR_TIME_PASSED"),
                row.timePassed }));
        static_cast<void>(ui.setWidget(screen, kJonesStatisticsDoneIndex,
            babylon::ui::Label{ JonesUiContext_ResolveString("JONES_STR_CHS_DONE"),
                row.numLevelsFinished }));
        static_cast<void>(ui.setWidget(screen, kJonesStatisticsTreasureIndex,
            babylon::ui::Label{ JonesUiContext_ResolveString("JONES_STR_TREASURES"),
                row.treasuresFound }));
        static_cast<void>(ui.setWidget(screen, kJonesStatisticsHintIndex,
            babylon::ui::Label{ JonesUiContext_ResolveString("JONES_STR_HINTS_SEEN"),
                row.hintsSeen }));
        static_cast<void>(ui.setWidget(screen, kJonesStatisticsDifficultyIndex,
            babylon::ui::Label{ JonesUiContext_ResolveString("JONES_STR_DFCLTYDROP"),
                row.difficultyDrop }));
        static_cast<void>(ui.setWidget(screen, kJonesStatisticsIQIndex,
            babylon::ui::Label{ JonesUiContext_ResolveString("JONES_STR_LEVELIQ"), row.iqPoints }));
    }

    void JonesUiEndScreens_CopyString(char* pDest, std::size_t destSize, const char* pSrc)
    {
        if ( !pDest || !destSize )
        {
            return;
        }

        const std::size_t length = pSrc ? std::strlen(pSrc) : 0;
        const std::size_t copied = length < destSize - 1 ? length : destSize - 1;
        if ( copied )
        {
            std::memcpy(pDest, pSrc, copied);
        }

        pDest[copied] = '\0';
    }

    std::string JonesUiEndScreens_JoinPath(const char* pDir, const char* pName)
    {
        std::string path = pDir ? pDir : "";
        if ( !path.empty() && path.back() != '\\' && path.back() != '/' )
        {
            path += '\\';
        }

        path += pName ? pName : "";
        return path;
    }

    // The auto save the original init looked up: the auto-save prefix and the current world's save
    // name with the .nds extension, resolved in the save directory. Empty when there is no world or
    // the file does not exist, which is also the original's "disable Restart" condition.
    std::string JonesUiEndScreens_GetAutoSavePath(StdBabylonContext& rContext)
    {
        if ( !sithWorld_g_pCurrentWorld || !rContext.pFileSystem )
        {
            return std::string();
        }

        const char* pSaveName = sithGetCurrentWorldSaveName();
        const char* pPrefix   = sithGetAutoSaveFilePrefix();

        char aFilename[128] = { 0 };
        std::snprintf(aFilename, sizeof(aFilename), "%s%s", pPrefix ? pPrefix : "",
            pSaveName ? pSaveName : "");
        stdFnames_ChangeExtEx(aFilename, STD_ARRAYLEN(aFilename), "nds");

        const std::string path = JonesUiEndScreens_JoinPath(sithGetSaveGamesDir(), aFilename);
        std::string resolved;
        if ( !stdBabylonPath_Resolve(*rContext.pFileSystem, path.c_str(), resolved)
            || !rContext.pFileSystem->exists(resolved) )
        {
            return std::string();
        }

        return path;
    }

    // The end screens share one look (ADR-0028): a Cinzel title and Jost rows at 16 canvas units,
    // in the boxless Options layout without tabs or help.
    babylon::ui::Skin JonesUiEndScreens_CreateSkin(JonesUiContext& rUiContext,
        StdBabylonContext& rContext)
    {
        constexpr const char* kTitleFontFile = "fonts/Cinzel[wght].ttf";
        constexpr const char* kRowFontFile   = "fonts/Jost[wght].ttf";
        constexpr float kTitleFontSize       = 22.0f;
        constexpr float kRowFontSize         = 16.0f;

        babylon::ui::FontId titleFont = rUiContext.painter.registerTtfFontFile(
            *rContext.pFileSystem, kTitleFontFile, kTitleFontSize);
        if ( titleFont == 0 )
        {
            STDLOG_WARNING("End screen: '%s' unavailable, using the rdFont fallback.\n",
                kTitleFontFile);
            titleFont = rUiContext.font;
        }

        babylon::ui::FontId rowFont = rUiContext.painter.registerTtfFontFile(
            *rContext.pFileSystem, kRowFontFile, kRowFontSize);
        if ( rowFont == 0 )
        {
            STDLOG_WARNING("End screen: '%s' unavailable, using the rdFont fallback.\n",
                kRowFontFile);
            rowFont = rUiContext.font;
        }

        return JonesUiSkin_CreateEndScreen(titleFont, rowFont, &rUiContext.painter);
    }
}

const char* JonesUiEndScreens_LevelNameKey(int levelIndex)
{
    const int index = levelIndex < 0 ? 0
        : levelIndex > JONESLEVEL_LASTLEVELNUM ? JONESLEVEL_LASTLEVELNUM
                                               : levelIndex;
    return kJonesStatisticsLevelKeys[index];
}

std::string JonesUiEndScreens_FormatTime(uint32_t elapsedTime)
{
    const unsigned int hours   = elapsedTime >> 8;
    const unsigned int minutes = elapsedTime & 0xFFu;

    char aText[32] = { 0 };
    if ( minutes >= 10 )
    {
        std::snprintf(aText, sizeof(aText), "%u : %u ", hours, minutes);
    }
    else
    {
        std::snprintf(aText, sizeof(aText), "%u : 0%u ", hours, minutes);
    }

    return std::string(aText);
}

std::vector<JonesUiStatisticsRow> JonesUiStatistics_BuildView(
    const SithGameStatistics& statistics, int totalIQPoints)
{
    std::vector<JonesUiStatisticsRow> rows;

    // The summary is first so the Choice opens where the original scrollbar did, at the total.
    rows.push_back(JonesUiEndScreens_MakeStatisticsRow(statistics, statistics.totalLevelStats,
        JONESLEVEL_BONUSLEVELNUM, true, totalIQPoints));

    int highestLevel = statistics.curLevelNum;
    if ( highestLevel < 0 )
    {
        highestLevel = 0;
    }
    if ( highestLevel > JONESLEVEL_LASTLEVELNUM )
    {
        highestLevel = JONESLEVEL_LASTLEVELNUM;
    }

    for ( int levelNum = highestLevel; levelNum >= 0; --levelNum )
    {
        rows.push_back(JonesUiEndScreens_MakeStatisticsRow(statistics,
            statistics.aLevelStatistic[levelNum], levelNum, false, totalIQPoints));
    }

    return rows;
}

babylon::ui::Screen JonesUiStatistics_BuildScreen(const SithGameStatistics& statistics,
    int totalIQPoints)
{
    const std::vector<JonesUiStatisticsRow> rows =
        JonesUiStatistics_BuildView(statistics, totalIQPoints);

    std::vector<std::string> options;
    options.reserve(rows.size());
    for ( const JonesUiStatisticsRow& row : rows )
    {
        options.push_back(row.optionName);
    }

    babylon::ui::Screen screen;
    screen.title = JonesUiContext_ResolveString("JONES_STR_IQ");
    // No game string key holds a plain "Level" or "Chapter" label for this row.
    screen.widgets.emplace_back(babylon::ui::Choice{
        "Chapter", std::string(), std::move(options), true }); // TODO: [COMPAT] no JONES_STR key
    JonesUiEndScreens_AppendStatisticsRow(screen, rows.front());
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_CLOSE"), kJonesStatisticsActionClose, true });
    screen.backResult   = kJonesDialogCancel;
    screen.initialFocus = kJonesStatisticsChoiceIndex;
    return screen;
}

int JonesUiStatistics_RunStatisticsScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const SithGameStatistics& statistics, int totalIQPoints)
{
    const std::vector<JonesUiStatisticsRow> rows =
        JonesUiStatistics_BuildView(statistics, totalIQPoints);
    const babylon::ui::Screen screen = JonesUiStatistics_BuildScreen(statistics, totalIQPoints);

    JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef, babylon::ui::ScreenId screenId,
        const babylon::ui::UiEvent& event) -> bool
    {
        const babylon::ui::ValueChanged* pChanged =
            std::get_if<babylon::ui::ValueChanged>(&event);
        if ( !pChanged || pChanged->screen != screenId
            || pChanged->widget != kJonesStatisticsChoiceIndex )
        {
            return false;
        }

        const std::int64_t* pSelected = std::get_if<std::int64_t>(&pChanged->value);
        if ( !pSelected || *pSelected < 0 || (std::size_t)*pSelected >= rows.size() )
        {
            return true;
        }

        JonesUiEndScreens_SetStatisticsRow(uiRef, screenId, rows[(std::size_t)*pSelected]);
        return true;
    };

    return JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler);
}

babylon::ui::Screen JonesUiLevelCompleted_BuildScreen(int elapsedTime, int iqPoints,
    int numFoundTreasures, int foundTreasureValue, int totalTreasureValue)
{
    babylon::ui::Screen screen;
    screen.title = JonesUiContext_ResolveString("JONES_STR_CHPDONE");

    screen.widgets.emplace_back(babylon::ui::Label{
        JonesUiContext_ResolveString("JONES_STR_TIME_PASSED"),
        JonesUiEndScreens_FormatTime((uint32_t)elapsedTime) });
    screen.widgets.emplace_back(babylon::ui::Label{
        JonesUiContext_ResolveString("JONES_STR_TREASURES"),
        JonesUiEndScreens_Number(numFoundTreasures) });
    screen.widgets.emplace_back(babylon::ui::Label{
        JonesUiContext_ResolveString("JONES_STR_WORTH"),
        JonesUiEndScreens_Number(foundTreasureValue) });
    screen.widgets.emplace_back(babylon::ui::Label{
        JonesUiContext_ResolveString("JONES_STR_ALLWORTH"),
        JonesUiEndScreens_Number(totalTreasureValue) });
    screen.widgets.emplace_back(babylon::ui::Label{
        JonesUiContext_ResolveString("JONES_STR_LEVELIQ"),
        JonesUiEndScreens_Number(iqPoints) });
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_CLOSE"), kJonesDialogClose, true });

    screen.backResult = kJonesDialogCancel;
    return screen;
}

int JonesUiLevelCompleted_RunLevelCompletedScreen(babylon::ui::Ui& ui,
    babylon::ui::Painter* pPainter, const SithGameStatistics& statistics, int elapsedTime,
    int iqPoints, int numFoundTreasures, int foundTreasureValue, int totalTreasureValue,
    int* pBalance, int* apItemsState, int unknown2)
{
    const babylon::ui::Screen screen = JonesUiLevelCompleted_BuildScreen(elapsedTime, iqPoints,
        numFoundTreasures, foundTreasureValue, totalTreasureValue);

    bool bStoreOpened = false;
    const auto openStore = [&]()
    {
        if ( bStoreOpened || statistics.curLevelNum + 1 >= JONESLEVEL_BONUSLEVELNUM )
        {
            return;
        }

        bStoreOpened = true;
        static_cast<void>(jonesConfig_ShowStoreDialog(NULL, pBalance, apItemsState, unknown2));
    };

    JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef, babylon::ui::ScreenId screenId,
        const babylon::ui::UiEvent& event) -> bool
    {
        const babylon::ui::ActionPressed* pAction =
            std::get_if<babylon::ui::ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId
            || pAction->action != kJonesDialogClose )
        {
            return false;
        }

        // The original opens the store before it ends the completed screen, so it stacks on it.
        openStore();
        static_cast<void>(uiRef.pop(kJonesDialogClose));
        return true;
    };

    const int result = JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler);

    // A back or quit closes the screen without the button handler; the original still opened the
    // store for that answer.
    openStore();
    return result;
}

babylon::ui::Screen JonesUiGameOver_BuildScreen(bool bRestartEnabled)
{
    babylon::ui::Screen screen;
    screen.title = JonesUiContext_ResolveString("JONES_STR_DEATH");

    // Dialog 150's buttons, top to bottom.
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_LOAD"), kJonesGameOverLoad, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_DEAD"), kJonesGameOverDead, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_RESTART"), kJonesGameOverRestart,
        bRestartEnabled });

    screen.backResult   = 0;
    screen.initialFocus = 0;
    return screen;
}

int JonesUiGameOver_RunGameOverScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const char* pAutoSavePath, bool bRestartEnabled, tSoundHandle hMusic,
    tSoundChannelHandle* pOutMusicChannel, char* pOutRestoreFilename)
{
    if ( pOutRestoreFilename )
    {
        JonesUiEndScreens_CopyString(pOutRestoreFilename, JONESCONFIG_GAMESAVE_FILEPATHSIZE,
            pAutoSavePath);
    }

    tSoundChannelHandle musicChannel = 0;
    if ( hMusic )
    {
        musicChannel = sithSoundMixer_PlaySound(hMusic, 1.0f, 0.0f, (SoundPlayFlag)0);
    }

    const babylon::ui::Screen screen = JonesUiGameOver_BuildScreen(bRestartEnabled);

    bool bLoadAttempted = false;
    JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef, babylon::ui::ScreenId screenId,
        const babylon::ui::UiEvent& event) -> bool
    {
        const babylon::ui::ActionPressed* pAction =
            std::get_if<babylon::ui::ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId
            || pAction->action != kJonesGameOverLoad )
        {
            return false;
        }

        bLoadAttempted = true;
        if ( pOutRestoreFilename
            && jonesConfig_GetLoadGameFilePath(NULL, pOutRestoreFilename) == 1 )
        {
            static_cast<void>(uiRef.pop(kJonesGameOverLoad));
        }

        return true;
    };

    int result = JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler);

    // The original set data.result to 1178 before it opened the load screen, so a later close kept
    // that value even though the load was cancelled.
    if ( result == 0 && bLoadAttempted )
    {
        result = kJonesGameOverLoad;
    }

    if ( pOutMusicChannel )
    {
        *pOutMusicChannel = musicChannel;
    }

    return result;
}

int JonesUiEndScreens_GetStoreCallCount(void)
{
    return g_storeCallCount;
}

void JonesUiEndScreens_ResetStoreCallCount(void)
{
    g_storeCallCount = 0;
}

// Added: the Store screen entry point. The screen itself is in JonesUiStore.cpp; this wrapper
// keeps the original's headless behavior (cancel without touching the outputs) and the call count
// the Level Completed tests use.
extern "C" int J3DAPI jonesConfig_ShowStoreDialog(tSysWindow hWnd, int* pBalance,
    int* pItemsState, int a4)
{
    (void)hWnd;

    ++g_storeCallCount;

    if ( !pBalance || !pItemsState )
    {
        return kJonesDialogCancel;
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless: the Null stub answers cancel without blocking.
        return kJonesDialogCancel;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return kJonesDialogCancel;
    }

    const int result = JonesUiStore_RunStoreScreen(*pUiContext->pUi,
        &pUiContext->painter, pBalance, pItemsState, a4);
    JonesUiContext_Release(pUiContext);
    return result;
}

extern "C" int J3DAPI jonesConfig_ShowStatisticsDialog(tSysWindow hWnd,
    SithGameStatistics* pStatistics)
{
    (void)hWnd;

    if ( !pStatistics )
    {
        return kJonesDialogCancel;
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless: the Null stub answers cancel without blocking.
        return kJonesDialogCancel;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return kJonesDialogCancel;
    }

    const babylon::ui::Skin previousSkin = pUiContext->pUi->skin();
    static_cast<void>(pUiContext->pUi->setSkin(
        JonesUiEndScreens_CreateSkin(*pUiContext, *pContext)));

    const int result = JonesUiStatistics_RunStatisticsScreen(*pUiContext->pUi,
        &pUiContext->painter, *pStatistics, jonesInventory_GetTotalIQPoints());

    static_cast<void>(pUiContext->pUi->setSkin(previousSkin));
    JonesUiContext_Release(pUiContext);
    return result;
}

extern "C" int J3DAPI jonesConfig_ShowLevelCompletedDialog(tSysWindow hWnd, int* pBalance,
    int* apItemsState, int a4, int elapsedTime, int qiPoints, int numFoundTrasures,
    int foundTrasureValue, int totalTreasureValue)
{
    (void)hWnd;

    if ( !pBalance || !apItemsState )
    {
        return kJonesDialogCancel;
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless: the Null stub answers cancel without blocking.
        return kJonesDialogCancel;
    }

    SithGameStatistics* pStatistics = sithGamesave_GetGameStatistics();
    if ( !pStatistics || !sithGamesave_LockGameStatistics() )
    {
        return jonesConfig_ShowStoreDialog(hWnd, pBalance, apItemsState, a4);
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        sithGamesave_UnlockGameStatistics();
        return kJonesDialogCancel;
    }

    const babylon::ui::Skin previousSkin = pUiContext->pUi->skin();
    static_cast<void>(pUiContext->pUi->setSkin(
        JonesUiEndScreens_CreateSkin(*pUiContext, *pContext)));

    const int result = JonesUiLevelCompleted_RunLevelCompletedScreen(*pUiContext->pUi,
        &pUiContext->painter, *pStatistics, elapsedTime, qiPoints, numFoundTrasures,
        foundTrasureValue, totalTreasureValue, pBalance, apItemsState, a4);

    static_cast<void>(pUiContext->pUi->setSkin(previousSkin));
    sithGamesave_UnlockGameStatistics();
    return result;
}

extern "C" int J3DAPI jonesConfig_ShowGameOverDialog(tSysWindow hWnd, char* pRestoreFilename,
    tSoundHandle hSndGameOVerMus, tSoundChannelHandle* pSndChnlMus)
{
    (void)hWnd;

    if ( !pRestoreFilename )
    {
        return kJonesDialogCancel;
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless: the Null stub answers cancel without blocking.
        return kJonesDialogCancel;
    }

    const std::string autoSavePath = JonesUiEndScreens_GetAutoSavePath(*pContext);
    JonesUiEndScreens_CopyString(pRestoreFilename, JONESCONFIG_GAMESAVE_FILEPATHSIZE,
        autoSavePath.c_str());

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return kJonesDialogCancel;
    }

    const babylon::ui::Skin previousSkin = pUiContext->pUi->skin();
    static_cast<void>(pUiContext->pUi->setSkin(
        JonesUiEndScreens_CreateSkin(*pUiContext, *pContext)));

    const int result = JonesUiGameOver_RunGameOverScreen(*pUiContext->pUi,
        &pUiContext->painter, autoSavePath.c_str(), !autoSavePath.empty(), hSndGameOVerMus,
        pSndChnlMus, pRestoreFilename);

    static_cast<void>(pUiContext->pUi->setSkin(previousSkin));
    JonesUiContext_Release(pUiContext);
    return result;
}

extern "C" void J3DAPI jonesConfig_UpdateCurrentLevelNum(void)
{
    // Added: the cache the statistics screen reads for the current level's page.
    g_statisticsCurrentLevelNum = (int)JonesMain_GetCurrentLevelNum();
}
