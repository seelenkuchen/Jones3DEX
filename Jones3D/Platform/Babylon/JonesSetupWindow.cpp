// The setup wizard's window and its native-dialog fallback; see JonesSetupWindow.h.
#include <Jones3D/Platform/Babylon/JonesSetupWindow.h>

#include <Jones3D/Platform/Babylon/JonesSetupHost.h>
#include <Jones3D/Platform/Babylon/JonesUtf8Path.h>
#include <Jones3D/Platform/Babylon/JonesSetupWizard.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/Path.h>
#include <babylon/font/Font.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/process/NullProcess.h>
#include <babylon/process/Process.h>
#include <babylon/process/SdlProcess.h>
#include <babylon/render/Device.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>
#include <babylon/uirender/RenderPainter.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdPlatform.h>

#include <sith/Main/sithMain.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using namespace babylon::ui;

    constexpr ActionId kActionContinue     = 1;
    constexpr ActionId kActionQuit         = 2;
    constexpr ActionId kActionBack         = 3;
    constexpr ActionId kActionAccept       = 4;
    constexpr ActionId kActionCancel       = 5;
    constexpr ActionId kActionStartGame    = 6;
    constexpr ActionId kActionChooseUser   = 7;
    constexpr ActionId kActionChooseExeDir = 8;
    constexpr ActionId kActionRowBase      = 100;

    constexpr std::size_t kProgressPhase = 0;
    constexpr std::size_t kProgressBar   = 1;
    constexpr std::size_t kProgressFile  = 2;
    constexpr std::size_t kProgressBytes = 3;

    constexpr const char* kWindowTitle = "Jones3D EX Setup";

    constexpr const char* kWelcomeText =
        "Jones3D EX needs the game files of Indiana Jones and the Infernal Machine. "
        "This setup copies them from your copy of the game: an installed Steam or GOG "
        "version, a GOG installer file, or the two CDs.";

    constexpr const char* kRerunWelcomeText =
        "Choose a new copy of the game to install. Your settings and saves are kept.";

    constexpr float kWizardCanvasHeight = 480.0f;

    // The wizard's panel is wider than the Options screens and reserves no help column, so its
    // text gets the full width under the title.
    float WizardPanelWidth(const Skin& skin, float canvasWidth)
    {
        const float available = std::max(0.0f, canvasWidth - 2.0f * skin.canvasMargin);
        if ( skin.fixedPanelWidth > 0.0f )
        {
            return std::min(canvasWidth * skin.fixedPanelWidth, available);
        }
        return available;
    }

    // The width a row's text has after the panel padding and the focus marker inset, the same
    // numbers Ui uses to lay a row out.
    float WizardTextWidth(const Skin& skin, Painter& painter, float canvasWidth)
    {
        const float panelWidth = WizardPanelWidth(skin, canvasWidth);
        float width = std::max(0.0f, panelWidth - 2.0f * skin.panelPadding);
        if ( !skin.focusMarker.empty() )
        {
            width -= painter.measureText(skin.textFont, skin.focusMarker).width
                + skin.markerSpacing;
        }
        return width > 1.0f ? width : 1.0f;
    }

    std::size_t Utf8CodePointLength(unsigned char lead)
    {
        if ( lead < 0x80 )
        {
            return 1;
        }
        if ( (lead >> 5) == 0x6 )
        {
            return 2;
        }
        if ( (lead >> 4) == 0xE )
        {
            return 3;
        }
        if ( (lead >> 3) == 0x1E )
        {
            return 4;
        }
        return 1;
    }

    bool IsBlankLine(const std::string& line)
    {
        for ( const char chr : line )
        {
            if ( chr != ' ' && chr != '\t' && chr != '\r' )
            {
                return false;
            }
        }
        return true;
    }

    // Wraps every paragraph by measuring words with the painter. A word is never split; a word
    // wider than the column stays alone and is clipped by the painter. With collapseBlankLines
    // runs of blank lines become one blank line and leading/trailing blanks are dropped.
    std::vector<std::string> WrapParagraphs(Painter& painter, FontId font, float maxWidth,
        const std::string& text, bool bCollapseBlankLines)
    {
        std::vector<std::string> lines;
        bool bPreviousBlank = true;
        std::size_t start = 0;
        while ( start <= text.size() )
        {
            const std::size_t end = text.find('\n', start);
            std::string paragraph = text.substr(start,
                end == std::string::npos ? std::string::npos : end - start);
            if ( !paragraph.empty() && paragraph.back() == '\r' )
            {
                paragraph.pop_back();
            }

            if ( paragraph.empty() || IsBlankLine(paragraph) )
            {
                if ( !bCollapseBlankLines )
                {
                    lines.emplace_back();
                    bPreviousBlank = true;
                }
                else if ( !bPreviousBlank && !lines.empty() )
                {
                    lines.emplace_back();
                    bPreviousBlank = true;
                }
            }
            else
            {
                bPreviousBlank = false;
                std::string line;
                std::size_t cur = 0;
                while ( cur < paragraph.size() )
                {
                    while ( cur < paragraph.size()
                        && (paragraph[cur] == ' ' || paragraph[cur] == '\t') )
                    {
                        ++cur;
                    }

                    std::size_t wordEnd = cur;
                    while ( wordEnd < paragraph.size() && paragraph[wordEnd] != ' '
                        && paragraph[wordEnd] != '\t' )
                    {
                        ++wordEnd;
                    }

                    if ( wordEnd == cur )
                    {
                        break;
                    }

                    const std::string word = paragraph.substr(cur, wordEnd - cur);
                    std::string candidate = line.empty() ? word : line + " " + word;
                    if ( !line.empty()
                        && painter.measureText(font, candidate).width > maxWidth )
                    {
                        lines.push_back(line);
                        line = word;
                    }
                    else
                    {
                        line = std::move(candidate);
                    }

                    cur = wordEnd;
                }

                if ( !line.empty() )
                {
                    lines.push_back(std::move(line));
                }
                else if ( !bCollapseBlankLines )
                {
                    lines.emplace_back();
                }
            }

            if ( end == std::string::npos )
            {
                break;
            }
            start = end + 1;
        }

        if ( bCollapseBlankLines )
        {
            while ( !lines.empty() && lines.back().empty() )
            {
                lines.pop_back();
            }
        }

        return lines;
    }

    void AddWrappedLabels(Screen& screen, Painter& painter, FontId font, float maxWidth,
        const std::string& text, bool bCollapseBlankLines = false)
    {
        for ( const std::string& line : WrapParagraphs(painter, font, maxWidth, text,
            bCollapseBlankLines) )
        {
            screen.widgets.emplace_back(Label{ line, std::string() });
        }
    }

    // Elides the middle of a long path with "...", keeping its start and its file name. The
    // pieces are measured so the result fits maxWidth.
    std::string ElideMiddle(Painter& painter, FontId font, const std::string& text,
        float maxWidth)
    {
        if ( text.empty() || painter.measureText(font, text).width <= maxWidth )
        {
            return text;
        }

        const std::string ellipsis = "...";
        const float ellipsisWidth = painter.measureText(font, ellipsis).width;
        const std::size_t separator = text.find_last_of("/\\");
        const std::string suffix = separator == std::string::npos
            ? text : text.substr(separator + 1);

        if ( !suffix.empty()
            && painter.measureText(font, ellipsis + suffix).width <= maxWidth )
        {
            const float prefixBudget = maxWidth - ellipsisWidth
                - painter.measureText(font, suffix).width;
            std::string prefix;
            std::size_t cur = 0;
            while ( cur < text.size()
                && (separator == std::string::npos || cur < separator) )
            {
                const std::size_t length = Utf8CodePointLength(
                    static_cast<unsigned char>(text[cur]));
                const std::size_t next = std::min(text.size(), cur + length);
                const std::string candidate = text.substr(0, next);
                if ( painter.measureText(font, candidate).width > prefixBudget )
                {
                    break;
                }

                prefix = candidate;
                cur = next;
            }

            return prefix + ellipsis + suffix;
        }

        // Even the file name alone is too wide: keep the end of the text.
        std::vector<std::size_t> starts;
        for ( std::size_t cur = 0; cur < text.size(); )
        {
            starts.push_back(cur);
            cur += Utf8CodePointLength(static_cast<unsigned char>(text[cur]));
        }

        // The first candidate is the whole text after the ellipsis; drop one code point from
        // the front at a time until the longest suffix that fits is found.
        for ( std::size_t index = 1; index <= starts.size(); ++index )
        {
            const std::string candidate = ellipsis + text.substr(starts[index - 1]);
            if ( painter.measureText(font, candidate).width <= maxWidth )
            {
                return candidate;
            }
        }

        return ellipsis;
    }

    std::string PercentText(double percent)
    {
        char aText[16] = { 0 };
        std::snprintf(aText, sizeof(aText), "%d%%",
            static_cast<int>(std::clamp(percent, 0.0, 1.0) * 100.0 + 0.5));
        return aText;
    }

    std::string BytesText(int64_t done, int64_t total)
    {
        char aText[64] = { 0 };
        std::snprintf(aText, sizeof(aText), "%.1f / %.1f MB",
            static_cast<double>(done) / (1024.0 * 1024.0),
            static_cast<double>(total) / (1024.0 * 1024.0));
        return aText;
    }

    void AddQuitAndBack(Screen& screen)
    {
        screen.widgets.emplace_back(Button{ "Back", kActionBack, true });
        screen.widgets.emplace_back(Button{ "Quit", kActionQuit, true });
        screen.backResult = kActionBack;
    }

    Screen BuildWelcomeScreen(const JonesSetupWizard& wizard, Painter& painter, const Skin& skin,
        float textWidth)
    {
        Screen screen;
        screen.title = kWindowTitle;
        AddWrappedLabels(screen, painter, skin.textFont, textWidth,
            wizard.Rerun() ? kRerunWelcomeText : kWelcomeText);
        screen.widgets.emplace_back(Button{ "Continue", kActionContinue, true });
        screen.widgets.emplace_back(Button{ wizard.Rerun() ? "Back to the game" : "Quit",
            kActionQuit, true });
        screen.backResult = kActionQuit;
        return screen;
    }

    Screen BuildSourceScreen(const JonesSetupWizard& wizard, const std::string& title,
        Painter& painter, const Skin& skin, float textWidth)
    {
        Screen screen;
        screen.title = title;
        if ( !wizard.Message().empty() )
        {
            AddWrappedLabels(screen, painter, skin.textFont, textWidth, wizard.Message());
        }

        const std::vector<JonesSetupRow>& rows = wizard.Rows();
        for ( std::size_t i = 0; i < rows.size(); ++i )
        {
            // A source row is "<kind> - <path>"; keep the kind (and the incomplete marker) and
            // elide only the path, so a long folder or installer name never hides what the row is.
            std::string label = rows[i].label;
            const std::string incompleteMarker = " (incomplete)";
            const bool bIncomplete = label.size() >= incompleteMarker.size()
                && label.compare(label.size() - incompleteMarker.size(),
                    incompleteMarker.size(), incompleteMarker) == 0;
            if ( bIncomplete )
            {
                label.resize(label.size() - incompleteMarker.size());
            }

            std::string kind;
            const std::size_t kindEnd = label.find(" - ");
            if ( kindEnd != std::string::npos )
            {
                kind = label.substr(0, kindEnd + 3);
                label = label.substr(kindEnd + 3);
            }

            const float markerWidth = bIncomplete
                ? painter.measureText(skin.textFont, incompleteMarker).width : 0.0f;
            const float pathWidth = textWidth
                - painter.measureText(skin.textFont, kind).width - markerWidth;
            std::string shown = kind + ElideMiddle(painter, skin.textFont, label,
                std::max(1.0f, pathWidth));
            if ( bIncomplete )
            {
                shown += incompleteMarker;
            }

            screen.widgets.emplace_back(Button{ std::move(shown),
                static_cast<ActionId>(kActionRowBase + i), true });
        }

        AddQuitAndBack(screen);
        return screen;
    }

    Screen BuildLicenceScreenView(const JonesSetupWizard& wizard, const Skin& skin,
        const JonesSetupLicenceView& view)
    {
        (void)wizard;
        (void)skin;

        static const std::vector<std::string> emptyLines;
        const std::vector<std::string>& lines = view.pLines ? *view.pLines : emptyLines;

        Screen screen;
        screen.title = "Licence agreement";
        const std::size_t visible = view.visible;
        for ( std::size_t i = 0; i < visible; ++i )
        {
            const std::size_t line = view.offset + i;
            screen.widgets.emplace_back(Label{
                line < lines.size() ? lines[line] : std::string(),
                std::string() });
        }

        const std::size_t total = lines.size();
        const std::size_t first = total == 0 ? 0 : std::min(view.offset + 1, total);
        const std::size_t last = total == 0 ? 0 : std::min(view.offset + visible, total);
        screen.widgets.emplace_back(Label{
            "Lines " + std::to_string(first) + "-" + std::to_string(last)
                + " of " + std::to_string(total),
            std::string() });

        screen.widgets.emplace_back(Button{ "I accept", kActionAccept, true });
        screen.widgets.emplace_back(Button{ "Back", kActionBack, true });
        screen.widgets.emplace_back(Button{ "Quit", kActionQuit, true });
        screen.backResult = kActionBack;
        screen.initialFocus = visible + 1;
        return screen;
    }

    Screen BuildLocationScreen(const JonesSetupWizard& wizard, Painter& painter,
        const Skin& skin, float textWidth)
    {
        Screen screen;
        screen.title = "Where should Jones3D EX keep its files?";

        std::string userText = "Your user folder (recommended)";
        if ( !wizard.NextToGame() )
        {
            userText += " (selected)";
        }
        screen.widgets.emplace_back(Button{ userText, kActionChooseUser, true });
        screen.widgets.emplace_back(Label{
            "  " + ElideMiddle(painter, skin.textFont, wizard.UserDirForDisplay(),
                textWidth - painter.measureText(skin.textFont, "  ").width),
            std::string() });

        if ( wizard.NextToGameAvailable() )
        {
            std::string exeText = "Next to the game";
            if ( wizard.NextToGame() )
            {
                exeText += " (selected)";
            }
            screen.widgets.emplace_back(Button{ exeText, kActionChooseExeDir, true });
            screen.widgets.emplace_back(Label{
                "  " + ElideMiddle(painter, skin.textFont, wizard.ExeDir(),
                    textWidth - painter.measureText(skin.textFont, "  ").width),
                std::string() });
        }

        if ( wizard.CanCopySaves() )
        {
            screen.widgets.emplace_back(Toggle{ "Copy saved games", std::string(), true,
                wizard.CopySaves() });
        }

        screen.widgets.emplace_back(Button{ "Install", kActionContinue, true });
        AddQuitAndBack(screen);
        return screen;
    }

    Screen BuildProgressScreen(const JonesSetupWizard& wizard, Painter& painter,
        const Skin& skin, float textWidth)
    {
        const JonesSetupProgressState& progress = wizard.Progress();
        Screen screen;
        screen.title = "Installing...";
        screen.widgets.emplace_back(Label{ progress.phase,
            PercentText(progress.percent) });
        // The bar row is only a placeholder; the window draws the real bar over it.
        screen.widgets.emplace_back(Label{ std::string(), std::string() });

        const float fileValueWidth = std::max(1.0f,
            textWidth - painter.measureText(skin.textFont, "File:").width - 12.0f);
        screen.widgets.emplace_back(Label{ "File:",
            ElideMiddle(painter, skin.textFont, progress.file, fileValueWidth) });
        screen.widgets.emplace_back(Label{ std::string(),
            progress.bytesTotal > 0 ? BytesText(progress.bytesDone, progress.bytesTotal)
                                    : std::string() });
        screen.widgets.emplace_back(Button{ "Cancel", kActionCancel, true });
        screen.backResult = kActionCancel;
        return screen;
    }

    Screen BuildDoneScreen(const JonesSetupWizard& wizard, Painter& painter,
        const Skin& skin, float textWidth)
    {
        Screen screen;
        screen.title = "Setup complete";
        if ( wizard.Report().bComplete )
        {
            screen.widgets.emplace_back(Label{ "The game files are installed.", std::string() });
            screen.widgets.emplace_back(Label{ wizard.ReleaseText(), std::string() });
            screen.widgets.emplace_back(Button{ "Start the game", kActionStartGame, true });
            screen.backResult = kActionQuit;
        }
        else
        {
            screen.widgets.emplace_back(Label{ "The game files are not complete.",
                std::string() });
            const std::string missing = wizard.MissingText();
            if ( !missing.empty() )
            {
                AddWrappedLabels(screen, painter, skin.textFont, textWidth,
                    "Missing: " + missing + ".");
            }
            AddQuitAndBack(screen);
        }
        return screen;
    }

    Screen BuildOutcomeScreen(const JonesSetupWizard& wizard, bool bCancelled,
        Painter& painter, const Skin& skin, float textWidth)
    {
        Screen screen;
        screen.title = bCancelled ? "Setup cancelled" : "Setup failed";
        AddWrappedLabels(screen, painter, skin.textFont, textWidth, wizard.Message());
        AddQuitAndBack(screen);
        return screen;
    }

    // Draws the progress bar over the placeholder row: a track and a skin-coloured fill. Ui owns
    // the layout, so this repeats the panel/row positions Ui computes for a Top panel.
    void DrawProgressBar(Painter& painter, const Ui& ui, const Skin& skin, double percent)
    {
        const Size canvas = ui.canvasSize();
        const float panelWidth = WizardPanelWidth(skin, canvas.width);
        const float innerX = (canvas.width - panelWidth) * 0.5f + skin.panelPadding;
        const float innerWidth = std::max(0.0f, panelWidth - 2.0f * skin.panelPadding);
        const float rowsY = skin.topMargin + skin.panelPadding + skin.titleHeight;
        const float barTop = rowsY + skin.rowHeight + 2.0f * skin.rowSpacing;
        const float barHeight = std::max(6.0f, skin.rowHeight - 8.0f);
        const float barY = barTop + (skin.rowHeight - barHeight) * 0.5f;

        const Rect track{ innerX, barY, innerWidth, barHeight };
        painter.fillRect(track, skin.highlightUnfocused);

        // The shared screen skin's highlight is transparent, so the fill uses the focused/accent
        // text colour, which contrasts with the track.
        Rect fill = track;
        fill.width = innerWidth * static_cast<float>(std::clamp(percent, 0.0, 1.0));
        painter.fillRect(fill, skin.textFocused);
    }

    std::optional<babylon::font::Font> LoadFont(babylon::files::FileSystem& fs,
        const char* pName)
    {
        const std::string path = babylon::files::joinPath(fs.basePath(),
            std::string("Data/ui/fonts/") + pName);
        const std::unique_ptr<babylon::files::File> file =
            fs.open(path, babylon::files::OpenMode::Read);
        if ( !file )
        {
            return std::nullopt;
        }

        const std::optional<std::int64_t> size = file->size();
        if ( !size || *size < 0 )
        {
            return std::nullopt;
        }

        std::vector<std::byte> bytes(static_cast<std::size_t>(*size));
        if ( !bytes.empty() )
        {
            const std::optional<std::size_t> read = file->read(bytes);
            if ( !read || *read != bytes.size() )
            {
                return std::nullopt;
            }
        }

        return babylon::font::Font::load(std::move(bytes));
    }

    void ShowOk(babylon::platform::Platform& platform, const std::string& text)
    {
        const std::string_view buttons[] = { "OK" };
        static_cast<void>(platform.askChoice(kWindowTitle, text, buttons));
    }

    bool SetupConfigFallbackHasStarted(void)
    {
        return true;
    }

    int SetupConfigFallbackGetInt(const char*, int defaultValue)
    {
        return defaultValue;
    }

    float SetupConfigFallbackGetFloat(const char*, float defaultValue)
    {
        return defaultValue;
    }

    int SetupConfigFallbackGetStr(const char*, char* pDstStr, size_t size,
        const char* pDefaultValue)
    {
        if ( !pDstStr || size == 0 )
        {
            return 1;
        }

        const char* pValue = pDefaultValue ? pDefaultValue : "";
        std::snprintf(pDstStr, size, "%s", pValue);
        return 0;
    }

    // Added: a rerun installs <user dir>/GameData, but an old installPath in Jones.cfg would win
    // the next data search. Clearing it lets the new GameData folder be found.
    void ClearInstallPathAfterRerun(const std::string& userDir)
    {
        const bool bWasStarted = stdConfig_HasStarted();
        if ( !bWasStarted )
        {
            const StdConfigFallback fallback = { SetupConfigFallbackHasStarted,
                SetupConfigFallbackGetInt, SetupConfigFallbackGetFloat,
                SetupConfigFallbackGetStr };
            stdConfig_SetFallback(&fallback);

            const std::filesystem::path configPath = userDir.empty()
                ? JonesUtf8Path_From("Jones.cfg")
                : JonesUtf8Path_From(userDir) / JonesUtf8Path_From("Jones.cfg");
            if ( !stdConfig_Startup(JonesUtf8Path_To(configPath).c_str()) )
            {
                return;
            }
        }

        static_cast<void>(stdConfig_Remove(SITH_CFG_INSTALLPATH));

        if ( !bWasStarted )
        {
            stdConfig_Shutdown();
        }
    }

    JonesSetupResult RunFallback(babylon::platform::Platform& platform,
        const JonesSetupHost& host, babylon::process::Process& process,
        const std::string& userDir, const std::string& exeDir, std::string& outUserDir,
        std::string& outDataDir, bool bRerun, const std::string& toolDir)
    {
        JonesSetupWizard wizard(platform, process, host.installHost, userDir, exeDir, bRerun,
            toolDir);
        wizard.Start();
        wizard.Continue();

        for ( ;; )
        {
            std::vector<std::string> labels;
            labels.push_back("Choose game folder...");
            if ( !wizard.InnoextractPath().empty() )
            {
                labels.push_back("Choose GOG installer...");
            }
            labels.push_back(bRerun ? "Back to the game" : "Quit");

            std::vector<std::string_view> buttons;
            buttons.reserve(labels.size());
            for ( const std::string& label : labels )
            {
                buttons.push_back(label);
            }

            const std::optional<std::size_t> choice = platform.askChoice(kWindowTitle,
                bRerun ? kRerunWelcomeText : kWelcomeText, buttons);
            if ( !choice )
            {
                return JonesSetupResult::Failed;
            }
            if ( *choice + 1 >= buttons.size() )
            {
                return JonesSetupResult::Cancelled;
            }

            const bool bGog = *choice == 1;
            babylon::platform::PathChoice path;
            if ( bGog )
            {
                const babylon::platform::FileFilter filters[] = {
                    { "GOG installer", "exe" },
                    { "All files", "*" },
                };
                path = platform.chooseFile("Choose a GOG installer file", userDir, filters);
            }
            else
            {
                path = platform.chooseFolder("Choose a game folder", userDir);
            }

            if ( path.result == babylon::platform::PathChoiceResult::Unavailable )
            {
                ShowOk(platform, "No file dialog is available on this system.");
                return JonesSetupResult::Failed;
            }

            if ( path.result == babylon::platform::PathChoiceResult::Cancelled )
            {
                continue;
            }

            const bool bChosen = bGog
                ? wizard.ChooseInstaller(path.path) : wizard.ChooseFolder(path.path);
            if ( !bChosen && wizard.Step() == JonesSetupStep::Source )
            {
                ShowOk(platform, wizard.Message());
                continue;
            }

            if ( wizard.Step() == JonesSetupStep::Licence )
            {
                std::string text = wizard.LicenceText().substr(0, 2000);
                const std::string_view licenceButtons[] = { "I accept", "Quit" };
                const std::optional<std::size_t> answer = platform.askChoice(
                    "Licence agreement", text, licenceButtons);
                if ( !answer )
                {
                    return JonesSetupResult::Failed;
                }
                if ( *answer != 0 )
                {
                    return JonesSetupResult::Cancelled;
                }
                wizard.AcceptLicence();
            }

            if ( bRerun )
            {
                if ( wizard.Step() != JonesSetupStep::Progress )
                {
                    ShowOk(platform, wizard.Message());
                    continue;
                }
            }
            else
            {
                if ( wizard.Step() != JonesSetupStep::Location )
                {
                    ShowOk(platform, wizard.Message());
                    continue;
                }

                wizard.SetNextToGame(false);
            }

            wizard.StartInstall();

            if ( wizard.Step() == JonesSetupStep::Done && wizard.Report().bComplete )
            {
                ShowOk(platform, "The game files are installed.");
                outUserDir = wizard.SelectedUserDir();
                outDataDir = wizard.TargetDataDir();
                return JonesSetupResult::Installed;
            }

            if ( wizard.Step() == JonesSetupStep::Done )
            {
                ShowOk(platform, "Missing: " + wizard.MissingText() + ".");
            }
            else
            {
                ShowOk(platform, wizard.Message());
            }

            return JonesSetupResult::Failed;
        }
    }

    JonesSetupResult RunWindowed(babylon::platform::Platform& platform,
        babylon::files::FileSystem& fs, const JonesSetupHost& host,
        babylon::process::Process& process, const std::string& userDir,
        const std::string& exeDir, std::string& outUserDir, std::string& outDataDir,
        bool bRerun, const std::string& toolDir)
    {
        const babylon::platform::WindowDesc desc{ kWindowTitle, { 960, 640 }, true };
        std::unique_ptr<babylon::platform::Window> pWindow = platform.createWindow(desc);
        if ( !pWindow )
        {
            return RunFallback(platform, host, process, userDir, exeDir, outUserDir,
                outDataDir, bRerun, toolDir);
        }

        std::unique_ptr<babylon::render::Device> pDevice =
            babylon::render::createSdlGpuDevice(pWindow.get());
        if ( !pDevice )
        {
            pWindow.reset();
            return RunFallback(platform, host, process, userDir, exeDir, outUserDir,
                outDataDir, bRerun, toolDir);
        }

        babylon::uirender::RenderPainter painter(*pDevice);

        // Bake the fonts at the wizard's real pixel scale before measuring or
        // building any screen, so the first frame is not a 1x bake stretched.
        const babylon::platform::Size initialSize = pWindow->size();
        if ( initialSize.height > 0 )
        {
            static_cast<void>(painter.setPixelScale(
                (float)initialSize.height / kWizardCanvasHeight));
        }

        const std::optional<babylon::font::Font> titleFont = LoadFont(fs, "Cinzel[wght].ttf");
        const std::optional<babylon::font::Font> rowFont = LoadFont(fs, "Jost[wght].ttf");
        const FontId titleId = titleFont ? painter.addFont(*titleFont, 22.0f) : 0;
        const FontId rowId = rowFont ? painter.addFont(*rowFont, 16.0f) : 0;

        babylon::cvars::Registry cvars;
        Ui ui(painter, cvars);
        // Same navigation as the game's menus: the default map. A click focuses a row and a
        // second click accepts it, exactly like JonesUiModal's screens.
        const NavigationMap navigation;
        static_cast<void>(ui.setNavigation(navigation));

        const Skin skin = JonesSetupWindow_CreateSkin(titleId, rowId, &painter);
        static_cast<void>(ui.setSkin(skin));

        static_cast<void>(ui.setViewport(initialSize.width, initialSize.height));

        JonesSetupWizard wizard(platform, process, host.installHost, userDir, exeDir, bRerun,
            toolDir);
        wizard.Start();

        JonesSetupStep shownStep = wizard.Step();
        const std::size_t licenceVisible = JonesSetupWindow_LicenceVisibleLines(skin);
        std::vector<std::string> licenceLines;
        std::size_t licenceOffset = 0;

        ScreenId screenId = ui.push(JonesSetupWindow_BuildScreen(wizard, painter,
            ui.canvasSize().width, skin));
        if ( screenId == kInvalidScreen )
        {
            return JonesSetupResult::Failed;
        }

        JonesSetupResult result = JonesSetupResult::Cancelled;
        bool bDone = false;
        bool bPendingInstall = false;
        std::size_t pendingFocus = babylon::ui::kNoWidget;

        const babylon::platform::WindowId windowId = pWindow->id();
        auto lastUpdate = std::chrono::steady_clock::now();
        auto lastDraw = lastUpdate - std::chrono::seconds(1);
        int lastWidth = initialSize.width;
        int lastHeight = initialSize.height;

        auto LicenceHint = [&]() -> std::string
        {
            const std::size_t total = licenceLines.size();
            const std::size_t first = total == 0
                ? 0 : std::min(licenceOffset + 1, total);
            const std::size_t last = total == 0
                ? 0 : std::min(licenceOffset + licenceVisible, total);
            return "Lines " + std::to_string(first) + "-" + std::to_string(last)
                + " of " + std::to_string(total);
        };

        auto UpdateLicenceWidgets = [&]()
        {
            if ( screenId == kInvalidScreen || wizard.Step() != JonesSetupStep::Licence )
            {
                return;
            }

            for ( std::size_t i = 0; i < licenceVisible; ++i )
            {
                const std::size_t line = licenceOffset + i;
                static_cast<void>(ui.setWidget(screenId, i, Label{
                    line < licenceLines.size() ? licenceLines[line] : std::string(),
                    std::string() }));
            }

            static_cast<void>(ui.setWidget(screenId, licenceVisible,
                Label{ LicenceHint(), std::string() }));
        };

        auto ScrollLicence = [&](long delta)
        {
            if ( wizard.Step() != JonesSetupStep::Licence )
            {
                return;
            }

            const std::size_t maxOffset = licenceLines.size() > licenceVisible
                ? licenceLines.size() - licenceVisible : 0;
            const long current = static_cast<long>(licenceOffset);
            const long next = std::clamp(current + delta, 0L,
                static_cast<long>(maxOffset));
            if ( static_cast<std::size_t>(next) != licenceOffset )
            {
                licenceOffset = static_cast<std::size_t>(next);
                UpdateLicenceWidgets();
            }
        };

        auto SendTab = [&](int count)
        {
            for ( int i = 0; i < count; ++i )
            {
                ui.handleEvent(babylon::platform::KeyChanged{ windowId,
                    babylon::platform::Key::Tab, true, false });
                ui.handleEvent(babylon::platform::KeyChanged{ windowId,
                    babylon::platform::Key::Tab, false, false });
            }
        };

        // Up/Down/PageUp/PageDown and the wheel always scroll the licence text; Left/Right move
        // between its three buttons. Everything else goes to Ui unchanged.
        auto HandleLicenceEvent = [&](const babylon::platform::Event& event) -> bool
        {
            if ( wizard.Step() != JonesSetupStep::Licence || screenId == kInvalidScreen )
            {
                return false;
            }

            if ( const babylon::platform::KeyChanged* pKey =
                std::get_if<babylon::platform::KeyChanged>(&event) )
            {
                if ( !pKey->down )
                {
                    return false;
                }

                switch ( pKey->key )
                {
                    case babylon::platform::Key::Up:
                        ScrollLicence(-1);
                        return true;

                    case babylon::platform::Key::Down:
                        ScrollLicence(1);
                        return true;

                    case babylon::platform::Key::PageUp:
                        ScrollLicence(-static_cast<long>(licenceVisible));
                        return true;

                    case babylon::platform::Key::PageDown:
                        ScrollLicence(static_cast<long>(licenceVisible));
                        return true;

                    case babylon::platform::Key::Left:
                        SendTab(2);
                        return true;

                    case babylon::platform::Key::Right:
                        SendTab(1);
                        return true;

                    default:
                        return false;
                }
            }

            if ( const babylon::platform::MouseWheelMoved* pWheel =
                std::get_if<babylon::platform::MouseWheelMoved>(&event) )
            {
                ScrollLicence(pWheel->dy > 0.0f ? -1 : 1);
                return true;
            }

            return false;
        };

        const std::function<void(bool)> frame = [&](bool bInstalling)
        {
            bool bClose = false;
            while ( const std::optional<babylon::platform::Event> event =
                platform.pollEvent() )
            {
                if ( std::holds_alternative<babylon::platform::QuitRequested>(*event) )
                {
                    bClose = true;
                }
                else if ( const babylon::platform::WindowCloseRequested* pClose =
                    std::get_if<babylon::platform::WindowCloseRequested>(&*event) )
                {
                    if ( pClose->window == windowId )
                    {
                        bClose = true;
                    }
                }
                else if ( const babylon::platform::WindowResized* pResized =
                    std::get_if<babylon::platform::WindowResized>(&*event) )
                {
                    if ( pResized->window == windowId )
                    {
                        lastWidth = pResized->size.width;
                        lastHeight = pResized->size.height;
                        static_cast<void>(ui.setViewport(lastWidth, lastHeight));
                    }
                }

                if ( !HandleLicenceEvent(*event) )
                {
                    ui.handleEvent(*event);
                }
            }

            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - lastUpdate).count();
            lastUpdate = now;
            ui.update(elapsed);

            while ( const std::optional<UiEvent> event = ui.pollEvent() )
            {
                if ( const ActionPressed* pAction = std::get_if<ActionPressed>(&*event) )
                {
                    if ( pAction->screen != screenId )
                    {
                        continue;
                    }

                    if ( bInstalling )
                    {
                        if ( pAction->action == kActionCancel )
                        {
                            wizard.RequestCancel();
                        }
                        continue;
                    }

                    if ( pAction->action == kActionContinue )
                    {
                        if ( wizard.Step() == JonesSetupStep::Welcome )
                        {
                            wizard.Continue();
                        }
                        else
                        {
                            bPendingInstall = true;
                        }
                    }
                    else if ( pAction->action == kActionQuit )
                    {
                        bDone = true;
                        result = JonesSetupResult::Cancelled;
                    }
                    else if ( pAction->action == kActionBack )
                    {
                        wizard.Back();
                        if ( screenId )
                        {
                            static_cast<void>(ui.pop(0));
                            screenId = 0;
                        }
                    }
                    else if ( pAction->action == kActionAccept )
                    {
                        wizard.AcceptLicence();
                        if ( wizard.Rerun() && wizard.TakeRerunRun() )
                        {
                            bPendingInstall = true;
                        }
                        static_cast<void>(ui.pop(0));
                        screenId = 0;
                    }
                    else if ( pAction->action == kActionCancel )
                    {
                        wizard.RequestCancel();
                    }
                    else if ( pAction->action == kActionStartGame )
                    {
                        bDone = true;
                        result = JonesSetupResult::Installed;
                    }
                    else if ( pAction->action == kActionChooseUser )
                    {
                        // The screen is rebuilt on the same step; keep focus on the row the
                        // player just chose instead of falling back to the first row.
                        pendingFocus = 0;
                        wizard.SetNextToGame(false);
                        static_cast<void>(ui.pop(0));
                        screenId = 0;
                    }
                    else if ( pAction->action == kActionChooseExeDir )
                    {
                        pendingFocus = 2;
                        wizard.SetNextToGame(true);
                        static_cast<void>(ui.pop(0));
                        screenId = 0;
                    }
                    else if ( pAction->action >= kActionRowBase )
                    {
                        const std::size_t row = pAction->action - kActionRowBase;
                        const JonesSetupStep stepBefore = wizard.Step();
                        wizard.SelectRow(row);

                        if ( wizard.Step() == JonesSetupStep::Disc2
                            && wizard.TakeDisc2Run() )
                        {
                            bPendingInstall = true;
                        }

                        if ( wizard.Rerun() && wizard.Step() == JonesSetupStep::Progress
                            && wizard.TakeRerunRun() )
                        {
                            bPendingInstall = true;
                        }

                        pendingFocus = babylon::ui::kNoWidget;
                        if ( wizard.Step() == stepBefore && !bPendingInstall )
                        {
                            // Source rows with a message are shifted down by the wrapped message
                            // lines, so map the row to its widget index in the rebuilt screen.
                            std::size_t messageLines = 0;
                            if ( !wizard.Message().empty() )
                            {
                                messageLines = WrapParagraphs(painter, skin.textFont,
                                    WizardTextWidth(skin, painter,
                                        ui.canvasSize().width),
                                    wizard.Message(), false).size();
                            }
                            pendingFocus = messageLines + row;
                        }

                        if ( screenId )
                        {
                            static_cast<void>(ui.pop(0));
                            screenId = 0;
                        }
                    }
                }
                else if ( const ValueChanged* pChanged = std::get_if<ValueChanged>(&*event) )
                {
                    if ( !bInstalling && wizard.Step() == JonesSetupStep::Location )
                    {
                        if ( const bool* pValue = std::get_if<bool>(&pChanged->value) )
                        {
                            wizard.SetCopySaves(*pValue);
                            // If a subsequent same-step rebuild happens, keep the toggle focused.
                            pendingFocus = wizard.NextToGameAvailable() ? 4 : 2;
                        }
                    }
                }
                else if ( const ScreenClosed* pClosed = std::get_if<ScreenClosed>(&*event) )
                {
                    if ( pClosed->screen != screenId )
                    {
                        continue;
                    }

                    screenId = 0;
                    if ( bInstalling )
                    {
                        if ( pClosed->result == kActionCancel
                            || pClosed->result == kActionBack )
                        {
                            wizard.RequestCancel();
                        }
                        else if ( pClosed->result == kActionQuit )
                        {
                            bDone = true;
                            result = JonesSetupResult::Cancelled;
                        }
                    }
                    else if ( pClosed->result == kActionBack )
                    {
                        wizard.Back();
                    }
                    else if ( pClosed->result == kActionQuit )
                    {
                        bDone = true;
                        result = JonesSetupResult::Cancelled;
                    }
                    else if ( pClosed->result == kActionCancel )
                    {
                        wizard.RequestCancel();
                    }
                }
            }

            if ( bDone )
            {
                return;
            }

            if ( bClose )
            {
                if ( bInstalling || wizard.Step() == JonesSetupStep::Progress )
                {
                    wizard.RequestCancel();
                }
                else
                {
                    bDone = true;
                    result = JonesSetupResult::Cancelled;
                    return;
                }
            }

            if ( !bInstalling )
            {
                const JonesSetupStep newStep = wizard.Step();
                if ( screenId == kInvalidScreen || shownStep != newStep )
                {
                    if ( screenId != kInvalidScreen )
                    {
                        static_cast<void>(ui.pop(0));
                        screenId = kInvalidScreen;
                    }

                    if ( newStep == JonesSetupStep::Licence
                        && shownStep != JonesSetupStep::Licence )
                    {
                        licenceLines = JonesSetupWindow_WrapLicenceText(
                            wizard.LicenceText(), painter, skin, ui.canvasSize().width);
                        licenceOffset = 0;
                    }

                    const std::size_t focus = shownStep == newStep
                        ? pendingFocus : babylon::ui::kNoWidget;
                    shownStep = newStep;
                    if ( newStep == JonesSetupStep::Licence )
                    {
                        const JonesSetupLicenceView licenceView{ &licenceLines,
                            licenceOffset, licenceVisible };
                        screenId = ui.push(JonesSetupWindow_BuildLicenceScreen(wizard,
                            skin, licenceView));
                    }
                    else
                    {
                        screenId = ui.push(JonesSetupWindow_BuildScreen(wizard, painter,
                            ui.canvasSize().width, skin, focus));
                    }
                    pendingFocus = babylon::ui::kNoWidget;
                }
            }

            if ( screenId != kInvalidScreen
                && wizard.Step() == JonesSetupStep::Progress )
            {
                const JonesSetupProgressState& progress = wizard.Progress();
                const float textWidth = WizardTextWidth(skin, painter,
                    ui.canvasSize().width);
                const float fileValueWidth = std::max(1.0f, textWidth
                    - painter.measureText(skin.textFont, "File:").width - 12.0f);

                static_cast<void>(ui.setWidget(screenId, kProgressPhase,
                    Label{ progress.phase, PercentText(progress.percent) }));
                static_cast<void>(ui.setWidget(screenId, kProgressBar,
                    Label{ std::string(), std::string() }));
                static_cast<void>(ui.setWidget(screenId, kProgressFile,
                    Label{ "File:", ElideMiddle(painter, skin.textFont, progress.file,
                        fileValueWidth) }));
                static_cast<void>(ui.setWidget(screenId, kProgressBytes,
                    Label{ std::string(), progress.bytesTotal > 0
                        ? BytesText(progress.bytesDone, progress.bytesTotal)
                        : std::string() }));
            }

            if ( now - lastDraw >= std::chrono::milliseconds(33) )
            {
                lastDraw = now;

                const babylon::platform::Size size = pWindow->size();
                if ( size.width != lastWidth || size.height != lastHeight )
                {
                    lastWidth = size.width;
                    lastHeight = size.height;
                    static_cast<void>(ui.setViewport(lastWidth, lastHeight));
                }

                if ( size.width > 0 && size.height > 0
                    && painter.begin(static_cast<std::uint32_t>(size.width),
                        static_cast<std::uint32_t>(size.height)) )
                {
                    const Size canvas = ui.canvasSize();
                    painter.fillRect(Rect{ 0.0f, 0.0f, canvas.width, canvas.height },
                        Color{ 0.055f, 0.06f, 0.08f, 1.0f });
                    ui.draw();
                    if ( wizard.Step() == JonesSetupStep::Progress )
                    {
                        JonesSetupWindow_DrawProgressBar(painter, ui, skin, wizard.Progress().percent);
                    }
                    static_cast<void>(painter.end());
                }
            }
        };

        wizard.SetPump([&frame]() { frame(true); });

        while ( !bDone )
        {
            frame(false);
            if ( bDone )
            {
                break;
            }

            if ( bPendingInstall )
            {
                bPendingInstall = false;

                if ( screenId != kInvalidScreen )
                {
                    static_cast<void>(ui.pop(0));
                    screenId = kInvalidScreen;
                }

                wizard.BeginInstall();
                shownStep = wizard.Step();
                screenId = ui.push(JonesSetupWindow_BuildScreen(wizard, painter,
                    ui.canvasSize().width, skin));
                wizard.RunInstall();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if ( result == JonesSetupResult::Installed )
        {
            outUserDir = wizard.SelectedUserDir();
            outDataDir = wizard.TargetDataDir();
        }

        return result;
    }
}

