// Added: the Babylon column's Gameplay settings screen; see JonesUiGameplaySettings.h.
//
// Original behavior, from (git show 82b8f607^:Jones3D/Platform/Win32/jonesConfig.c):
// - jonesConfig_ShowGamePlayOptions (line 4320) returns 1 when the dialog's OK is pressed and 2 on
//   cancel (EndDialog with the command id).
// - Its init (line 4443) checked the Show Text, Show Hints, Rotate Map and Default To Run boxes,
//   the Aim Reticle and Blood Splatter boxes added in the port, and set the difficulty slider and
//   its text from the running game.
// - Its OK handler (line 4586) called sithVoice_ShowText and wrote gameplay.showText,
//   sithOverlayMap_EnableMapRotation and wrote gameplay.mapRotation,
//   sithOverlayMap_SetShowHints and wrote gameplay.showHints, sithSetGameDifficulty and wrote
//   gameplay.difficulty, stored gameplay.defaultRun and changed the control options run bit,
//   JonesReticle_Enable and sithWeapon_EnableBloodSplatter (each writes its own key on a change).
//
// The profile row shows the run's game.profile. The cvar is ReadOnly and fixed by the build's
// JONES3D_QOL_IMPROVEMENTS switch, and no game code reads it at startup (only the state trace
// reports it), so the row is disabled: a choice here cannot change the simulation.
//
// Added: the enhancements that are already Jones.cfg keys with a QOL default are toggles. Their
// modules read the keys at their own startup, so a change applies the next time that module opens
// (the next start for a normal session); Retail disables the rows and writes them off.
#include <Jones3D/Platform/Babylon/JonesUiGameplaySettings.h>

#include <Jones3D/Display/JonesReticle.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Play/jonesInventory.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithPuppet.h>
#include <sith/Gameplay/sithOverlayMap.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithMaterial.h>
#include <sith/World/sithVoice.h>
#include <sith/World/sithWeapon.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/std.h>
#include <std/General/stdConfig.h>

