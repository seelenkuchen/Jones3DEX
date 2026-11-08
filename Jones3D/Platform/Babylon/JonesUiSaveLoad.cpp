// Added: the Babylon column's Save, Load and Exit screens; see JonesUiSaveLoad.h.
//
// Return values and nested order, from the original dialogs
// (git show 82b8f607^:Jones3D/Platform/Win32/jonesConfig.c):
// - jonesConfig_GetSaveGameFilePath (line 3360) answers 1 with the chosen full path, 2 when the
//   dialog is cancelled or pOutFilePath is null. Its CDN_FILEOK handler asks the overwrite dialog
//   (jonesConfig_ShowOverwriteSaveGameDlg, line 3724) before accepting an existing file, and the
//   overwrite dialog's No (2) leaves the file dialog open.
// - jonesConfig_GetLoadGameFilePath (line 3828) answers 1 with the chosen full path, 2 on cancel.
//   In a running game it asks jonesConfig_ShowLoadGameWarningMsgBox (line 4160) first; Yes (1)
//   continues and No (2) stays in the file dialog.
// - jonesConfig_ShowExitGameDialog (line 8783) returns 1 (exit), 1187 (save first) or 2 (cancel).
//   Its 1187 handler (line 3817) opens jonesConfig_GetSaveGameFilePath and cancels the exit with 2
//   when that screen does not return 1.
// - jonesConfig_GetSaveGameFilePath returns 2 without a save game title or filter (lines 3373-3376),
//   and jonesConfig_GetLoadGameFilePath does the same (lines 3841-3844); the string table is live
//   whenever the game is past startup.
//
// Callers: JonesHud.c 3320-3425 (Save, Load, Exit from the HUD menu) and JonesMain.c 316 (Load
// from the load-game start mode).
#include <Jones3D/Platform/Babylon/JonesUiSaveLoad.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesLevel.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiEndScreens.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>

#include <sith/Dss/sithGamesave.h>
#include <sith/Dss/sithGamesaveSys.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithWorld.h>