JonesSetupResult JonesSetupWindow_Run(babylon::platform::Platform& platform,
    babylon::files::FileSystem& fs, const std::string& userDir, const std::string& exeDir,
    std::string& outUserDir, std::string& outDataDir, bool bRerun, const std::string& toolDir)
{
    // Added: the install engine reads and copies through the std adapter, which needs both the host
    // services and the file system. The game starts them later in JonesMain_Startup and stops them
    // again in JonesMain_Shutdown, so every call installs them, including a rerun after a game run.
    static tHostServices s_setupHostServices;
    static_cast<void>(stdPlatform_InitServices(&s_setupHostServices));
    stdStartup(&s_setupHostServices);

    StdBabylonContext setupContext = {};
    setupContext.pFileSystem = &fs;
    stdBabylon_SetContext(&setupContext);

    JonesSetupHost host;
    JonesSetupHost_Init(host);

    std::unique_ptr<babylon::process::Process> pProcess =
        babylon::process::createSdlProcess();
    JonesSetupResult result;
    if ( pProcess )
    {
        result = RunWindowed(platform, fs, host, *pProcess, userDir, exeDir, outUserDir,
            outDataDir, bRerun, toolDir);
    }
    else
    {
        babylon::process::NullProcess nullProcess;
        result = RunFallback(platform, host, nullProcess, userDir, exeDir, outUserDir,
            outDataDir, bRerun, toolDir);
    }

    if ( bRerun && result == JonesSetupResult::Installed )
    {
        ClearInstallPathAfterRerun(outUserDir);
    }

    stdBabylon_SetContext(nullptr);
    return result;
}

