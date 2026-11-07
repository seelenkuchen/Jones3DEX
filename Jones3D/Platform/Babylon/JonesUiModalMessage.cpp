// Added: the Babylon column's message and missing-file screens (ADR-0026), the two jonesConfig_*
// dialogs this slice replaces. They run through the shared babylon::ui screen state of the column
// so a message opened from another screen is pushed on top of it and returns to it.
//
// Return values, from the original dialogs (git show 82b8f607^:Jones3D/Platform/Win32/jonesConfig.c):
// - jonesConfig_ShowMessageDialog's dialog proc calls EndDialog with the command id for 1 (OK) and
//   2 (cancel/close); the Null stub and the headless column answer 2, so those are kept here.
// - jonesConfig_ShowDialogInsertCD answers 1 (retry) or 2 (quit); with all game data installed a
//   retry cannot find the file, so the missing-file screen always answers 2 and the caller exits
//   cleanly (JonesMain_EnsureLevelFileEx returns 1 unless the dialog answered 1).
#include <Jones3D/Display/jonesConfig.h>

#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/Widget.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/std.h>
#include <std/General/stdDisplay.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr int kJonesDialogOk     = 1; // IDOK
    constexpr int kJonesDialogCancel = 2; // IDCANCEL, the Null stub's answer

    constexpr babylon::ui::ActionId kJonesMessageActionOk = 1;

    constexpr const char* kJonesInsertText1 = "JONES_STR_INSERT1";
    constexpr const char* kJonesInsertText2 = "JONES_STR_INSERT2";

    // The widest line the panel gives its labels: the canvas minus its margins and the panel
    // padding on both sides.
    float MessageTextWidth(const babylon::ui::Ui& ui)
    {
        const babylon::ui::Skin& skin = ui.skin();
        const float width = ui.canvasSize().width - 2.0f * skin.canvasMargin
            - 2.0f * skin.panelPadding;
        return width > 1.0f ? width : 1.0f;
    }

    // Wraps one paragraph into lines no wider than maxWidth, honoring explicit '\n's. A word wider
    // than maxWidth stays alone on its line and is truncated by the painter.
    void WrapParagraph(babylon::ui::Painter& painter, babylon::ui::FontId font,
        float maxWidth, const std::string& text, std::vector<std::string>& lines)
    {
        std::string line;
        size_t cur = 0;
        while ( cur < text.size() )
        {
            const size_t space = text.find(' ', cur);
            const std::string word = text.substr(cur,
                space == std::string::npos ? std::string::npos : space - cur);

            std::string candidate = line;
            if ( !candidate.empty() )
            {
                candidate += ' ';
            }
            candidate += word;

            if ( !line.empty()
                && painter.measureText(font, candidate).width > maxWidth )
            {
                lines.push_back(line);
                line = word;
            }
            else
            {
                line = candidate;
            }

            if ( space == std::string::npos )
            {
                break;
            }
            cur = space + 1;
        }

        lines.push_back(line);
    }

    std::vector<std::string> WrapMessageText(babylon::ui::Painter& painter,
        babylon::ui::FontId font, float maxWidth, const std::string& text)
    {
        std::vector<std::string> lines;
        size_t start = 0;
        for ( ;; )
        {
            const size_t end = text.find('\n', start);
            const std::string paragraph = end == std::string::npos
                ? text.substr(start) : text.substr(start, end - start);
            WrapParagraph(painter, font, maxWidth, paragraph, lines);

            if ( end == std::string::npos )
            {
                break;
            }
            start = end + 1;
        }

        if ( lines.empty() )
        {
            lines.push_back(std::string());
        }
        return lines;
    }

    babylon::ui::Screen BuildMessageScreen(JonesUiContext& context, const std::string& title,
        const std::string& text, const std::string& okText)
    {
        babylon::ui::Screen screen;
        screen.title = title;

        const float maxWidth = MessageTextWidth(*context.pUi);
        for ( const std::string& line : WrapMessageText(context.painter, context.font, maxWidth, text) )
        {
            screen.widgets.emplace_back(babylon::ui::Label{ line, std::string() });
        }

        screen.widgets.emplace_back(babylon::ui::Button{ okText, kJonesMessageActionOk, true });
        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = screen.widgets.size() - 1;
        return screen;
    }

    // Runs one message screen through the shared Ui. Returns false when the shared context cannot
    // start, so the caller can fall back to the native message box.
    bool RunSharedMessage(const std::string& title, const std::string& text,
        const std::string& okText, int& result)
    {
        JonesUiContext* pContext = JonesUiContext_Acquire();
        if ( !pContext )
        {
            return false;
        }

        babylon::ui::Screen screen = BuildMessageScreen(*pContext, title, text, okText);
        result = JonesUiModal_Run(*pContext->pUi, &pContext->painter, screen,
            [](babylon::ui::ResultCode code) { return (int)code; });
        JonesUiContext_Release(pContext);

        return true;
    }
}

extern "C" int J3DAPI jonesConfig_ShowMessageDialog(tSysWindow hWnd, const char* pTitle,
    const char* pText, int iconID)
{
    (void)hWnd;
    // Added: the Win32 dialog showed iconID in a static control; the screen has no icon set, so it
    // is ignored and the title keeps the skin's title color.
    (void)iconID;

    const std::string title = JonesUiContext_ResolveString(pTitle);
    const std::string text  = JonesUiContext_ResolveString(pText);

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow )
    {
        // Headless: answer the Null stub's cancel value, log the text and never block.
        fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
        return kJonesDialogCancel;
    }

    int result = kJonesDialogCancel;
    if ( !RunSharedMessage(title, text, "OK", result) )
    {
        // Added: no open display or no menu font (errors before rendering works). The native box
        // has one OK button, so its answer is IDOK.
        static_cast<void>(pContext->pPlatform->showMessageBox(title, text));
        return kJonesDialogOk;
    }

    return result;
}

extern "C" int J3DAPI jonesConfig_ShowDialogInsertCD(tSysWindow hWnd, intptr_t dwInitParam)
{
    (void)hWnd;

    // The caller passes only the disc number; the missing level file's name is not available here.
    const int cdNum = (int)dwInitParam;
    std::string text = JonesUiContext_ResolveString(cdNum == 2 ? kJonesInsertText2 : kJonesInsertText1);
    if ( std::strncmp(text.c_str(), "JONES_STR_", 10) == 0 )
    {
        text = cdNum == 2 ? "Insert CD 2" : "Insert CD 1";
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow )
    {
        // Headless: the Null stub is silent and answers the quit value; nothing blocks.
        return kJonesDialogCancel;
    }

    int result = kJonesDialogCancel;
    if ( !RunSharedMessage("Jones3D", text, "OK", result) )
    {
        static_cast<void>(pContext->pPlatform->showMessageBox("Jones3D", text));
    }

    (void)result;

    // Added: the disc cannot be inserted in this build and the file remains missing, so the caller
    // gets the quit value and exits cleanly instead of retrying the open.
    return kJonesDialogCancel;
}