#include <babylon/files/FileSystem.h>
#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonPath.h>
#include <std/General/std.h>
#include <std/General/stdFnames.h>
#include <std/General/stdUtil.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr int kJonesDialogSave         = 1;
    constexpr int kJonesDialogCancel       = 2;
    constexpr int kJonesDialogSaveAndExit  = 1187;

    constexpr babylon::ui::ActionId kJonesSaveActionUse = 1;

    constexpr std::size_t kJonesSaveTextIndex  = 0;
    constexpr std::size_t kJonesSaveImageIndex = 1;
    constexpr std::size_t kJonesSaveListIndex  = 2;

    constexpr std::size_t kJonesLoadImageIndex = 0;
    constexpr std::size_t kJonesLoadListIndex  = 1;

    constexpr babylon::ui::Size kJonesThumbnailSize{ 96.0f, 72.0f };

    // Added: the Windows file dialogs took their headings from the OS, which has no game string.
    // The string table is a game asset that the repo does not hold, so this one label is English
    // until a JONES_STR_ key exists for it.
    constexpr const char* kJonesNewSaveText = "New save"; // TODO: [COMPAT] add a JONES_STR key

    std::string JonesUiSave_JoinPath(const char* pDir, const std::string& name)
    {
        std::string path = pDir ? pDir : "";
        if ( !path.empty() && path.back() != '\\' && path.back() != '/' )
        {
            path += '\\';
        }

        path += name;
        return path;
    }

    bool JonesUiSave_HasNdsExtension(const std::string& name)
    {
        if ( name.size() < 4 )
        {
            return false;
        }

        const std::size_t cur = name.size() - 4;
        return name[cur] == '.' && std::tolower((unsigned char)name[cur + 1]) == 'n'
            && std::tolower((unsigned char)name[cur + 2]) == 'd'
            && std::tolower((unsigned char)name[cur + 3]) == 's';
    }

    // Replaces the extension, like stdFnames_ChangeExtEx, so a typed "name.txt" becomes
    // "name.nds" and a typed "name.nds" keeps one extension.
    std::string JonesUiSave_EnsureNdsExtension(const std::string& name)
    {
        if ( JonesUiSave_HasNdsExtension(name) )
        {
            return name;
        }

        std::string result = name;
        const std::size_t dot = result.rfind('.');
        if ( dot != std::string::npos && dot != 0 )
        {
            result.erase(dot);
        }

        result += ".nds";
        return result;
    }

    std::string JonesUiSave_Stem(const std::string& name)
    {
        const std::size_t dot = name.rfind('.');
        return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
    }

    std::string JonesUiSave_FormatDate(std::int64_t modifiedSeconds)
    {
        const std::time_t seconds = (std::time_t)modifiedSeconds;
        std::tm timeValue{};
#ifdef _WIN32
        localtime_s(&timeValue, &seconds);
#else
        localtime_r(&seconds, &timeValue);
#endif

        char aText[32] = { 0 };
        std::strftime(aText, sizeof(aText), "%Y-%m-%d %H:%M", &timeValue);
        return std::string(aText);
    }

    std::tm JonesUiSave_Now()
    {
        const std::time_t now = std::time(nullptr);
        std::tm timeValue{};
#ifdef _WIN32
        localtime_s(&timeValue, &now);
#else
        localtime_r(&now, &timeValue);
#endif
        return timeValue;
    }

    bool JonesUiSave_Exists(babylon::files::FileSystem& fileSystem, const std::string& path)
    {
        std::string resolved;
        if ( !stdBabylonPath_Resolve(fileSystem, path.c_str(), resolved) )
        {
            return false;
        }

        return fileSystem.exists(resolved);
    }

    // A C++-side copy: stdUtil_StringCopy is inline in a C header and named with C++ linkage
    // when a C++ file reaches it through j3dcrt.h, which the C implementation does not use.
    void JonesUiSave_CopyString(char* pDest, std::size_t destSize, const char* pSrc)
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

    bool JonesUiSave_HasString(const char* pKey)
    {
        const char* pValue = jonesString_GetString(pKey);
        return pValue && pValue[0];
    }

    void JonesUiSave_CopyPath(char* pOutPath, const std::string& path)
    {
        JonesUiSave_CopyString(pOutPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, path.c_str());
    }

    bool JonesUiSave_IsDeviceName(const std::string& name)
    {
        std::string stem = JonesUiSave_Stem(name);
        std::transform(stem.begin(), stem.end(), stem.begin(),
            [](unsigned char ch) { return (char)std::toupper(ch); });

        static const char* const aDeviceNames[] = {
            "CON", "PRN", "AUX", "NUL",
            "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
        };

        for ( const char* pDeviceName : aDeviceNames )
        {
            if ( stem == pDeviceName )
            {
                return true;
            }
        }

        return false;
    }

    babylon::ui::Screen JonesUiSave_BuildYesNo(const std::string& title, const std::string& text,
        const std::string& yesText, const std::string& noText)
    {
        babylon::ui::Screen screen;
        screen.title = title;
        screen.widgets.emplace_back(babylon::ui::Label{ text, std::string() });
        screen.widgets.emplace_back(babylon::ui::Button{ yesText, kJonesDialogSave, true });
        screen.widgets.emplace_back(babylon::ui::Button{ noText, kJonesDialogCancel, true });
        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = 1;
        return screen;
    }

    // The overwrite prompt, dialog 214 in the original resources: title JONES_STR_SAVEGM, text
    // JONES_STR_OVERWRITE with the save name, Yes (1) and No (2).
    int JonesUiSave_AskOverwrite(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
        const std::string& saveTitle, const std::string& fileName)
    {
        const std::string question = JonesUiContext_ResolveString("JONES_STR_OVERWRITE");
        char aText[512] = { 0 };
        std::snprintf(aText, sizeof(aText), question.c_str(), fileName.c_str());

        const babylon::ui::Screen screen = JonesUiSave_BuildYesNo(saveTitle, aText,
            JonesUiContext_ResolveString("JONES_STR_YES"),
            JonesUiContext_ResolveString("JONES_STR_NO"));
        return JonesUiModal_Run(ui, pPainter, screen,
            [](babylon::ui::ResultCode code) { return (int)code; });
    }

    // The in-game load warning, dialog 163 in the original resources: title JONES_STR_LOADGM,
    // text JONES_STR_PROGRESS1, Yes (1) and No (2).
    int JonesUiLoad_AskWarning(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
        const std::string& loadTitle)
    {
        const babylon::ui::Screen screen = JonesUiSave_BuildYesNo(loadTitle,
            JonesUiContext_ResolveString("JONES_STR_PROGRESS1"),
            JonesUiContext_ResolveString("JONES_STR_YES"),
            JonesUiContext_ResolveString("JONES_STR_NO"));
        return JonesUiModal_Run(ui, pPainter, screen,
            [](babylon::ui::ResultCode code) { return (int)code; });
    }

    // Registers every save's thumbnail with the painter. The ids live until the set is destroyed,
    // which happens when the screen returns; a 0 id means the save has no thumbnail.
    class JonesUiSave_Thumbnails
    {
    public:
        explicit JonesUiSave_Thumbnails(JonesUiPainter* pPainter)
            : m_pPainter(pPainter)
        {
        }

        ~JonesUiSave_Thumbnails()
        {
            if ( !m_pPainter )
            {
                return;
            }

            for ( babylon::ui::ImageId image : m_images )
            {
                m_pPainter->unregisterImage(image);
            }
        }

        JonesUiSave_Thumbnails(const JonesUiSave_Thumbnails&) = delete;
        JonesUiSave_Thumbnails& operator=(const JonesUiSave_Thumbnails&) = delete;

        void Load(const std::vector<JonesUiSaveEntry>& entries)
        {
            m_images.assign(entries.size(), 0);
            if ( !m_pPainter )
            {
                return;
            }

            std::vector<std::uint8_t> aThumbnail(SITHSAVEGAME_THUMBSIZE);
            for ( std::size_t cur = 0; cur < entries.size(); ++cur )
            {
                if ( sithGamesave_ReadThumbnail(entries[cur].path.c_str(), aThumbnail.data(),
                        aThumbnail.size()) )
                {
                    m_images[cur] = m_pPainter->registerRgbImage(aThumbnail.data(),
                        SITHSAVEGAME_THUMB_WIDTH, SITHSAVEGAME_THUMB_HEIGHT);
                }
            }
        }

        babylon::ui::ImageId Image(std::size_t row) const
        {
            return row < m_images.size() ? m_images[row] : 0;
        }

    private:
        JonesUiPainter* m_pPainter;
        std::vector<babylon::ui::ImageId> m_images;
    };

    babylon::ui::Screen JonesUiSave_BuildSaveScreen(const std::string& title,
        const std::string& defaultName, const std::vector<JonesUiSaveEntry>& entries,
        const JonesUiSave_Thumbnails& thumbnails, const std::string& cancelText)
    {
        babylon::ui::Screen screen;
        screen.title = title;

        screen.widgets.emplace_back(babylon::ui::TextEntry{
            kJonesNewSaveText, std::string(), defaultName, true });

        screen.widgets.emplace_back(JonesUiSave_ThumbnailImage(thumbnails.Image(0)));

        std::vector<babylon::ui::ListItem> items;
        items.reserve(entries.size());
        for ( std::size_t cur = 0; cur < entries.size(); ++cur )
        {
            items.push_back(babylon::ui::ListItem{
                JonesUiSave_RowText(entries[cur]), 0, (std::int64_t)cur + 1 });
        }

        screen.widgets.emplace_back(babylon::ui::List{
            std::string(), std::move(items), 0, kJonesSaveActionUse, true });
        screen.widgets.emplace_back(babylon::ui::Button{ cancelText, kJonesDialogCancel, true });

        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = kJonesSaveTextIndex;
        return screen;
    }

    babylon::ui::Screen JonesUiSave_BuildLoadScreen(const std::string& title,
        const std::vector<JonesUiSaveEntry>& entries, const JonesUiSave_Thumbnails& thumbnails,
        const std::string& cancelText)
    {
        babylon::ui::Screen screen;
        screen.title = title;

        screen.widgets.emplace_back(JonesUiSave_ThumbnailImage(thumbnails.Image(0)));

        std::vector<babylon::ui::ListItem> items;
        items.reserve(entries.size());
        for ( std::size_t cur = 0; cur < entries.size(); ++cur )
        {
            items.push_back(babylon::ui::ListItem{
                JonesUiSave_RowText(entries[cur]), 0, (std::int64_t)cur + 1 });
        }

        screen.widgets.emplace_back(babylon::ui::List{
            std::string(), std::move(items), 0, kJonesSaveActionUse, true });
        screen.widgets.emplace_back(babylon::ui::Button{ cancelText, kJonesDialogCancel, true });

        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = kJonesLoadListIndex;
        return screen;
    }

    void JonesUiSave_SetImage(babylon::ui::Ui& ui, babylon::ui::ScreenId screenId,
        std::size_t index, const JonesUiSave_Thumbnails& thumbnails, std::size_t row)
    {
        static_cast<void>(ui.setWidget(screenId, index,
            JonesUiSave_ThumbnailImage(thumbnails.Image(row))));
    }

    babylon::ui::Screen JonesUiSave_BuildExitScreen(void)
    {
        babylon::ui::Screen screen;
        screen.title = JonesUiContext_ResolveString("JONES_STR_EXIT");

        // Dialog 211's buttons, top to bottom and with its default on the cancel answer:
        // JONES_STR_EXIT3 (2), JONES_STR_EXIT2 (1187), JONES_STR_EXIT1 (1).
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_EXIT3"), kJonesDialogCancel, true });
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_EXIT2"), kJonesDialogSaveAndExit, true });
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_EXIT1"), kJonesDialogSave, true });
        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = 0;
        return screen;
    }
}