babylon::ui::Skin JonesSetupWindow_CreateSkin(babylon::ui::FontId titleFont,
    babylon::ui::FontId rowFont, const babylon::ui::Painter* pPainter)
{
    babylon::ui::Skin skin = JonesUiSkin_Create(titleFont, rowFont, pPainter);

    // A wide, centred panel with no help column: the wizard's text gets about 85% of the canvas
    // width under the title, the rest going to the panel padding and the focus marker.
    skin.placement = babylon::ui::PanelPlacement::Top;
    skin.fixedPanelWidth = 0.92f;
    skin.helpColumnWidth = 0.0f;
    skin.minPanelWidth = 0.0f;
    skin.topMargin = 12.0f;
    skin.canvasMargin = 12.0f;
    skin.panelPadding = 10.0f;
    skin.rowSpacing = 2.0f;

    float rowLineHeight = 0.0f;
    float titleLineHeight = 0.0f;
    if ( pPainter )
    {
        rowLineHeight = pPainter->measureText(skin.textFont, "").height;
        titleLineHeight = pPainter->measureText(skin.titleFont, "").height;
    }

    skin.rowHeight = rowLineHeight > 0.0f
        ? std::max(18.0f, rowLineHeight + 2.0f) : 20.0f;
    skin.titleHeight = titleLineHeight > 0.0f
        ? std::max(22.0f, titleLineHeight + 4.0f) : 26.0f;
    return skin;
}

