#ifndef JONES3D_DISPLAY_JONESHUD_PROMPT_H
#define JONES3D_DISPLAY_JONESHUD_PROMPT_H
#include <j3dcore/j3d.h>
#include <Jones3D/types.h>

J3D_EXTERN_C_START

// Added: the interaction prompt's cached state. The game HUD sets it once per frame; the Babylon
// present overlay draws it at the window's native resolution. It is presentation only and never
// reaches the simulation.
typedef enum eJonesHudInteractionPrompt
{
    JONESHUD_INTERACTIONPROMPT_NONE = 0,
    JONESHUD_INTERACTIONPROMPT_USE,
    JONESHUD_INTERACTIONPROMPT_PICKUP,
    JONESHUD_INTERACTIONPROMPT_PUSH,
    JONESHUD_INTERACTIONPROMPT_PULL,
    JONESHUD_INTERACTIONPROMPT_PUSH_PULL,
    JONESHUD_INTERACTIONPROMPT_CLIMB_UP,
    JONESHUD_INTERACTIONPROMPT_CLIMB,
    JONESHUD_INTERACTIONPROMPT_WHIP_SWING,
    JONESHUD_INTERACTIONPROMPT_WHIP_CLIMB,
} JonesHudInteractionPrompt;

// control names the action the press for this prompt would read, so the overlay shows the binding
// the player would actually use (Activate, Jump or Forward). Quick whip's keyboard and pad presses
// read different rows, so SetInteractionPerDevice gives each device its own action; the plain
// setter uses control for both. JONESHUD_PROMPTCONTROL_NONE on a device draws no icon for it.
#define JONESHUD_PROMPTCONTROL_NONE JONESCONTROL_ACTION_NUMACTIONS
void J3DAPI JonesHudPrompt_SetInteraction(JonesHudInteractionPrompt prompt,
    JonesControlActions control);
void J3DAPI JonesHudPrompt_SetInteractionPerDevice(JonesHudInteractionPrompt prompt,
    JonesControlActions keyboardControl, JonesControlActions controllerControl);
JonesHudInteractionPrompt J3DAPI JonesHudPrompt_GetInteraction(void);
JonesControlActions J3DAPI JonesHudPrompt_GetControl(void);
JonesControlActions J3DAPI JonesHudPrompt_GetControllerControl(void);

J3D_EXTERN_C_END
#endif // JONES3D_DISPLAY_JONESHUD_PROMPT_H