#ifdef J3D_TEST
void JonesUiSaveLoad_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens)
{
    JonesUiSaveEntry entry;
    entry.fileName    = "save.nds";
    entry.displayName = "save";
    entry.levelName   = "Level";
    entry.dateText    = "2020-01-01";
    entry.path        = "saves/save.nds";

    const std::vector<JonesUiSaveEntry> entries{ entry };
    JonesUiSave_Thumbnails thumbnails(nullptr);

    screens.push_back(JonesUiSave_BuildYesNo("Title", "Text", "Yes", "No"));
    screens.push_back(JonesUiSave_BuildSaveScreen("Save", "Default", entries, thumbnails,
        "Cancel"));
    screens.push_back(JonesUiSave_BuildLoadScreen("Load", entries, thumbnails, "Cancel"));
    screens.push_back(JonesUiSave_BuildExitScreen());
}
#endif

std::string JonesUiSave_LevelDisplayName(const std::string& levelFileName)
{
    for ( std::size_t cur = 0; cur < STD_ARRAYLEN(JonesMain_aCndLevelLoadInfos); ++cur )
    {
        const JonesLevelInfo& level = JonesMain_aCndLevelLoadInfos[cur];
        if ( !level.pFilename || !streqi(levelFileName.c_str(), level.pFilename) )
        {
            continue;
        }

        // The original statistics dialog maps the same levels to their display-name keys; level 1
        // in the load table (Canyonlands) is its index 0.
        const int levelIndex = (int)cur - JONESLEVEL_FIRSTLEVELNUM;
        if ( levelIndex >= 0 && levelIndex <= JONESLEVEL_LASTLEVELNUM )
        {
            return JonesUiContext_ResolveString(JonesUiEndScreens_LevelNameKey(levelIndex));
        }

        break;
    }

    return levelFileName;
}