std::size_t JonesSetupWindow_LicenceVisibleLines(const babylon::ui::Skin& skin)
{
    const float rowsY = skin.topMargin + skin.panelPadding + skin.titleHeight;
    const float rowsBottom = kWizardCanvasHeight - skin.canvasMargin;
    const float rowStep = skin.rowHeight + skin.rowSpacing;
    if ( !(rowStep > 0.0f) )
    {
        return 12;
    }

    // Four non-text rows: the position hint and the three buttons.
    const float textRows = std::max(0.0f, rowsBottom - rowsY - 4.0f * rowStep);
    const int visible = static_cast<int>(textRows / rowStep);
    return static_cast<std::size_t>(std::clamp(visible, 6, 12));
}

std::vector<std::string> JonesSetupWindow_WrapLicenceText(const std::string& text,
    babylon::ui::Painter& painter, const babylon::ui::Skin& skin, float canvasWidth)
{
    return WrapParagraphs(painter, skin.textFont,
        WizardTextWidth(skin, painter, canvasWidth), text, true);
}

babylon::ui::Screen JonesSetupWindow_BuildLicenceScreen(const JonesSetupWizard& wizard,
    const babylon::ui::Skin& skin, const JonesSetupLicenceView& view)
{
    return BuildLicenceScreenView(wizard, skin, view);
}

