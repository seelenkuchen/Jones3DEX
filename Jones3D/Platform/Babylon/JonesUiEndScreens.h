// Added: the Babylon column's Statistics, Level Completed and Game Over screens (ADR-0026), the
// three jonesConfig_* dialogs this slice replaces, plus the store entry point the Level Completed
// screen calls. Each runner pushes its screen through the shared babylon::ui of the column and
// returns the original dialog's value; the view and builder helpers are the test surface.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIENDSCREENS_H
#define JONES3D_PLATFORM_BABYLON_JONESUIENDSCREENS_H
#ifndef __cplusplus
#error "JonesUiEndScreens.h is C++ only: it names babylon::ui and std::string."
#endif

#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Ui.h>

#include <sith/types.h>
#include <sound/types.h>

#include <cstdint>
#include <string>
#include <vector>

// One selectable level of the Statistics screen. The summary is the first entry, as the original
// opened the dialog at the summary end of its scrollbar; the levels follow from the current one
// down to level 0. levelNum is the level whose SithLevelStatistic supplies the rows, and
// JONESLEVEL_BONUSLEVELNUM names the summary (the original's totalLevelStats alias).
struct JonesUiStatisticsRow
{
    std::string optionName;
    std::string levelName;
    std::string timePassed;
    std::string numLevelsFinished;
    std::string treasuresFound;
    std::string hintsSeen;
    std::string difficultyDrop;
    std::string iqPoints;
    int levelNum = 0;
};

// "hours : minutes" with a zero-padded minute below ten and the original's trailing space.
std::string JonesUiEndScreens_FormatTime(uint32_t elapsedTime);

// The original statistics dialog's JONES_STR key for a level's display name. levelIndex 0 is
// Canyonlands (index 1 of JonesMain_aCndLevelLoadInfos) through 16 for Return to Peru.
const char* JonesUiEndScreens_LevelNameKey(int levelIndex);

// Every Statistics row in the order the screen offers them: the summary first and then the levels
// from statistics.curLevelNum down to 0. totalIQPoints is jonesInventory_GetTotalIQPoints(), the
// value the original drew for the summary; a level uses its own SithLevelStatistic::iqPoints.
std::vector<JonesUiStatisticsRow> JonesUiStatistics_BuildView(
    const SithGameStatistics& statistics, int totalIQPoints);

// The Statistics screen before it is pushed, with the summary selected and the rows for it.
babylon::ui::Screen JonesUiStatistics_BuildScreen(const SithGameStatistics& statistics,
    int totalIQPoints);

// Runs the Statistics screen and returns 1 (Close) or 2 (back or quit), like dialog 164.
int JonesUiStatistics_RunStatisticsScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const SithGameStatistics& statistics, int totalIQPoints);

// The Level Completed screen before it is pushed, showing the finished level's time, treasures and
// worth and the IQ points, like dialog 233.
babylon::ui::Screen JonesUiLevelCompleted_BuildScreen(int elapsedTime, int iqPoints,
    int numFoundTreasures, int foundTreasureValue, int totalTreasureValue);

// Runs the Level Completed screen and returns 1 (Close) or 2 (back or quit). Before it returns it
// calls jonesConfig_ShowStoreDialog when the original did (curLevelNum + 1 below the bonus level),
// passing pBalance and apItemsState through unchanged.
int JonesUiLevelCompleted_RunLevelCompletedScreen(babylon::ui::Ui& ui,
    babylon::ui::Painter* pPainter, const SithGameStatistics& statistics, int elapsedTime,
    int iqPoints, int numFoundTreasures, int foundTreasureValue, int totalTreasureValue,
    int* pBalance, int* apItemsState, int unknown2);

// The Game Over screen before it is pushed: Load (1178), Dead (1179) and Restart (1177) in the
// resource's order, Restart disabled when the auto save does not exist like the original init.
babylon::ui::Screen JonesUiGameOver_BuildScreen(bool bRestartEnabled);

// Runs the Game Over screen. pAutoSavePath is the auto save the init would have put in the restore
// buffer; it is written to pOutRestoreFilename first and a successful nested Load replaces it.
// hMusic is played when non-zero and its channel is written to pOutMusicChannel. Returns 1177
// (restart), 1178 (load), 1179 (dead), or the original's data.result value (0 on a plain close).
int JonesUiGameOver_RunGameOverScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const char* pAutoSavePath, bool bRestartEnabled, tSoundHandle hMusic,
    tSoundChannelHandle* pOutMusicChannel, char* pOutRestoreFilename);

// Test surface: the number of times the Level Completed screen called the store entry point.
int JonesUiEndScreens_GetStoreCallCount(void);
void JonesUiEndScreens_ResetStoreCallCount(void);

#endif // JONES3D_PLATFORM_BABYLON_JONESUIENDSCREENS_H