#include <babylon/ui/Screen.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr int kJonesDialogOk     = 1;
    constexpr int kJonesDialogCancel = 2;

    constexpr babylon::ui::ActionId kJonesGameplayActionOk   = 1;
    constexpr babylon::ui::ActionId kJonesGameplayActionBack = 2;

    // The run bit in sithControl_g_controlOptions that the original Default To Run option set.
    constexpr int kJonesGameplayRunOption = 0x02;

    // Added: the original dialog took its captions from the dialog resource, which holds no
    // JONES_STR key. The English text is used until one exists.
    constexpr const char* kJonesGameplayShowTextText       = "Show Text";            // TODO: [COMPAT]
    constexpr const char* kJonesGameplayShowHintsText      = "Show Hints";           // TODO: [COMPAT]
    constexpr const char* kJonesGameplayRotateMapText      = "Rotate Map";           // TODO: [COMPAT]
    constexpr const char* kJonesGameplayDifficultyText     = "Difficulty";           // TODO: [COMPAT]
    constexpr const char* kJonesGameplayDefaultRunText     = "Default to Run";       // TODO: [COMPAT]
    constexpr const char* kJonesGameplayReticleText        = "Aim Reticle";          // TODO: [COMPAT]
    constexpr const char* kJonesGameplayBloodText          = "Blood Splatter Effect"; // TODO: [COMPAT]
    constexpr const char* kJonesGameplayProfileText        = "Profile";              // TODO: [COMPAT]
    constexpr const char* kJonesGameplayEnhancedText       = "Enhanced";             // TODO: [COMPAT]
    constexpr const char* kJonesGameplayRetailText         = "Retail";               // TODO: [COMPAT]
    constexpr const char* kJonesGameplayProfileNoteText    =
        "Profile is fixed by this build."; // TODO: [COMPAT]
    constexpr const char* kJonesGameplayEnhancementsText   = "Enhancements";         // TODO: [COMPAT]
    constexpr const char* kJonesGameplayFireFlashText      =
        "Weapon fire flash"; // TODO: [COMPAT]
    constexpr const char* kJonesGameplayExternalMaterialsText =
        "External CND materials"; // TODO: [COMPAT]
    constexpr const char* kJonesGameplayExternalKeyframesText =
        "External CND keyframes"; // TODO: [COMPAT]

    struct JonesUiGameplayLayout
    {
        std::size_t showText = 0;
        std::size_t showHints = 0;
        std::size_t rotateMap = 0;
        std::size_t difficulty = 0;
        std::size_t defaultRun = 0;
        std::size_t reticle = 0;
        std::size_t blood = 0;
        std::size_t profile = 0;
        std::size_t fireFlash = 0;
        std::size_t externalMaterials = 0;
        std::size_t externalKeyframes = 0;
        std::size_t ok = 0;
        std::size_t back = 0;
    };

    struct JonesUiGameplayValues
    {
        bool bShowText = false;
        bool bShowHints = false;
        bool bRotateMap = false;
        std::size_t difficulty = 0;
        bool bDefaultRun = false;
        bool bReticle = false;
        bool bBlood = false;
        bool bFireFlash = false;
        bool bExternalMaterials = false;
        bool bExternalKeyframes = false;
    };

    bool JonesUiGameplay_ReadToggle(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
        std::size_t index, bool& value)
    {
        const std::optional<babylon::ui::WidgetValue> pValue = ui.widgetValue(screen, index);
        if ( !pValue || !std::holds_alternative<bool>(*pValue) )
        {
            return false;
        }

        value = std::get<bool>(*pValue);
        return true;
    }

    bool JonesUiGameplay_ReadChoice(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
        std::size_t index, std::size_t& value)
    {
        const std::optional<babylon::ui::WidgetValue> pValue = ui.widgetValue(screen, index);
        if ( !pValue || !std::holds_alternative<std::int64_t>(*pValue) )
        {
            return false;
        }

        value = (std::size_t)std::max<std::int64_t>(0, std::get<std::int64_t>(*pValue));
        return true;
    }

    bool JonesUiGameplay_ReadConfigBool(const char* pKey, bool defaultValue)
    {
        return stdConfig_HasStarted() ? stdConfig_GetBool(pKey, defaultValue) : defaultValue;
    }

    void JonesUiGameplay_SetConfigBool(const char* pKey, bool value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetBool(pKey, value));
        }
    }

    void JonesUiGameplay_SetConfigInt(const char* pKey, int value)
    {
        if ( stdConfig_HasStarted() )
        {
            static_cast<void>(stdConfig_SetInt(pKey, value));
        }
    }

    std::string JonesUiGameplay_DifficultyLabel(int difficulty)
    {
        char aKey[32] = { 0 };
        std::snprintf(aKey, sizeof(aKey), "JONES_STR_HARD%d", difficulty);

        const std::string text = JonesUiContext_ResolveString(aKey);
        // The string table returns the key itself when it has no entry, which is the case in a
        // unit test with no game assets.
        if ( text == aKey )
        {
            return std::to_string(difficulty);
        }

        return text;
    }

    babylon::ui::Screen JonesUiGameplay_BuildScreen(bool bEnhancedProfile,
        const JonesUiGameplayValues& values, JonesUiGameplayLayout& layout)
    {
        babylon::ui::Screen screen;
        screen.title = JonesUiContext_ResolveString("JONES_STR_GMPLY_OPTS");

        layout.showText = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiContext_ResolveString(kJonesGameplayShowTextText), std::string(), true,
            values.bShowText });

        layout.showHints = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiContext_ResolveString(kJonesGameplayShowHintsText), std::string(), true,
            values.bShowHints });

        layout.rotateMap = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiContext_ResolveString(kJonesGameplayRotateMapText), std::string(), true,
            values.bRotateMap });

        std::vector<std::string> difficultyOptions;
        const int maxDifficulty = (int)jonesInventory_GetMaxDifficultyLevel();
        difficultyOptions.reserve((std::size_t)maxDifficulty + 1);
        for ( int cur = 0; cur <= maxDifficulty; ++cur )
        {
            difficultyOptions.push_back(JonesUiGameplay_DifficultyLabel(cur));
        }

        layout.difficulty = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiContext_ResolveString(kJonesGameplayDifficultyText), std::string(),
            std::move(difficultyOptions), true,
            std::min(values.difficulty, (std::size_t)maxDifficulty) });

        layout.defaultRun = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiContext_ResolveString(kJonesGameplayDefaultRunText), std::string(), true,
            values.bDefaultRun });

        layout.reticle = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiContext_ResolveString(kJonesGameplayReticleText), std::string(), true,
            values.bReticle });

        layout.blood = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiContext_ResolveString(kJonesGameplayBloodText), std::string(), true,
            values.bBlood });

        // The profile is fixed by the build, so the row is shown but cannot be changed.
        layout.profile = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Choice{
            JonesUiContext_ResolveString(kJonesGameplayProfileText), std::string(),
            { JonesUiContext_ResolveString(kJonesGameplayEnhancedText),
                JonesUiContext_ResolveString(kJonesGameplayRetailText) },
            false, bEnhancedProfile ? 0u : 1u });

        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString(kJonesGameplayProfileNoteText), std::string() });

        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString(kJonesGameplayEnhancementsText), std::string() });

        layout.fireFlash = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiContext_ResolveString(kJonesGameplayFireFlashText), std::string(),
            bEnhancedProfile, values.bFireFlash });

        layout.externalMaterials = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiContext_ResolveString(kJonesGameplayExternalMaterialsText), std::string(),
            bEnhancedProfile, values.bExternalMaterials });

        layout.externalKeyframes = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Toggle{
            JonesUiContext_ResolveString(kJonesGameplayExternalKeyframesText), std::string(),
            bEnhancedProfile, values.bExternalKeyframes });

        layout.ok = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("OK"), kJonesGameplayActionOk, true });

        layout.back = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_CANCEL"), kJonesGameplayActionBack, true });

        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = 0;
        return screen;
    }
}