std::string JonesUiSave_RowText(const JonesUiSaveEntry& entry)
{
    std::string text = entry.displayName;
    if ( !entry.levelName.empty() )
    {
        text += " - ";
        text += entry.levelName;
    }

    if ( !entry.dateText.empty() )
    {
        text += " - ";
        text += entry.dateText;
    }

    return text;
}

babylon::ui::Image JonesUiSave_ThumbnailImage(babylon::ui::ImageId image)
{
    if ( image == 0 )
    {
        return babylon::ui::Image{ 0, babylon::ui::Size{}, babylon::ui::TextAlign::Center };
    }

    return babylon::ui::Image{ image, kJonesThumbnailSize, babylon::ui::TextAlign::Center };
}

std::string JonesUiSave_SanitizeName(const char* pName)
{
    static const char* const kInvalid = "<>:\"/\\|?*";

    std::string result;
    if ( pName )
    {
        for ( const char* p = pName; *p; ++p )
        {
            const unsigned char ch = (unsigned char)*p;
            if ( ch < 0x20 || ch == 0x7F || std::strchr(kInvalid, ch) )
            {
                result += '_';
            }
            else
            {
                result += (char)ch;
            }
        }
    }

    while ( !result.empty() && result.front() == ' ' )
    {
        result.erase(result.begin());
    }

    while ( !result.empty() && (result.back() == ' ' || result.back() == '.') )
    {
        result.pop_back();
    }

    if ( result.empty() )
    {
        result = "save";
    }

    if ( result.size() > 64 )
    {
        result.resize(64);
        while ( !result.empty() && (result.back() == ' ' || result.back() == '.') )
        {
            result.pop_back();
        }
    }

    if ( result.empty() || result == "." || result == ".." )
    {
        result = "save";
    }

    if ( JonesUiSave_IsDeviceName(result) )
    {
        result.insert(result.begin(), '_');
    }

    return result;
}

