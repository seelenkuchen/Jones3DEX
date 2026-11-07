// Added: the windowed build's scripted tour: an optional InputScript whose frame number is the
// present count, a present log, and screenshots after named presents. The composition root parses
// the options and configures the tour before the run; stdBabylonEvents_Pump replaces the platform's
// key, mouse, gamepad and text events with the script's while it is active, and stdDisplay_Update
// reports every successful present here. Adapter code, C++ only: no Fixed/Added markers inside.
#ifndef STD_STDBABYLONTOUR_H
#define STD_STDBABYLONTOUR_H
#ifndef __cplusplus
#error "stdBabylonTour.h is C++ only: it names babylon::input and std::span."
#endif

#include <babylon/input/InputScript.h>
#include <babylon/platform/Event.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The command-line options of the scripted tour. Empty strings mean "not given".
struct StdBabylonTourOptions
{
    std::string inputPath;
    std::string screenshotDir;
    std::vector<std::uint64_t> screenshotFrames;
    std::string presentLogPath;
    bool bQuitAfterScript = false;
};

// Takes --input <script>, --screenshot-dir <dir>, --screenshot-at <n>[,<n>...],
// --present-log <file> and --quit-after-script out of the command line. Every recognized option
// is removed from argv, the remaining arguments keep their order, *pArgc is reduced to match and
// argv[*pArgc] is set to nullptr; the last occurrence of a value option wins. Returns false,
// changing nothing, when a value is missing or invalid, when --screenshot-at and
// --screenshot-dir are not both given, or when --quit-after-script has no --input. argv[0] is
// never looked at.
bool stdBabylonTour_TakeOptions(int* pArgc, char** argv, StdBabylonTourOptions& rOptions,
    std::string& rError);

// Reads the input script and opens the present log, and makes the screenshot directory. The
// script is parsed completely here, so a malformed script fails before any engine code runs.
// Returns false with a message in rError; leaves the tour disabled. Window events the script
// carries use window. Configuring resets the present count to 0.
bool stdBabylonTour_Configure(const StdBabylonTourOptions& rOptions,
    babylon::platform::WindowId window, std::string& rError);

// Closes the present log and disables the tour. Safe to call at any time.
void stdBabylonTour_Shutdown();

// True when Configure succeeded (a script, a present log or screenshots are active).
bool stdBabylonTour_IsEnabled();

// The number of presents reported since Configure; 0 before the first one.
std::uint64_t stdBabylonTour_GetPresentCount();

// True while the input script still has frames to drive. The pump then drops the platform's
// key, mouse, gamepad and text events and injects this present count's script events instead;
// window, focus, resize and quit events still come from the platform.
bool stdBabylonTour_InputActive();

// The script events of the current present count, or an empty span when they were already taken
// at this count (the pump may run more than once per present). Only valid while InputActive().
std::span<const babylon::platform::Event> stdBabylonTour_TakeInputEvents();

// True once, after the script's last frame has been presented, when --quit-after-script was
// given. An empty script has no frames and counts as ended at once.
bool stdBabylonTour_TakeQuitAfterScript();

// Called after every successful present: increments the present count, writes the present log
// line, and marks a requested screenshot due. Does nothing while the tour is not configured.
void stdBabylonTour_OnPresent();

// The path <screenshot-dir>/frame-<n>.bmp of a requested screenshot, or nullptr. The caller
// writes the frame and the pointer stays valid until the next present.
const char* stdBabylonTour_TakeScreenshotPath();

// The modal loop pushes the title of the screen it presents and pops it when the screen closes;
// the present log names the innermost title, or "game" without a screen.
void stdBabylonTour_PushScreenTitle(const char* pTitle);
void stdBabylonTour_PopScreenTitle();

#endif // STD_STDBABYLONTOUR_H
