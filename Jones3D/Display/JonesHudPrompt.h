#ifndef JONES3D_DISPLAY_JONESHUD_PROMPT_H
#define JONES3D_DISPLAY_JONESHUD_PROMPT_H
#include <j3dcore/j3d.h>

J3D_EXTERN_C_START

// Added: the interaction prompt's cached state. The game HUD sets it once per frame; the Babylon
// present overlay draws it at the window's native resolution. It is presentation only and never
// reaches the simulation.
typedef enum eJonesHudInteractionPrompt
{
    JONESHUD_INTERACTIONPROMPT_NONE = 0,
    JONESHUD_INTERACTIONPROMPT_USE,
    JONESHUD_INTERACTIONPROMPT_PICKUP,
} JonesHudInteractionPrompt;

void J3DAPI JonesHudPrompt_SetInteraction(JonesHudInteractionPrompt prompt);
JonesHudInteractionPrompt J3DAPI JonesHudPrompt_GetInteraction(void);

J3D_EXTERN_C_END
#endif // JONES3D_DISPLAY_JONESHUD_PROMPT_H