std::string JonesUiSave_MakeDefaultName(const char* pLevelName, const std::tm& timeValue)
{
    char aText[256] = { 0 };
    std::snprintf(aText, sizeof(aText), "%s %04d-%02d-%02d %02d-%02d-%02d",
        pLevelName ? pLevelName : "", timeValue.tm_year + 1900, timeValue.tm_mon + 1,
        timeValue.tm_mday, timeValue.tm_hour, timeValue.tm_min, timeValue.tm_sec);
    return std::string(aText);
}

bool JonesUiSave_CollectEntries(babylon::files::FileSystem& fileSystem, const char* pSaveDir,
    std::vector<JonesUiSaveEntry>& entries)
{
    entries.clear();
    if ( !pSaveDir || !pSaveDir[0] )
    {
        return false;
    }

    std::string aResolved;
    if ( !stdBabylonPath_Resolve(fileSystem, pSaveDir, aResolved) )
    {
        return false;
    }

    std::optional<std::vector<babylon::files::DirectoryEntry>> pEntries =
        fileSystem.listDirectory(aResolved);
    if ( !pEntries )
    {
        return false;
    }

    for ( const babylon::files::DirectoryEntry& entry : *pEntries )
    {
        if ( entry.isDirectory || !JonesUiSave_HasNdsExtension(entry.name) )
        {
            continue;
        }

        JonesUiSaveEntry saveEntry;
        saveEntry.fileName        = entry.name;
        saveEntry.displayName     = JonesUiSave_Stem(entry.name);
        saveEntry.modifiedSeconds = entry.modifiedSeconds;
        saveEntry.dateText        = JonesUiSave_FormatDate(entry.modifiedSeconds);
        saveEntry.path            = JonesUiSave_JoinPath(pSaveDir, entry.name);

        char aLevelName[64] = { 0 };
        if ( sithGamesave_LoadLevelFilename(saveEntry.path.c_str(), aLevelName) == 0
            && aLevelName[0] )
        {
            saveEntry.levelName = JonesUiSave_LevelDisplayName(aLevelName);
        }
        else
        {
            saveEntry.levelName = saveEntry.displayName;
        }

        entries.push_back(std::move(saveEntry));
    }

    std::sort(entries.begin(), entries.end(),
        [](const JonesUiSaveEntry& a, const JonesUiSaveEntry& b)
        {
            if ( a.modifiedSeconds != b.modifiedSeconds )
            {
                return a.modifiedSeconds > b.modifiedSeconds;
            }

            return a.fileName < b.fileName;
        });
    return true;
}

