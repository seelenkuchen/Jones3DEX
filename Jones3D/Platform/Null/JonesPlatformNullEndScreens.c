// Added: the Null column's Statistics, Level Completed and Game Over dialog stubs, split out of
// JonesPlatformNull.c so the Babylon column can provide its in-game screens for exactly those
// three and keep the other Null stubs (Jones3D/Platform/Babylon/JonesUiEndScreens.cpp). The Null
// column and the Babylon headless runner keep these; the windowed Babylon game does not compile
// this file.
#include <j3dcore/j3d.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>
#include <Jones3D/types.h>

int J3DAPI jonesConfig_ShowGameOverDialog(tSysWindow hWnd, char* pRestoreFilename, tSoundHandle hSndGameOVerMus, tSoundChannelHandle* pSndChnlMus)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pRestoreFilename);
    J3D_UNUSED(hSndGameOVerMus);
    J3D_UNUSED(pSndChnlMus);
    // Altered: Buttons are 1177 ("restart") and 1178 ("load game"); anything else
    // (including the EndDialog(hwnd, 2) cancel of the Windows dialog) just stops the
    // died-music channel, which is the natural Null answer.
    return JONESPLATFORM_NULL_IDCANCEL;
}

int J3DAPI jonesConfig_ShowLevelCompletedDialog(tSysWindow hWnd, int* pBalance, int* apItemsState, int a4, int elapsedTime, int qiPoints, int numFoundTrasures, int foundTrasureValue, int totalTreasureValue)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pBalance);
    J3D_UNUSED(apItemsState);
    J3D_UNUSED(a4);
    J3D_UNUSED(elapsedTime);
    J3D_UNUSED(qiPoints);
    J3D_UNUSED(numFoundTrasures);
    J3D_UNUSED(foundTrasureValue);
    J3D_UNUSED(totalTreasureValue);
    // Altered: The Windows store dialog answers 1 when the bonus map was bought; the
    // Null dialog cancels, so no store purchases and no bonus map happen.
    return JONESPLATFORM_NULL_IDCANCEL;
}

int J3DAPI jonesConfig_ShowStatisticsDialog(tSysWindow hWnd, SithGameStatistics* pStatistics)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pStatistics);
    // Altered: The statistics dialog's result is only consumed as a modal wait; cancel
    // is the Windows EndDialog(hwnd, 2) answer.
    return JONESPLATFORM_NULL_IDCANCEL;
}

int J3DAPI jonesConfig_ShowStoreDialog(tSysWindow hWnd, int* pBalance, int* pItemsState, int a4)
{
    J3D_UNUSED(hWnd);
    J3D_UNUSED(pBalance);
    J3D_UNUSED(pItemsState);
    J3D_UNUSED(a4);
    // Altered: The store screen comes in a later slice; until then the entry point cancels and
    // leaves the balance and item states unchanged.
    return JONESPLATFORM_NULL_IDCANCEL;
}

void jonesConfig_UpdateCurrentLevelNum(void)
{
    // Added: No statistics screen on this platform; nothing to track.
}
