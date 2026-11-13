// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/cvars/Registry.h>
#include <babylon/platform/Event.h>
#include <babylon/ui/Navigation.h>
#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/UiEvent.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace babylon::ui
{

    // The screen stack and everything that runs it. A game declares each
    // screen as a Screen value, pushes it, and then once per frame feeds
    // platform events through handleEvent(), calls draw(), and drains
    // pollEvent() for actions and close results. The blocking wrapper for
    // an original dialog is therefore a loop around one pushed screen.
    //
    // Declaring a settings screen, the test of whether the declarative
    // widgets are small enough:
    //
    //     enum : babylon::ui::ActionId { kActionBack = 1 };
    //     babylon::ui::ScreenId id = ui.push({
    //         .title = "Settings",
    //         .widgets = {
    //             babylon::ui::Toggle{ .text = "Fullbright",
    //                                  .cvar = "r_fullbright" },
    //             babylon::ui::Slider{ .text = "Gamma", .cvar = "r_gamma",
    //                                  .min = 0.5, .max = 2.0, .step = 0.05 },
    //             babylon::ui::Button{ .text = "Back", .action = kActionBack },
    //         },
    //     });
    //
    // The stack: push puts a screen on top, pop removes the top one with a
    // result, and replace swaps the top one. Only the top screen takes
    // input and is drawn. Closing the top screen lets the one below it
    // resume. A screen's result is reported once, in a ScreenClosed event,
    // and is not stored after the event is polled.
    //
    // Layout is automatic on a virtual canvas 480 units high and
    // 480 * aspect units wide. Lengths the game passes are canvas units;
    // the module only ever learns the viewport in pixels to map the pointer.
    //
    // Bound widgets read their value with cvars::Registry::getBool,
    // getInt, getFloat or getString, take type and range from
    // cvars::Registry::desc and read-only status from
    // cvars::Registry::flags, and write with cvars::Registry::set(name,
    // value, cvars::Source::User). A widget bound to a ReadOnly cvar is
    // drawn disabled and never writes. A StartupOnly write after
    // finishStartup() is refused by the registry, which logs it, and the
    // widget shows the stored value again on the next draw. The module
    // additionally clamps sliders to the intersection of their own range
    // and the cvar's range and refuses a Choice whose int cvar range
    // excludes an option index.
    //
    // An open screen can change between frames with setWidget(), which is
    // how a store updates its balance or a Load screen swaps its preview.
    // User changes of value-carrying widgets are reported as ValueChanged
    // and list selection changes as SelectionChanged, bound or not.
    //
    // Threading: every function must be called from the same thread, which
    // is also the thread that calls Painter. Not thread-safe.
    class Ui
    {
    public:
        // painter draws every screen and is used for text measurement; cvars
        // is read and written by bound widgets. Both are borrowed and must
        // outlive this Ui. The initial skin is default-constructed and the
        // initial navigation map is NavigationMap{}.
        Ui(Painter& painter, cvars::Registry& cvars);

        // Does not draw or report anything.
        ~Ui();

        Ui(const Ui&) = delete;
        Ui& operator=(const Ui&) = delete;
        Ui(Ui&&) = delete;
        Ui& operator=(Ui&&) = delete;

        // Replaces the copied look and sound. Returns false and changes
        // nothing, with one log warning, for a skin whose rowHeight is not
        // positive, titleHeight is negative, rowSpacing, panelPadding,
        // minPanelWidth, canvasMargin, borderWidth or markerSpacing is
        // negative. May be called at any time; a pushed screen keeps its
        // state and is laid out with the new metrics on the next draw.
        [[nodiscard]] bool setSkin(const Skin& skin);

        // The current skin. The reference is valid until the next setSkin()
        // or until this Ui is destroyed.
        const Skin& skin() const;

        // Replaces the copied navigation map. Returns false and changes
        // nothing, with one log warning, when axisThreshold is outside 0..1.
        // May be called at any time.
        [[nodiscard]] bool setNavigation(const NavigationMap& map);

        // The current map. The reference is valid until the next
        // setNavigation() or until this Ui is destroyed.
        const NavigationMap& navigationMap() const;

        // Sets the window's client size in pixels, used to map pointer
        // positions onto the canvas. Returns false, with one log warning,
        // for a non-positive width or height, leaving the old size. The
        // first call is required before pointer or mouse-wheel events mean
        // anything; until it, the pointer is ignored. Keyboard and gamepad
        // navigation work without it. May be called whenever the window
        // resizes.
        [[nodiscard]] bool setViewport(int pixelWidth, int pixelHeight);

        // The current virtual canvas size in canvas units: height is always
        // 480, width is 480 * aspect. Before setViewport() it is 0 x 480.
        Size canvasSize() const;

        // Advances the UI's own clock by one frame's elapsed seconds, used
        // only for gamepad stick repeat because platform events carry no
        // timestamps. Call once per frame before handleEvent() or after it;
        // a non-positive or non-finite value changes nothing. Holding a
        // stick beyond axisThreshold fires once immediately, then again
        // after a fixed initial delay and at a fixed rate until it returns
        // below the threshold. Keyboard repeats come from the operating
        // system's repeat flag instead.
        void update(double seconds);

        // Puts a copy of screen on top of the stack and returns its id. The
        // copy owns the widgets, so the argument may be destroyed at once.
        // Every bound widget is validated: a non-empty cvar name must name a
        // cvar of the widget's type (bool for Toggle, int or float for
        // Slider, int or string for Choice, string for TextEntry), a Choice
        // must have at least one option, an int Choice cvar with a range
        // must admit every option index, and a Slider must have min <= max
        // and step >= 0. Returns kInvalidScreen and changes nothing, with
        // one log warning, when validation fails. The new screen becomes the
        // focused top screen with Screen::initialFocus honored; the previous
        // top screen keeps its state.
        [[nodiscard]] ScreenId push(Screen screen);

        // Replaces the widget at index of an open screen with a copy of
        // widget, for screens that change while they are open. Any kind may
        // replace any kind. The new widget is validated exactly as push()
        // validates, including its cvar binding. Returns false, changes
        // nothing and logs one warning when screen is not open, index is out
        // of range, or the widget is invalid. Focus stays on the same index
        // when the new widget is focusable and enabled; otherwise it moves
        // to the nearest focusable widget after that index, wrapping. The
        // scroll offset is kept and clamped to the new layout. An unbound
        // widget keeps its local value when the old and new kinds are the
        // same and starts from the new widget's defaults otherwise; a bound
        // widget shows its cvar.
        [[nodiscard]] bool setWidget(ScreenId screen, std::size_t index, Widget widget);

        // Replaces the top screen with a copy of screen and reports the old
        // top screen's close with result, exactly as pop(result) followed by
        // push(screen) would, but without the old screen being drawn in
        // between. Validation and the return value are push()'s. When the
        // stack is empty this only pushes and returns the new id; result is
        // then unused.
        [[nodiscard]] ScreenId replace(Screen screen, ResultCode result);

        // Removes the top screen and queues its ScreenClosed event with
        // result. Returns false, changes nothing and logs nothing when the
        // stack is empty. result defaults to 0. Closing a screen discards
        // its widget state; it does not leak into a later push of the same
        // declaration. If a binding capture or text edit is active it is
        // cancelled first.
        bool pop(ResultCode result = 0);

        // True when id names a screen currently on the stack.
        bool isOpen(ScreenId screen) const;

        // The top screen's id, or kInvalidScreen when the stack is empty.
        ScreenId topScreen() const;

        // Number of screens on the stack.
        std::size_t screenCount() const;

        // Feeds one platform event to the top screen. TextInput inserts
        // committed text into a focused TextEntry. Events of other kinds
        // (quit, window resize, gamepad connect) are ignored; call
        // setViewport() for WindowResized. Never fails. Call once for every
        // event the platform queue returned, in queue order, before draw().
        // An invalid pointer position from before the first setViewport() is
        // ignored.
        void handleEvent(const platform::Event& event);

        // Draws the top screen through the painter, or draws nothing when
        // the stack is empty. The game draws whatever belongs behind the
        // screen (game frame or menu background) first, then calls this.
        // The painter's clip stack is balanced when this returns. Calling it
        // more than once per frame draws again; it has no side effects on
        // UI state.
        void draw();

        // The next event since the previous call, in occurrence order, or
        // nullopt when none is queued. Events are produced by handleEvent()
        // and by push(), replace(), pop() and close-on-back. The game should
        // drain this once per frame; the queue is unbounded, so a game that
        // never polls keeps growing it.
        std::optional<UiEvent> pollEvent();

        // The index in the top screen's Screen::widgets of the focused
        // widget, or kNoWidget when the stack is empty or no widget can
        // take focus. A focused List uses this index even while its own
        // selection moves.
        std::size_t focusedWidget() const;

        // The current value of the Toggle, Slider, Choice or TextEntry at
        // index of an open screen, bound or unbound, as described at
        // WidgetValue. A TextEntry returns its current edit buffer, which
        // may differ from the cvar until it is accepted. Returns nullopt for
        // a screen that is not open, an index out of range, or a widget of
        // another kind. Reading a closed screen's value is not possible;
        // read it before the screen closes, or take it from TextSubmitted.
        std::optional<WidgetValue> widgetValue(ScreenId screen, std::size_t index) const;

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };

}