int JonesUiSave_RunSaveScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesUiPainter* pImagePainter, babylon::files::FileSystem& fileSystem, const char* pSaveDir,
    const char* pLevelName, const std::tm& now, char* pOutPath)
{
    const std::string title = JonesUiContext_ResolveString("JONES_STR_SAVEGM");
    const std::string cancelText = JonesUiContext_ResolveString("JONES_STR_CANCEL");

    std::vector<JonesUiSaveEntry> entries;
    static_cast<void>(JonesUiSave_CollectEntries(fileSystem, pSaveDir, entries));

    JonesUiSave_Thumbnails thumbnails(pImagePainter);
    thumbnails.Load(entries);

    // The world names levels by file name; the default save name uses their display names.
    const std::string levelDisplayName = JonesUiSave_LevelDisplayName(pLevelName ? pLevelName : "");
    const std::string defaultName = JonesUiSave_SanitizeName(
        JonesUiSave_MakeDefaultName(levelDisplayName.c_str(), now).c_str());

    babylon::ui::Screen screen = JonesUiSave_BuildSaveScreen(title, defaultName, entries,
        thumbnails, cancelText);

    JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef, babylon::ui::ScreenId screenId,
        const babylon::ui::UiEvent& event) -> bool
    {
        if ( const babylon::ui::SelectionChanged* pSelection =
                std::get_if<babylon::ui::SelectionChanged>(&event) )
        {
            if ( pSelection->screen == screenId && pSelection->widget == kJonesSaveListIndex )
            {
                JonesUiSave_SetImage(uiRef, screenId, kJonesSaveImageIndex, thumbnails,
                    pSelection->row);
                return true;
            }

            return false;
        }

        if ( const babylon::ui::ActionPressed* pAction =
                std::get_if<babylon::ui::ActionPressed>(&event) )
        {
            if ( pAction->screen != screenId || pAction->action != kJonesSaveActionUse )
            {
                return false;
            }

            const std::size_t row = (std::size_t)(pAction->item - 1);
            if ( row >= entries.size() )
            {
                return true;
            }

            const JonesUiSaveEntry& entry = entries[row];
            if ( JonesUiSave_AskOverwrite(uiRef, pPainter, title, entry.fileName)
                == kJonesDialogSave )
            {
                JonesUiSave_CopyPath(pOutPath, entry.path);
                static_cast<void>(uiRef.pop(kJonesDialogSave));
            }

            return true;
        }

        if ( const babylon::ui::TextSubmitted* pText =
                std::get_if<babylon::ui::TextSubmitted>(&event) )
        {
            if ( pText->screen != screenId || pText->widget != kJonesSaveTextIndex )
            {
                return false;
            }

            const std::string name = JonesUiSave_SanitizeName(pText->text.c_str());
            const std::string fileName = JonesUiSave_EnsureNdsExtension(name);
            const std::string path = JonesUiSave_JoinPath(pSaveDir, fileName);

            if ( JonesUiSave_Exists(fileSystem, path) )
            {
                if ( JonesUiSave_AskOverwrite(uiRef, pPainter, title, fileName) != kJonesDialogSave )
                {
                    static_cast<void>(uiRef.setWidget(screenId, kJonesSaveTextIndex,
                        babylon::ui::TextEntry{ kJonesNewSaveText, std::string(), name, true }));
                    return true;
                }
            }

            JonesUiSave_CopyPath(pOutPath, path);
            static_cast<void>(uiRef.pop(kJonesDialogSave));
            return true;
        }

        return false;
    };

    return JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler);
}

int JonesUiLoad_RunLoadScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesUiPainter* pImagePainter, babylon::files::FileSystem& fileSystem, const char* pSaveDir,
    bool bWarnBeforeLoad, char* pOutPath)
{
    const std::string title = JonesUiContext_ResolveString("JONES_STR_LOADGM");
    const std::string cancelText = JonesUiContext_ResolveString("JONES_STR_CANCEL");

    std::vector<JonesUiSaveEntry> entries;
    static_cast<void>(JonesUiSave_CollectEntries(fileSystem, pSaveDir, entries));

    JonesUiSave_Thumbnails thumbnails(pImagePainter);
    thumbnails.Load(entries);

    babylon::ui::Screen screen = JonesUiSave_BuildLoadScreen(title, entries, thumbnails,
        cancelText);

    JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef, babylon::ui::ScreenId screenId,
        const babylon::ui::UiEvent& event) -> bool
    {
        if ( const babylon::ui::SelectionChanged* pSelection =
                std::get_if<babylon::ui::SelectionChanged>(&event) )
        {
            if ( pSelection->screen == screenId && pSelection->widget == kJonesLoadListIndex )
            {
                JonesUiSave_SetImage(uiRef, screenId, kJonesLoadImageIndex, thumbnails,
                    pSelection->row);
                return true;
            }

            return false;
        }

        const babylon::ui::ActionPressed* pAction =
            std::get_if<babylon::ui::ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId || pAction->action != kJonesSaveActionUse )
        {
            return false;
        }

        const std::size_t row = (std::size_t)(pAction->item - 1);
        if ( row >= entries.size() )
        {
            return true;
        }

        if ( bWarnBeforeLoad
            && JonesUiLoad_AskWarning(uiRef, pPainter, title) != kJonesDialogSave )
        {
            return true;
        }

        JonesUiSave_CopyPath(pOutPath, entries[row].path);
        static_cast<void>(uiRef.pop(kJonesDialogSave));
        return true;
    };

    return JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler);
}