int JonesUiGameplay_RunGameplayScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    bool bEnhancedProfile)
{
    const int maxDifficulty = (int)jonesInventory_GetMaxDifficultyLevel();
    const int currentDifficulty = std::clamp(sithGetGameDifficulty(), 0, maxDifficulty);

    JonesUiGameplayValues values;
    values.bShowText          = sithVoice_GetShowText() != 0;
    values.bShowHints         = sithOverlayMap_GetShowHints() != 0;
    values.bRotateMap         = sithOverlayMap_GetMapRotation() != 0;
    values.difficulty         = (std::size_t)currentDifficulty;
    values.bDefaultRun        = JonesUiGameplay_ReadConfigBool(
        JONESCONFIG_CFG_GAMEPLAY_DEFAULTRUN, false);
    values.bReticle           = JonesReticle_IsEnabled();
    values.bBlood             = sithWeapon_IsBloodSplatterEnabled();
    values.bFireFlash         = bEnhancedProfile && JonesUiGameplay_ReadConfigBool(
        SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, true);
    values.bExternalMaterials = bEnhancedProfile && JonesUiGameplay_ReadConfigBool(
        SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, true);
    values.bExternalKeyframes = bEnhancedProfile && JonesUiGameplay_ReadConfigBool(
        SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, true);

    JonesUiGameplayLayout layout;
    const babylon::ui::Screen screen = JonesUiGameplay_BuildScreen(bEnhancedProfile, values,
        layout);

    bool bApply = false;
    const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
        babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
    {
        const babylon::ui::ActionPressed* pAction =
            std::get_if<babylon::ui::ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId || pAction->action != kJonesGameplayActionOk )
        {
            return false;
        }

        // Read the unbound widget values while the screen is still open; accepting the OK button
        // pops it right after this handler returns.
        static_cast<void>(JonesUiGameplay_ReadToggle(uiRef, screenId, layout.showText,
            values.bShowText));
        static_cast<void>(JonesUiGameplay_ReadToggle(uiRef, screenId, layout.showHints,
            values.bShowHints));
        static_cast<void>(JonesUiGameplay_ReadToggle(uiRef, screenId, layout.rotateMap,
            values.bRotateMap));
        static_cast<void>(JonesUiGameplay_ReadChoice(uiRef, screenId, layout.difficulty,
            values.difficulty));
        static_cast<void>(JonesUiGameplay_ReadToggle(uiRef, screenId, layout.defaultRun,
            values.bDefaultRun));
        static_cast<void>(JonesUiGameplay_ReadToggle(uiRef, screenId, layout.reticle,
            values.bReticle));
        static_cast<void>(JonesUiGameplay_ReadToggle(uiRef, screenId, layout.blood,
            values.bBlood));
        static_cast<void>(JonesUiGameplay_ReadToggle(uiRef, screenId, layout.fireFlash,
            values.bFireFlash));
        static_cast<void>(JonesUiGameplay_ReadToggle(uiRef, screenId, layout.externalMaterials,
            values.bExternalMaterials));
        static_cast<void>(JonesUiGameplay_ReadToggle(uiRef, screenId, layout.externalKeyframes,
            values.bExternalKeyframes));

        bApply = true;
        return false;
    };

    const int result = JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler);
    if ( result != kJonesDialogOk || !bApply )
    {
        return result;
    }

    // The original OK handler, in its order: show text, map rotation, hints, difficulty and the
    // Default To Run control option.
    sithVoice_ShowText(values.bShowText ? 1 : 0);
    JonesUiGameplay_SetConfigBool(SITHVOICE_CFG_GAMEPLAY_SHOWTEXT,
        sithVoice_GetShowText() != 0);

    sithOverlayMap_EnableMapRotation(values.bRotateMap ? 1 : 0);
    JonesUiGameplay_SetConfigBool(SITHOVERLAYMAP_CFG_GAMEPLAY_MAPROTATION,
        sithOverlayMap_GetMapRotation() != 0);

    sithOverlayMap_SetShowHints(values.bShowHints ? 1 : 0);
    JonesUiGameplay_SetConfigBool(SITHOVERLAYMAP_CFG_GAMEPLAY_SHOWHINTS,
        sithOverlayMap_GetShowHints() != 0);

    const int difficulty = std::clamp((int)std::min(values.difficulty,
        (std::size_t)maxDifficulty), 0, maxDifficulty);
    sithSetGameDifficulty(difficulty);
    JonesUiGameplay_SetConfigInt(SITH_CFG_GAMEPLAY_DIFFICULTY, sithGetGameDifficulty());

    JonesUiGameplay_SetConfigBool(JONESCONFIG_CFG_GAMEPLAY_DEFAULTRUN, values.bDefaultRun);
    if ( values.bDefaultRun )
    {
        sithControl_g_controlOptions |= kJonesGameplayRunOption;
    }
    else
    {
        sithControl_g_controlOptions &= ~kJonesGameplayRunOption;
    }

    // Each of these writes its own key when it changes, as the original calls did.
    JonesReticle_Enable(values.bReticle);
    sithWeapon_EnableBloodSplatter(values.bBlood);

    // Added: the enhancement keys. Their modules read them when they open, so a change applies
    // then. Retail wrote them off above, so this also turns them off in the file.
    JonesUiGameplay_SetConfigBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED,
        values.bFireFlash);
    JonesUiGameplay_SetConfigBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL,
        values.bExternalMaterials);
    JonesUiGameplay_SetConfigBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL,
        values.bExternalKeyframes);

    return kJonesDialogOk;
}

extern "C" int J3DAPI jonesConfig_ShowGamePlayOptions(tSysWindow hWnd)
{
    (void)hWnd;

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow )
    {
        // Headless: the Null stub answers cancel without blocking.
        return kJonesDialogCancel;
    }

    // Added: the profile the build fixed, as the cvar the state trace reports.
    std::string profile = J3D_QOL_VALUE("enhanced", "retail");
    if ( const std::optional<std::string> value = pContext->pCvars->getString("game.profile") )
    {
        profile = *value;
    }
    const bool bEnhancedProfile = profile != "retail";

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return kJonesDialogCancel;
    }

    const int result = JonesUiGameplay_RunGameplayScreen(*pUiContext->pUi,
        &pUiContext->painter, bEnhancedProfile);
    JonesUiContext_Release(pUiContext);
    return result;
}
