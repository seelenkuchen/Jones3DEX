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
} JonesHudInteractionPrompt;

// control names the action the press for this prompt would read, so the overlay shows the binding
// the player would actually use (Activate, Jump or Forward).
void J3DAPI JonesHudPrompt_SetInteraction(JonesHudInteractionPrompt prompt,
    JonesControlActions control);
JonesHudInteractionPrompt J3DAPI JonesHudPrompt_GetInteraction(void);
JonesControlActions J3DAPI JonesHudPrompt_GetControl(void);

J3D_EXTERN_C_END
#endif // JONES3D_DISPLAY_JONESHUD_PROMPT_H
