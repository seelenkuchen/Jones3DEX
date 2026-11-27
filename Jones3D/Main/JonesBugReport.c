// Added: the Shift+F12 bug report. One folder gathers the flight recorder's last frames, a
// savegame and a pose file, so a developer can load the exact spot the player reported. The folder
// is report-NNN under JONES3D_REPORT_DIR, or the screenshots directory when the variable is unset.
#include "JonesBugReport.h"

#include "JonesFixedTick.h"
#include "JonesScreenshot.h"

#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Play/jonesCog.h>

#include <sith/Dss/sithGamesave.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithSector.h>
#include <sith/World/sithWorld.h>

#include <std/General/std.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdFileUtil.h>
#include <std/General/stdFnames.h>
#include <std/General/stdUserDir.h>
#include <std/General/stdUtil.h>

#include <stdio.h>
#include <string.h>

static int JonesBugReport_GetSectorIndex(const SithWorld* pWorld, const SithSector* pSector)
{
    if ( !pWorld || !pWorld->aSectors || !pSector )
    {
        return -1;
    }

    return sithSector_GetSectorIndexEx(pWorld, pSector);
}

static void JonesBugReport_WritePose(const char* pFolder)
{
    char aPath[STDUSERDIR_MAXPATH];
    if ( strlen(pFolder) + strlen("pose.txt") + 2u > sizeof(aPath) )
    {
        STDLOG_ERROR("JonesScreenshot: the pose path is too long for '%s'.\n", pFolder);
        return;
    }

    stdFnames_MakePath(aPath, STD_ARRAYLEN(aPath), pFolder, "pose.txt");

    FILE* pFile = NULL;
    if ( fopen_s(&pFile, aPath, "wt") != 0 || !pFile )
    {
        STDLOG_ERROR("JonesScreenshot: cannot write '%s'.\n", aPath);
        return;
    }

    const SithWorld* pWorld = sithWorld_g_pCurrentWorld;
    fprintf(pFile, "level %s\n", pWorld ? pWorld->aName : "none");
    fprintf(pFile, "gameTime %f\n", sithTime_g_secGameTime);

    const SithThing* pPlayer = sithPlayer_g_pLocalPlayerThing;
    if ( pPlayer )
    {
        fprintf(pFile, "player %f %f %f sector %d\n", pPlayer->pos.x, pPlayer->pos.y,
            pPlayer->pos.z, JonesBugReport_GetSectorIndex(pWorld, pPlayer->pInSector));
        fprintf(pFile, "playerLvec %f %f %f\n", pPlayer->orient.lvec.x, pPlayer->orient.lvec.y,
            pPlayer->orient.lvec.z);
    }
    else
    {
        fprintf(pFile, "player none\n");
        fprintf(pFile, "playerLvec none\n");
    }

    const SithCamera* pCamera = sithCamera_g_pCurCamera;
    if ( pCamera )
    {
        fprintf(pFile, "camera %f %f %f sector %d\n", pCamera->pos.x, pCamera->pos.y,
            pCamera->pos.z, JonesBugReport_GetSectorIndex(pWorld, pCamera->pSector));
        fprintf(pFile, "cameraLvec %f %f %f\n", pCamera->orient.lvec.x, pCamera->orient.lvec.y,
            pCamera->orient.lvec.z);
    }
    else
    {
        fprintf(pFile, "camera none\n");
        fprintf(pFile, "cameraLvec none\n");
    }

    fprintf(pFile, "fixedTick %d\n", JonesFixedTick_GetRateHz());
    fclose(pFile);
}

void JonesScreenshot_WriteBugReport(void)
{
    const char* pRoot = stdDisplay_GetBugReportDir();
    if ( !pRoot || pRoot[0] == '\0' )
    {
        pRoot = sithGetScreenShotsDir();
    }

    if ( !pRoot || pRoot[0] == '\0' )
    {
        STDLOG_ERROR("JonesScreenshot: the bug report directory is unavailable.\n");
        return;
    }

    stdFileUtil_MkDir(pRoot);

    char aFolder[STDUSERDIR_MAXPATH];
    if ( !JonesScreenshot_SelectBugReportFolder(pRoot, aFolder, sizeof(aFolder)) )
    {
        STDLOG_ERROR("JonesScreenshot: no free bug report folder under '%s'.\n", pRoot);
        return;
    }

    if ( !stdFileUtil_MkDir(aFolder) && !stdFileUtil_FileExists(aFolder) )
    {
        STDLOG_ERROR("JonesScreenshot: cannot create '%s'.\n", aFolder);
        return;
    }

    stdDisplay_SaveFlightRecorder(aFolder);

    if ( jonesCog_g_bEnableGamesave && !jonesCog_g_bMenuVisible && !sithWorld_g_bLoading
        && sithWorld_g_pCurrentWorld && sithPlayer_g_pLocalPlayerThing )
    {
        char aSavePath[STDUSERDIR_MAXPATH];
        if ( strlen(aFolder) + strlen("report.nds") + 2u > sizeof(aSavePath) )
        {
            STDLOG_ERROR("JonesScreenshot: the savegame path is too long for '%s'.\n", aFolder);
        }
        else
        {
            stdFnames_MakePath(aSavePath, STD_ARRAYLEN(aSavePath), aFolder, "report.nds");
            JonesHud_RestoreTreasuresStatistics();
            sithGamesave_Save(aSavePath, 1);
        }
    }

    JonesBugReport_WritePose(aFolder);

    STDLOG_STATUS("Bug report written to %s\n", aFolder);
}
