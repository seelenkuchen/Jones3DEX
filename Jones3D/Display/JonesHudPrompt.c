// Added: the interaction prompt state shared by the game HUD and the Babylon present overlay.
#include "JonesHudPrompt.h"
#include "JonesHud.h"

#include <std/General/stdConfig.h>

static JonesHudInteractionPrompt JonesHudPrompt_interaction = JONESHUD_INTERACTIONPROMPT_NONE;
static bool JonesHudPrompt_bEnabled = J3D_QOL_VALUE(true, false);

void J3DAPI JonesHudPrompt_SetInteraction(JonesHudInteractionPrompt prompt)
{
    if ( prompt < JONESHUD_INTERACTIONPROMPT_NONE || prompt > JONESHUD_INTERACTIONPROMPT_PICKUP )
    {
        prompt = JONESHUD_INTERACTIONPROMPT_NONE;
    }

    JonesHudPrompt_interaction = prompt;
}

JonesHudInteractionPrompt J3DAPI JonesHudPrompt_GetInteraction(void)
{
    return JonesHudPrompt_interaction;
}

bool JonesHud_LoadInteractionPromptsConfig(void)
{
    // Added: Enhanced turns the prompt on unless the config says otherwise; Retail keeps it off
    // whatever the file says, like the other Enhanced-only rows. A missing key keeps its default
    // and is not written: an existing Jones.cfg changes only when the player changes the option.
    JonesHudPrompt_bEnabled = J3D_QOL_VALUE(
        stdConfig_GetBool(JONESHUD_CFG_HUD_INTERACTIONPROMPTS, true), false);
    return JonesHudPrompt_bEnabled;
}

void JonesHud_EnableInteractionPrompts(int bEnable)
{
    const bool bNewValue = J3D_QOL_VALUE(bEnable != 0, 0);
    if ( bNewValue == JonesHudPrompt_bEnabled )
    {
        return;
    }

    JonesHudPrompt_bEnabled = bNewValue;

    // Added: the Gameplay screen writes the option only when the player changed it.
    if ( stdConfig_HasStarted() )
    {
        (void)stdConfig_SetBool(JONESHUD_CFG_HUD_INTERACTIONPROMPTS, JonesHudPrompt_bEnabled);
    }
}

bool JonesHud_IsInteractionPromptsEnabled(void)
{
    return JonesHudPrompt_bEnabled;
}