int JonesUiExit_RunExitScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesUiPainter* pImagePainter, babylon::files::FileSystem& fileSystem, const char* pSaveDir,
    const char* pLevelName, const std::tm& now, char* pSavePath)
{
    const babylon::ui::Screen screen = JonesUiSave_BuildExitScreen();

    JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef, babylon::ui::ScreenId screenId,
        const babylon::ui::UiEvent& event) -> bool
    {
        const babylon::ui::ActionPressed* pAction =
            std::get_if<babylon::ui::ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId
            || pAction->action != kJonesDialogSaveAndExit )
        {
            return false;
        }

        char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
        const int saveResult = JonesUiSave_RunSaveScreen(uiRef, pPainter, pImagePainter,
            fileSystem, pSaveDir, pLevelName, now, aPath);
        if ( saveResult == kJonesDialogSave )
        {
            JonesUiSave_CopyPath(pSavePath, aPath);
            static_cast<void>(uiRef.pop(kJonesDialogSaveAndExit));
        }
        else
        {
            // The original exit dialog ends with 2 when its Save button does not return 1.
            static_cast<void>(uiRef.pop(kJonesDialogCancel));
        }

        return true;
    };

    return JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler);
}

extern "C" int J3DAPI jonesConfig_GetSaveGameFilePath(tSysWindow hWnd, char* pOutFilePath)
{
    (void)hWnd;

    if ( !pOutFilePath )
    {
        return kJonesDialogCancel;
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless: the Null stub answers cancel without blocking.
        return kJonesDialogCancel;
    }

    // The original file dialog gives up when its title or filter string is missing.
    if ( !JonesUiSave_HasString("JONES_STR_SAVEGM") || !JonesUiSave_HasString("JONES_STR_FILTER") )
    {
        return kJonesDialogCancel;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return kJonesDialogCancel;
    }

    const char* pLevelName = sithWorld_g_pCurrentWorld ? sithWorld_g_pCurrentWorld->aName : "";
    const std::tm now = JonesUiSave_Now();
    const int result = JonesUiSave_RunSaveScreen(*pUiContext->pUi, &pUiContext->painter,
        &pUiContext->painter, *pContext->pFileSystem, sithGetSaveGamesDir(), pLevelName, now,
        pOutFilePath);
    JonesUiContext_Release(pUiContext);
    return result;
}

extern "C" int J3DAPI jonesConfig_GetLoadGameFilePath(tSysWindow hWnd, char* pDestNdsPath)
{
    (void)hWnd;

    if ( !pDestNdsPath )
    {
        return kJonesDialogCancel;
    }

    // Added: the --load option answers the dialog once, without a screen, like the Null stub.
    const char* pLoadPath = JonesPlatformNull_GetLoadPath();
    if ( pLoadPath && pLoadPath[0] )
    {
        char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
        JonesUiSave_CopyString(aPath, STD_ARRAYLEN(aPath), pLoadPath);
        stdFnames_ChangeExtEx(aPath, STD_ARRAYLEN(aPath), "nds");
        JonesUiSave_CopyString(pDestNdsPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, aPath);
        JonesPlatformNull_ClearLoadPath();
        return kJonesDialogSave;
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless: the Null stub answers cancel without blocking.
        return kJonesDialogCancel;
    }

    // The original file dialog gives up when its title or filter string is missing.
    if ( !JonesUiSave_HasString("JONES_STR_LOADGM") || !JonesUiSave_HasString("JONES_STR_FILTER") )
    {
        return kJonesDialogCancel;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return kJonesDialogCancel;
    }

    bool bWarn = false;
    if ( JonesMain_HasStarted() && sithPlayer_g_pLocalPlayerThing )
    {
        const unsigned int flags = sithPlayer_g_pLocalPlayerThing->flags;
        bWarn = (flags & SITH_TF_DYING) == 0 && (flags & SITH_TF_DESTROYED) == 0;
    }

    const int result = JonesUiLoad_RunLoadScreen(*pUiContext->pUi, &pUiContext->painter,
        &pUiContext->painter, *pContext->pFileSystem, sithGetSaveGamesDir(), bWarn, pDestNdsPath);
    JonesUiContext_Release(pUiContext);
    return result;
}

extern "C" int J3DAPI jonesConfig_ShowExitGameDialog(tSysWindow hWnd, char* pSaveGameFilePath)
{
    (void)hWnd;

    if ( !pSaveGameFilePath )
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

    const char* pLevelName = sithWorld_g_pCurrentWorld ? sithWorld_g_pCurrentWorld->aName : "";
    const std::tm now = JonesUiSave_Now();
    const int result = JonesUiExit_RunExitScreen(*pUiContext->pUi, &pUiContext->painter,
        &pUiContext->painter, *pContext->pFileSystem, sithGetSaveGamesDir(), pLevelName, now,
        pSaveGameFilePath);
    JonesUiContext_Release(pUiContext);
    return result;
}
