// Added: the Babylon column's one event pump. The composition root owns the platform whose queue
// this drains; the stdControl device read and the wkernel window adapter both call stdBabylonEvents_Pump
// instead of owning a pump of their own, so one event is taken exactly once and every consumer sees
// the same records. Input events go into the shared babylon::input::InputState; a QuitRequested, and
// a WindowCloseRequested or WindowFocusChanged for the context's game window (StdBabylonContext::pWindow),
// are recorded here for the window adapter to take.
// Adapter code: no Fixed/Added markers inside.
#ifndef STD_STDBABYLONEVENTS_H
#define STD_STDBABYLONEVENTS_H
#ifndef __cplusplus
#error "stdBabylonEvents.h is C++ only: it names babylon::input::InputState and std::optional."
#endif

#include <babylon/input/InputState.h>
#include <babylon/platform/Event.h>

#include <cstddef>
#include <optional>
#include <vector>

// Drains the context platform's event queue once: input events go into the shared InputState; a
// QuitRequested, and a WindowCloseRequested or WindowFocusChanged for the context's game window
// (pWindow; ignored when there is none), are recorded for the window adapter. While the scripted
// tour's input script is active, its events replace the platform's key, mouse, gamepad and text
// events (see stdBabylonTour.h); window, focus, resize and quit events still come from the
// platform. Returns the number of events taken from the queue plus the script events injected.
// Safe to call any number of times per frame.
std::size_t stdBabylonEvents_Pump();

// The input state every pumped event went into (owned here, file-static).
babylon::input::InputState& stdBabylonEvents_InputState();

// The events the last stdBabylonEvents_Pump() took from the queue, in queue order. The vector is
// replaced on every pump, so it stays bounded by one frame's events; the reference is valid until
// the next pump. A modal screen loop reads the raw events from here to feed babylon::ui.
const std::vector<babylon::platform::Event>& stdBabylonEvents_LastEvents();

// True while a quit or game-window close request is recorded and has not been taken. A modal
// screen loop uses it to close its screen without consuming the request, so the game still sees
// the WM_QUIT after the screen returns.
bool stdBabylonEvents_PeekQuitRequest();

// True once for each recorded quit or close request (the WM_QUIT the window adapter reports).
bool stdBabylonEvents_TakeQuitRequest();

// Records a quit request exactly as a platform QuitRequested does, so the next
// wkernel_PeekProcessEvents or wkernel_ProcessEvents answers 1 (the WM_QUIT). The game's own
// close path uses this to end the loop when it asks the window to close.
void stdBabylonEvents_RequestQuit();

// The oldest recorded focus change of the game window (true = gained), or nullopt.
std::optional<bool> stdBabylonEvents_TakeFocusChange();

#ifdef J3D_TEST
void stdBabylonEvents_ResetForTest(); // clears the state, the quit flag and the focus queue
#endif

#endif // STD_STDBABYLONEVENTS_H
