// Added: the Babylon column's Save, Load and Exit screens (ADR-0026), the three jonesConfig_*
// dialogs this slice replaces. Each runner pushes its screen through the shared babylon::ui of the
// column, opens the original nested prompts, and returns the original dialog's value.
//
// The list, name and date helpers and the explicit-directory runners are also the test surface:
// they keep the screen logic free of game assets and of the running game's world.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUISAVELOAD_H
#define JONES3D_PLATFORM_BABYLON_JONESUISAVELOAD_H
#ifndef __cplusplus
#error "JonesUiSaveLoad.h is C++ only: it names babylon::ui and std::string."
#endif

#include <babylon/files/FileSystem.h>
#include <babylon/ui/Painter.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/Widget.h>

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

class JonesUiPainter;

// One save in the save directory. fileName is the bare name with its .nds extension, displayName
// the same without the extension, levelName the level's display name (or the file name when the
// level is unknown), path the engine path the save code opens, and dateText the modification date
// the file list shows.
struct JonesUiSaveEntry
{
    std::string fileName;
    std::string displayName;
    std::string levelName;
    std::string dateText;
    std::string path;
    std::int64_t modifiedSeconds = 0;
};

// Makes a typed save name usable as one file name on Windows, Linux and macOS: control bytes, the
// Windows-reserved characters <>:"/\|?*, a trailing dot or space and the . and .. names are
// replaced or removed, and Windows device names get a leading underscore. The result is never
// empty. Spaces inside the name and UTF-8 bytes are kept.
std::string JonesUiSave_SanitizeName(const char* pName);

// The default name of a new save: the level name followed by the local date and time in
// YYYY-MM-DD HH-MM-SS form. The result is not sanitized; callers pass it through
// JonesUiSave_SanitizeName.
std::string JonesUiSave_MakeDefaultName(const char* pLevelName, const std::tm& timeValue);

// Lists the .nds files directly in pSaveDir, newest modification first and file name ascending
// for equal times. A file saves its level display name from the NdsHeader's level file name; a
// damaged header or an unknown level falls back to the file name. Returns false when the directory
// cannot be resolved or read.
bool JonesUiSave_CollectEntries(babylon::files::FileSystem& fileSystem, const char* pSaveDir,
    std::vector<JonesUiSaveEntry>& entries);

// The display name of a save's level from the level file name stored in its header. The name is
// matched case-insensitively against the CND level table and resolved through the game's strings;
// when no level matches, the level file name is returned unchanged.
std::string JonesUiSave_LevelDisplayName(const std::string& levelFileName);

// The line one save row shows: save name, level name and modification date, separated by " - ".
std::string JonesUiSave_RowText(const JonesUiSaveEntry& entry);

// The thumbnail widget for a row. A zero image id (the save has no thumbnail) gives a zero-size
// image, so the layout reserves no space and nothing is drawn in its place.
babylon::ui::Image JonesUiSave_ThumbnailImage(babylon::ui::ImageId image);

// Runs the Save screen. Writes the chosen engine path to pOutPath and returns 1, or returns 2
// when the player cancels. pImagePainter may be null, which skips thumbnails (the logic tests);
// the windowed wrappers pass the shared JonesUiPainter so the selected save's thumbnail is shown.
int JonesUiSave_RunSaveScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesUiPainter* pImagePainter, babylon::files::FileSystem& fileSystem, const char* pSaveDir,
    const char* pLevelName, const std::tm& now, char* pOutPath);

// Runs the Load screen. Returns 1 with the chosen engine path, or 2 on cancel. bWarnBeforeLoad
// shows the original in-game warning (JONES_STR_PROGRESS1) before the path is accepted.
int JonesUiLoad_RunLoadScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesUiPainter* pImagePainter, babylon::files::FileSystem& fileSystem, const char* pSaveDir,
    bool bWarnBeforeLoad, char* pOutPath);

// Runs the Exit screen and returns 1 (exit), 1187 (save first, then exit) or 2 (cancel). On 1187
// pSavePath holds the path written by the nested Save screen. A cancelled nested Save cancels the
// exit, as in the original dialog.
int JonesUiExit_RunExitScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesUiPainter* pImagePainter, babylon::files::FileSystem& fileSystem, const char* pSaveDir,
    const char* pLevelName, const std::tm& now, char* pSavePath);

#endif // JONES3D_PLATFORM_BABYLON_JONESUISAVELOAD_H
