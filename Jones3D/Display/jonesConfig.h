#ifndef JONES3D_JONESCONFIG_H
#define JONES3D_JONESCONFIG_H
#include <Jones3D/types.h>
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <sith/types.h>

#include <std/General/stdConffile.h>
#include <std/types.h>

J3D_EXTERN_C_START

// Required size of string that is passed to functions that assigns savegame nds file path to it 
// i.e.: jonesConfig_GetSaveGameFilePath,jonesConfig_GetLoadGameFilePath, jonesConfig_ShowGameOverDialog, jonesConfig_ShowExitGameDialog
// The length of passed string argument should be the same as this macro
#define  JONESCONFIG_GAMESAVE_FILEPATHSIZE 128

// Configuration keys
#define JONESCONFIG_CFG_SOURCEPATH          "sourcePath"
#define JONESCONFIG_CFG_DEVMODE             "devMode"
#define JONESCONFIG_CFG_STARTMODE           "startMode"

#define JONESCONFIG_CFG_CONTROLS_CONFIGFILE "controls.configFile"

#define JONESCONFIG_CFG_GAMEPLAY_DEFAULTRUN "gameplay.defaultRun"
#define JONESCONFIG_CFG_GAMEPLAY_STARTLEVEL "gameplay.startLevel"

#define JONESCONFIG_CFG_LOG_MODE            "log.mode"
#define JONESCONFIG_CFG_LOG_LEVEL           "log.level"

#define JONESCONFIG_CFG_SOUND_HW            "sound.hw"
#define JONESCONFIG_CFG_SOUND_REVERSE       "sound.reverse"
#define JONESCONFIG_CFG_SOUND_VOLUME        "sound.volume"
#define JONESCONFIG_CFG_ENGINE_AUDIO_OUTPUTDEVICE "engine.audio.outputDevice" // Added: presentation only (both profiles)

// Added: the result of jonesConfig_ShowMainMenu (ADR-0026).
#define JONES_MAINMENU_QUIT     0
#define JONES_MAINMENU_NEWGAME  1
#define JONES_MAINMENU_LOADGAME 2

int jonesConfig_Startup(void);
void jonesConfig_Shutdown(void);

// Added: frees what the column's dialogs and screens hold on the display, such as the shared
// Babylon ui painter and its textures. Safe to call when nothing is held, and called before the
// display shuts down so the resources are released while the device is still open.
void J3DAPI jonesConfig_ReleaseDisplayResources(void);

int J3DAPI jonesConfig_ShowMessageDialog(tSysWindow hWnd, const char* pTitle, const char* pText, int iconID);

int J3DAPI jonesConfig_GetSaveGameFilePath(tSysWindow hWnd, char* pOutFilePath); // save game dialog
int J3DAPI jonesConfig_GetLoadGameFilePath(tSysWindow hWnd, char* pDestNdsPath); // load game dialog

// Added: maps the configured startMode for this column. The windowed Babylon build has no
// launcher and no developer dialog any more, so a missing startMode or the developer-dialog value
// opens the main menu (JONES_STARTMODE_MAINMENU); a pending --load path still loads directly. The
// Null column and the Babylon headless run return startMode unchanged.
int J3DAPI jonesConfig_ResolveStartMode(int startMode, int bHasStartMode);

// Added: true when this column must run the start-mode screen of startMode only after the display
// and sound are up, because it is an in-game screen instead of a native dialog. The windowed
// Babylon build answers true for the Load, Sound settings and Display settings modes; the Null
// column and the Babylon headless run answer false for every mode and keep their current traces.
int J3DAPI jonesConfig_DefersStartMode(int startMode);

// Added: the windowed Babylon build's main menu (ADR-0026). Shows New Game, Load, Settings and
// Quit until one closes it; on a successful Load it writes the chosen save path to pOutLoadPath.
// Returns one of JONES_MAINMENU_*. pConfig supplies the settings the nested screens edit. The Null
// column answers JONES_MAINMENU_QUIT without blocking.
int J3DAPI jonesConfig_ShowMainMenu(tSysWindow hWnd, JonesState* pConfig, char* pOutLoadPath);

int J3DAPI jonesConfig_ShowGamePlayOptions(tSysWindow hWnd);
void J3DAPI jonesConfig_EnableMouseControl(int bEnable);
int J3DAPI jonesConfig_ShowControlOptions(tSysWindow hWnd);
int J3DAPI jonesConfig_ShowDisplaySettingsDialog(tSysWindow hWnd, StdDisplayEnvironment* pDisplayEnv, JonesDisplaySettings* pDSettings);
int J3DAPI jonesConfig_ShowSoundSettingsDialog(tSysWindow hWnd, JonesSoundSettings* pData);

int J3DAPI jonesConfig_ShowGameOverDialog(tSysWindow hWnd, char* pRestoreFilename, tSoundHandle hSndGameOVerMus, tSoundChannelHandle* pSndChnlMus);
int J3DAPI jonesConfig_ShowStatisticsDialog(tSysWindow hWnd, SithGameStatistics* pStatistics);
int J3DAPI jonesConfig_ShowLevelCompletedDialog(tSysWindow hWnd, int* pBalance, int* apItemsState, int a4, int elapsedTime, int qiPoints, int numFoundTrasures, int foundTrasureValue, int totalTreasureValue);

// Added: the Store screen entry point the Level Completed dialog calls after it closes. The store
// slice fills in the screen; until then it answers 2 (cancel) and leaves the balance and item
// states unchanged.
int J3DAPI jonesConfig_ShowStoreDialog(tSysWindow hWnd, int* pBalance, int* pItemsState, int a4);

int J3DAPI jonesConfig_ShowExitGameDialog(tSysWindow hWnd, char* pSaveGameFilePath);

int J3DAPI jonesConfig_ShowDialogInsertCD(tSysWindow hWnd, intptr_t dwInitParam);

void jonesConfig_UpdateCurrentLevelNum(void); // Updates cached current level num from JonesMain module. 
                                              // Cached level num is used for statistics dialog

void jonesConfig_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // JONES3D_JONESCONFIG_H
