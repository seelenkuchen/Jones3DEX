// Added: the Babylon column's modal screen loop (ADR-0026). The recovered jonesConfig_* wrappers
// are blocking: each runs a frame loop around one babylon::ui screen until it closes and returns
// the original dialog's result. This loop is that frame loop. The game simulation does not run
// while it does; it is entered from inside the caller, exactly where the Win32 modal dialog blocked.
//
// Each iteration pumps the column's one event pump, feeds the raw platform events to Ui::handleEvent,
// advances Ui with the elapsed platform time, drains the Ui events, draws the frozen last game frame
// dimmed with the screen on top, and presents. A button action closes the screen with the action id
// as its result, and back closes it with Screen::backResult; the result is then mapped to the
// wrapper's own return value. A quit or game-window close also closes the top screen with its back
// result, but leaves the request flagged so the game quits as it would after the dialog.
//
// Nested screens work because the caller passes the same Ui: an inner loop pushes and pops only its
// own screen and the outer one stays below it, unchanged.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIMODAL_H
#define JONES3D_PLATFORM_BABYLON_JONESUIMODAL_H
#ifndef __cplusplus
#error "JonesUiModal.h is C++ only: it names babylon::ui types."
#endif

#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>

#include <functional>

// An image drawn behind a screen instead of the frozen game frame, cover-scaled to the canvas:
// the image keeps its aspect and fills the whole canvas, cropping the overhang (not dimmed). image
// is registered with the same JonesUiPainter; the caller keeps it alive and releases it. A zero
// image keeps the frozen-frame or clear behavior. width and height are the image's pixel size.
struct JonesUiModalBackground
{
    babylon::ui::ImageId image = 0;
    float width = 0.0f;
    float height = 0.0f;
};

// Maps the result a screen closed with to the wrapper's original return value.
using JonesUiModalResultMapper = std::function<int(babylon::ui::ResultCode)>;

// Consumes one Ui event before the loop applies its default handling. Return true when the event
// was handled; return false to let the loop pop a button action or report the close as usual. A
// handler may push and run a nested screen through the same Ui, or pop the top screen itself.
using JonesUiModalEventHandler = std::function<bool(babylon::ui::Ui& ui,
    babylon::ui::ScreenId screen, const babylon::ui::UiEvent& event)>;

// Pushes a copy of screen on top of ui's screen stack and runs until that screen closes. pPainter
// must be the painter ui draws through, or nullptr to run without drawing; when it is a
// JonesUiPainter and the display is open, the current back buffer is captured once, drawn full
// screen, dimmed with the skin's dim color, and the screen is drawn over it. When background
// names an image, that image is cover-scaled across the canvas instead of the frozen frame and is
// not dimmed. Every resource the loop creates is freed before it returns. Returns mapResult(result); a screen Ui::push rejects
// returns mapResult(screen.backResult) without entering the loop. Requires the std Babylon adapter
// context (the event pump and the platform clock), like every other Babylon-column loop.
int JonesUiModal_Run(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalBackground& background = JonesUiModalBackground{});

// JonesUiModal_Run with an event handler for screens that need more than one step, such as an
// overwrite prompt opened after a list row is accepted. handler is called for every Ui event
// before the loop's own action and close handling; nullptr runs exactly like JonesUiModal_Run.
int JonesUiModal_RunHandled(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalEventHandler& handler,
    const JonesUiModalBackground& background = JonesUiModalBackground{});

#endif // JONES3D_PLATFORM_BABYLON_JONESUIMODAL_H