void JonesSetupWindow_DrawProgressBar(babylon::ui::Painter& painter, const babylon::ui::Ui& ui,
    const babylon::ui::Skin& skin, double fraction)
{
    DrawProgressBar(painter, ui, skin, fraction);
}

babylon::ui::Screen JonesSetupWindow_BuildScreen(const JonesSetupWizard& wizard,
    babylon::ui::Painter& painter, float canvasWidth, const babylon::ui::Skin& skin,
    std::size_t initialFocus)
{
    const float textWidth = WizardTextWidth(skin, painter, canvasWidth);
    Screen screen;

    switch ( wizard.Step() )
    {
        case JonesSetupStep::Welcome:
            screen = BuildWelcomeScreen(wizard, painter, skin, textWidth);
            break;

        case JonesSetupStep::Source:
            screen = BuildSourceScreen(wizard, "Where is your copy of the game?", painter,
                skin, textWidth);
            break;

        case JonesSetupStep::Licence:
        {
            JonesSetupLicenceView view;
            const std::vector<std::string> lines = JonesSetupWindow_WrapLicenceText(
                wizard.LicenceText(), painter, skin, canvasWidth);
            view.pLines = &lines;
            view.offset = 0;
            view.visible = JonesSetupWindow_LicenceVisibleLines(skin);
            screen = JonesSetupWindow_BuildLicenceScreen(wizard, skin, view);
            break;
        }

        case JonesSetupStep::Location:
            screen = BuildLocationScreen(wizard, painter, skin, textWidth);
            break;

        case JonesSetupStep::Progress:
            screen = BuildProgressScreen(wizard, painter, skin, textWidth);
            break;

        case JonesSetupStep::Disc2:
            screen = BuildSourceScreen(wizard, "Insert disc 2, then choose it.", painter,
                skin, textWidth);
            break;

        case JonesSetupStep::Done:
            screen = BuildDoneScreen(wizard, painter, skin, textWidth);
            break;

        case JonesSetupStep::Cancelled:
            screen = BuildOutcomeScreen(wizard, true, painter, skin, textWidth);
            break;

        case JonesSetupStep::Error:
        default:
            screen = BuildOutcomeScreen(wizard, false, painter, skin, textWidth);
            break;
    }

    if ( initialFocus != babylon::ui::kNoWidget )
    {
        screen.initialFocus = initialFocus;
    }

    return screen;
}
