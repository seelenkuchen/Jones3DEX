// Added: the Babylon windowed prompt for missing game data (ADR-0021, M8.4). It stays in the
// adapter, so the game layer only sees JonesDataLocate's function-pointer hook.
#include <Jones3D/Platform/Babylon/JonesBabylonData.h>

#include <Jones3D/Main/JonesDataLocate.h>

#include <babylon/platform/Platform.h>

#include <std/Babylon/stdBabylon.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    constexpr std::string_view kPromptTitle = "Jones3D EX";
    constexpr std::string_view kPromptButtons[] = { "Choose folder...", "Quit" };
    constexpr std::string_view kDataHint = "Start Jones3D EX with --data <folder>.";

    void ShowUnavailable(const char* pMessage)
    {
        babylon::platform::Platform* pPlatform =
            stdBabylon_RequireContext("JonesBabylonData").pPlatform;

        std::string text = pMessage ? pMessage : "";
        text += ' ';
        text += kDataHint;
        static_cast<void>(pPlatform->showMessageBox(kPromptTitle, text));
    }

    bool PromptForFolder(const char* pMessage, char* pOutDir, std::size_t outSize, void*)
    {
        babylon::platform::Platform* pPlatform =
            stdBabylon_RequireContext("JonesBabylonData").pPlatform;
        const std::string_view message = pMessage ? pMessage : "";

        while ( true )
        {
            std::optional<std::size_t> choice = pPlatform->askChoice(kPromptTitle, message,
                kPromptButtons);
            if ( !choice )
            {
                ShowUnavailable(pMessage);
                return false;
            }

            // Escape and closing the box choose the last button, Quit.
            if ( *choice != 0 )
            {
                return false;
            }

            babylon::platform::FolderChoice folder = pPlatform->chooseFolder(kPromptTitle, "");
            if ( folder.result == babylon::platform::FolderChoiceResult::Unavailable )
            {
                ShowUnavailable(pMessage);
                return false;
            }

            if ( folder.result == babylon::platform::FolderChoiceResult::Cancelled )
            {
                continue;
            }

            if ( folder.path.size() + 1 > outSize )
            {
                static_cast<void>(pPlatform->showMessageBox(kPromptTitle,
                    "The selected folder path is too long."));
                return false;
            }

            std::memcpy(pOutDir, folder.path.c_str(), folder.path.size() + 1);
            return true;
        }
    }
}

extern "C" void JonesBabylonData_InstallPromptHandler(void)
{
    JonesDataLocate_SetPromptHandler(PromptForFolder, nullptr);
}
