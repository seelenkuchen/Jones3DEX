# ADR-0036: Button Layout binding screens

- **Status:** Accepted
- **Date:** 2026-11-15

## Context

ADR-0026 put every key and gamepad binding in one rebind list on the Controls tab. After using the
windowed build the owner found that list hard to read: it is one large table, its rows are not
selectable, and it does not show which physical button a pad binding uses. The reference is the
"Controller > Button Layout" screen of *Indiana Jones and the Great Circle*: a scrollable list of
actions, each with the bound input's icon, and a pad diagram with callouts that group the actions
using the same button.

The Babylon `ui` module can draw buttons, lists and value rows, but not framed icon rows, a pad
diagram or callout lines and labels.

## Decision

1. Options > Controls gets two rows, **Controller** and **Keyboard & Mouse**, which open one shared
   `JonesUiButtonLayout` screen with a device switch. Scheme choice, New, Delete, Mouse camera and
   Rumble stay on the Controls tab. The old one-list editor is no longer opened from the tab.
2. The screen's left column is a scrollable list with one framed row per action, using the existing
   action display names. The first binding of the screen's device is drawn as its Xelu prompt icon,
   right-aligned in the row; a key without an icon uses a small key-shaped frame, and an unbound
   action draws "Not bound". Focus, scrolling, mouse hover and clicks are model state, so they are
   testable without a GPU.
3. The right side is the Xbox `XboxSeriesX_Diagram_Simple.png` with one callout per bound pad
   input. Directional inputs are grouped: the left stick's walk and turn actions become one "Move"
   callout, and the right stick and D-pad each get one. A callout joins the remaining action names
   with `" | "`, sits in a left or right column outside the diagram, and is sorted by the anchor's
   y and spread over the area. Labels wrap to two lines and are ellipsized if still too wide, so
   they never overlap or run into the image or off screen. Axis-aligned lines drawn with `fillRect`
   connect each label to its anchor. A small table of normalised anchor points per input lives
   beside the drawing code. The focused row's binding is highlighted. The Keyboard & Mouse screen
   swaps the pad for the mouse outline with callouts for the mouse buttons and shows the focused
   key's large icon and action name below it.
4. **Bind** captures the next input of that screen's device and replaces that action's first binding
   of the device, leaving the other device's bindings untouched. A control already bound to another
   action uses the existing reassign question. Escape or pad East/Back cancels. **Reset Binds**
   restores only that device's bindings from the default scheme after a Yes/No question. **Back**
   writes nothing and runs no required-binding check when nothing changed. After a change it
   validates the required-action check, turns a built-in default into a custom copy, writes the
   scheme and rebinds; the other device's bindings are kept. The check counts any device binding,
   exempts the original weapon keys and the optional artifact hot keys, and exempts actions the
   Modern style drives from a stick.
5. In the Modern control style the left stick owns movement, turn, sidestep and run, and the right
   stick owns the camera. Those rows show the stick icon with a lock, Bind explains that the stick
   owns them, and the pad callouts read "Move" and "Camera". Classic treats them as normal rows.
   The ownership check is one helper in `JonesUiControls.cpp`.
6. To draw and handle the screen without teaching `ui` about game actions, `Screen` gains optional
   draw and raw-event callbacks. The draw callback replaces the widget drawing for that screen and
   the event callback runs before navigation. They are general, game-agnostic hooks with a
   `NullPainter`/scripted-event test path.
7. No assets come from *The Great Circle*. The diagram and prompt icons are the Xelu prompt set
   already under `Data/ui/prompts/xbox/`, licensed CC0.

This supersedes ADR-0026's "one rebind list for keys and pad" and the part of ADR-0028 that described
the Controls tab's bind list.

## Consequences

- The Controls tab is shorter; device-specific binding work happens on its own screen and the
  diagram explains pad bindings at a glance.
- `babylon::ui::Screen` has two optional callbacks. A screen that sets `draw` must draw its own
  content; the module still draws the prompt bar and reports back results.
- The Keyboard & Mouse screen shares the model and layout; only its icon mapping and capture rules
  differ.
- Scheme files, defaults, reassign and required-binding rules are unchanged, so existing schemes and
  the Retail trace keep working.
