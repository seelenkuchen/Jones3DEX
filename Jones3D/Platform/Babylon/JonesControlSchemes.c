// Added: the Babylon column's control scheme files, ported from the Win32 jonesConfig.c without
// any dialog code (M7.17a). The same .kfg format, default schemes, key-sets directory and binding
// calls; the controls screen (M7.17b) uses the accessors at the end. See JonesControlSchemes.h.
//
// Extra bindings the original format cannot express (mouse buttons and the right-stick and left
// trigger axes) go in a trailing EXTRAS section. The original reader stops after its 37 action
// lines, so it skips the section; see LoadControlScheme below.
#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>

#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Play/JonesControl.h>
#include <Jones3D/Platform/Babylon/JonesBabylonWindow.h>

#include <sith/Devices/sithControl.h>
#include <sith/Main/sithMain.h>
#include <sith/Main/sithString.h>

#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdConffile.h>
#include <std/General/stdControl.h>
#include <std/General/stdFileUtil.h>
#include <std/General/stdFnames.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUserDir.h>
#include <std/General/stdUtil.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JONESCONFIG_NUMDEFAULTCONTROLSCHEMES 5u
#define JONESCONFIG_KEYSDIRPATH_MAX 512u
#define JONESCONFIG_KFG_EXT ".kfg"

static int jonesConfig_bStartup = 0;
static int jonesConfig_bPendingDefaultScheme = 0;
static char jonesConfig_aKeySetsDirPath[JONESCONFIG_KEYSDIRPATH_MAX] = { 0 };
// Added: where the shipped key sets stay readable when the user directory takes the writes.
static char jonesConfig_aShippedKeySetsDirPath[JONESCONFIG_KEYSDIRPATH_MAX] = { 0 };

static size_t jonesConfig_maxControlStrLen       = 0u;
static size_t jonesConfig_maxControlWords        = 0u;
static size_t jonesConfig_maxControlPaddedStrLen = 0u;
static size_t jonesConfig_maxActionStrLen        = 0u;
static size_t jonesConfig_maxActionWords         = 0u;
static size_t jonesConfig_maxActionPaddedStrLen  = 0u;
static int jonesConfig_bKeyActionsInited = 0;

static JonesControlsScheme* jonesConfig_pActiveScheme = NULL;

// Added: the exact strings the original writer and parser used for joystick controls. When the
// game string table has the key, the localized string wins and the files match retail; without
// game data the canonical English form keeps the files readable and testable.
static const char* JonesControlSchemes_ResolveOr(const char* pKey, const char* pCanonical)
{
    if ( !pKey )
    {
        return pCanonical;
    }

    const char* pValue = jonesString_GetString(pKey);
    if ( pValue && strcmp(pValue, pKey) != 0 )
    {
        return pValue;
    }

    return pCanonical;
}

static const char* JonesControlSchemes_ResolveKey(const char* pKey)
{
    return JonesControlSchemes_ResolveOr(pKey, pKey);
}

// The original jonesConfig_aJonesControlActionNames, used to write and parse the action lines.
static const char* jonesConfig_aJonesControlActionNames[JONESCONTROL_ACTION_NUMACTIONS] =
{
    "JONES_STR_WLKFWD",
    "JONES_STR_WLKBK",
    "JONES_STR_TRNLFT",
    "JONES_STR_TRNRGHT",
    "JONES_STR_STPLFT",
    "JONES_STR_STPRGHT",
    "JONES_STR_CRAWL",
    "JONES_STR_RUN",
    "JONES_STR_ROLL",
    "JONES_STR_JUMP",
    "JONES_STR_LOOK",
    "JONES_STR_ACT",
    "JONES_STR_TGLWEAP",
    "JONES_STR_PREVWEAP",
    "JONES_STR_NEXTWEAP",
    "JONES_STR_TGLLGHT",
    "JONES_STR_FISTS",
    "JONES_STR_WHIP",
    "JONES_STR_MAUSER",
    "JONES_STR_PPSH41",
    "JONES_STR_PISTOL",
    "JONES_STR_SIMONOV",
    "JONES_STR_TOKEREV",
    "JONES_STR_TOZ34",
    "JONES_STR_BAZOOKA",
    "JONES_STR_MACHETE",
    "JONES_STR_SATCHEL",
    "JONES_STR_GRENADE",
    "JONES_STR_MAP",
    "JONES_STR_INTERFACE",
    "JONES_STR_HEALTH",
    "JONES_STR_IMP1",
    "JONES_STR_IMP2",
    "JONES_STR_IMP3",
    "JONES_STR_IMP4",
    "JONES_STR_IMP5",
    "JONES_STR_CHALK"
};

// The original jonesConfig_aJonesCapControlActionNames. Only the serializer padding uses it.
static const char* jonesConfig_aJonesCapControlActionNames[JONESCONTROL_ACTION_NUMACTIONS] =
{
    "JONES_STR_CAPS_WLKFWD",
    "JONES_STR_CAPS_WLKBK",
    "JONES_STR_CAPS_TRNLFT",
    "JONES_STR_CAPS_TRNRGHT",
    "JONES_STR_CAPS_STPLFT",
    "JONES_STR_CAPS_STPRGHT",
    "JONES_STR_CAPS_CRAWL",
    "JONES_STR_CAPS_RUN",
    "JONES_STR_CAPS_ROLL",
    "JONES_STR_CAPS_JUMP",
    "JONES_STR_CAPS_LOOK",
    "JONES_STR_CAPS_ACT",
    "JONES_STR_CAPS_TGLWEAP",
    "JONES_STR_CAPS_PREVWEAP",
    "JONES_STR_CAPS_NEXTWEAP",
    "JONES_STR_CAPS_TGLLGHT",
    "JONES_STR_CAPS_FISTS",
    "JONES_STR_CAPS_WHIP",
    "JONES_STR_CAPS_MAUSER",
    "JONES_STR_CAPS_PPSH41",
    "JONES_STR_CAPS_PISTOL",
    "JONES_STR_CAPS_SIMONOV",
    "JONES_STR_CAPS_TOKEREV",
    "JONES_STR_CAPS_TOZ34",
    "JONES_STR_CAPS_BAZOOKA",
    "JONES_STR_CAPS_MACHETE",
    "JONES_STR_CAPS_SATCHEL",
    "JONES_STR_CAPS_GRENADE",
    "JONES_STR_CAPS_MAP",
    "JONES_STR_CAPS_INTERFACE",
    "JONES_STR_CAPS_HEALTH",
    "JONES_STR_CAPS_IMP1",
    "JONES_STR_CAPS_IMP2",
    "JONES_STR_CAPS_IMP3",
    "JONES_STR_CAPS_IMP4",
    "JONES_STR_CAPS_IMP5",
    "JONES_STR_CAPS_CHALK"
};

// The original jonesConfig_aControlKeyStrings table, indexed by engine key id. Entries are either
// a control name or a JONES_STR_ key.
static const char* jonesConfig_aControlKeyStrings[223] =
{
    NULL,
    "JONES_STR_ESCAPE",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "0",
    "JONES_STR_MINUS",
    "JONES_STR_EQUAL",
    "JONES_STR_BACK",
    "JONES_STR_TAB",
    "Q",
    "W",
    "E",
    "R",
    "T",
    "Y",
    "U",
    "I",
    "O",
    "P",
    "JONES_STR_LEFT_BRCKT",
    "JONES_STR_RIGHT_BRCKT",
    "JONES_STR_ENTER",
    "JONES_STR_LEFT_CTRL",
    "A",
    "S",
    "D",
    "F",
    "G",
    "H",
    "J",
    "K",
    "L",
    ";",
    "'",
    "`",
    "JONES_STR_LEFT_SHIFT",
    "\\",
    "Z",
    "X",
    "C",
    "V",
    "B",
    "N",
    "M",
    "JONES_STR_COMMA",
    ".",
    "/",
    "JONES_STR_RIGHT_SHIFT",
    "JONES_STR_NUMPAD_MULT",
    "JONES_STR_LEFT_ALT",
    "JONES_STR_SPACE",
    "JONES_STR_CAPS",
    "JONES_STR_F1",
    "JONES_STR_F2",
    "JONES_STR_F3",
    "JONES_STR_F4",
    "JONES_STR_F5",
    "JONES_STR_F6",
    "JONES_STR_F7",
    "JONES_STR_F8",
    "JONES_STR_F9",
    "JONES_STR_F10",
    "JONES_STR_NUMLOCK",
    "JONES_STR_SCROLL",
    "JONES_STR_NUMPAD_7",
    "JONES_STR_NUMPAD_8",
    "JONES_STR_NUMPAD_9",
    "JONES_STR_NUMPAD_MINUS",
    "JONES_STR_NUMPAD_4",
    "JONES_STR_NUMPAD_5",
    "JONES_STR_NUMPAD_6",
    "JONES_STR_NUMPAD_PLUS",
    "JONES_STR_NUMPAD_1",
    "JONES_STR_NUMPAD_2",
    "JONES_STR_NUMPAD_3",
    "JONES_STR_NUMPAD_0",
    "JONES_STR_NUMPAD_DOT",
    NULL,
    NULL,
    NULL,
    "JONES_STR_F11",
    "JONES_STR_F12",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "JONES_STR_F13",
    "JONES_STR_F14",
    "JONES_STR_F15",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "JONES_STR_NUMPAD_EQL",
    NULL,
    NULL,
    NULL,
    "@",
    ":",
    "__",
    NULL,
    "STOP",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "JONES_STR_NUMPAD_ENTER",
    "JONES_STR_RIGHT_CTRL",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "JONES_STR_NUMPAD_COMMA",
    NULL,
    "JONES_STR_NUMPAD_DIV",
    NULL,
    "JONES_STR_SYSRQ",
    "JONES_STR_RIGHT_ALT",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "JONES_STR_HOME",
    "JONES_STR_UPARRW",
    "JONES_STR_PAGEUP",
    NULL,
    "JONES_STR_LEFTARRW",
    NULL,
    "JONES_STR_RGHTARRW",
    NULL,
    "JONES_STR_END",
    "JONES_STR_DNARRW",
    "JONES_STR_PGDN",
    "JONES_STR_INSERT",
    "JONES_STR_DELETE",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    "JONES_STR_LEFT_WND",
    "JONES_STR_RIGHT_WND",
    "JONES_STR_APPS",
    NULL
};

typedef struct sJonesConfigControlAction
{
    const size_t numBinds;
    const size_t* aBindings;
} JonesConfigControlAction;

static const size_t aActionWalkFwdKeys[2]     = { STDKEY_UP   , JONESCONFIG_JOYAXISKID_UP };
static const size_t aActionWalkBackKeys[2]    = { STDKEY_DOWN , JONESCONFIG_JOYAXISKID_DOWN };
static const size_t aActionTurnLeftKeys[2]    = { STDKEY_LEFT , JONESCONFIG_JOYAXISKID_LEFT };
static const size_t aActionTurnRightKeys[2]   = { STDKEY_RIGHT, JONESCONFIG_JOYAXISKID_RIGHT };
static const size_t aActionStepLeftKeys[1]    = { STDKEY_A };
static const size_t aActionStepRightKeys[1]   = { STDKEY_S };
static const size_t aActionCrawlKeys[1]       = { STDKEY_C };
static const size_t aActionRunKeys[2]         = { STDKEY_LSHIFT, STDKEY_RSHIFT };
static const size_t aActionRollKeys[1]        = { STDKEY_Z };
static const size_t aActionJumpKeys[4]        = { STDKEY_X, STDKEY_LMENU, STDKEY_RMENU, STDCONTROL_JOYSTICK_GETBUTTON(0, 1) };
static const size_t aActionLookKeys[2]        = { STDKEY_INSERT, STDKEY_NUMPAD0 };
static const size_t aActionActivateKeys[3]    = { STDKEY_LCONTROL, STDKEY_RCONTROL, STDCONTROL_JOYSTICK_GETBUTTON(0, 0) };
static const size_t aActionToggleMapKeys[1]   = { STDKEY_SPACE };
static const size_t aActionPrevWeaponKeys[1]  = { STDKEY_COMMA };
static const size_t aActionNextWeaponKeys[1]  = { STDKEY_PERIOD };
static const size_t aActionToggleLightKeys[1] = { STDKEY_L };
static const size_t aActionFistsKeys[1]       = { STDKEY_0 };
static const size_t aActionWhipKeys[1]        = { STDKEY_1 };
static const size_t aActionMauserKeys[1]      = { STDKEY_4 };
static const size_t aActionPPSH41Keys[1]      = { STDKEY_7 };
static const size_t aActionPistolKeys[1]      = { STDKEY_2 };
static const size_t aActionSimonovKeys[1]     = { STDKEY_5 };
static const size_t aActionTokarevKeys[1]     = { STDKEY_3 };
static const size_t aActionToz34Keys[1]       = { STDKEY_8 };
static const size_t aActionBazookaKeys[1]     = { STDKEY_9 };
static const size_t aActionMacheteKeys[1]     = { STDKEY_6 };
static const size_t aActionSatchelKeys[1]     = { STDKEY_P };
static const size_t aActionGrenadeKeys[1]     = { STDKEY_G };
static const size_t aActionMapKeys[1]         = { STDKEY_M };
static const size_t aActionInterfaceKeys[1]   = { STDKEY_I };
static const size_t aActionHealthKeys[1]      = { STDKEY_H };
static const size_t aActionIMP1Keys[1]        = { STDKEY_Q };
static const size_t aActionIMP2Keys[1]        = { STDKEY_W };
static const size_t aActionIMP3Keys[1]        = { STDKEY_E };
static const size_t aActionIMP4Keys[1]        = { STDKEY_R };
static const size_t aActionIMP5Keys[1]        = { STDKEY_T };
static const size_t aActionChalk[1]           = { STDKEY_K };

static const JonesConfigControlAction jonesConfig_aDefaultControlSet[JONESCONTROL_ACTION_NUMACTIONS] =
{
    { STD_ARRAYLEN(aActionWalkFwdKeys)    , aActionWalkFwdKeys     },
    { STD_ARRAYLEN(aActionWalkBackKeys)   , aActionWalkBackKeys    },
    { STD_ARRAYLEN(aActionTurnLeftKeys)   , aActionTurnLeftKeys    },
    { STD_ARRAYLEN(aActionTurnRightKeys)  , aActionTurnRightKeys   },
    { STD_ARRAYLEN(aActionStepLeftKeys)   , aActionStepLeftKeys    },
    { STD_ARRAYLEN(aActionStepRightKeys)  , aActionStepRightKeys   },
    { STD_ARRAYLEN(aActionCrawlKeys)      , aActionCrawlKeys       },
    { STD_ARRAYLEN(aActionRunKeys)        , aActionRunKeys         },
    { STD_ARRAYLEN(aActionRollKeys)       , aActionRollKeys        },
    { STD_ARRAYLEN(aActionJumpKeys)       , aActionJumpKeys        },
    { STD_ARRAYLEN(aActionLookKeys)       , aActionLookKeys        },
    { STD_ARRAYLEN(aActionActivateKeys)   , aActionActivateKeys    },
    { STD_ARRAYLEN(aActionToggleMapKeys)  , aActionToggleMapKeys   },
    { STD_ARRAYLEN(aActionPrevWeaponKeys) , aActionPrevWeaponKeys  },
    { STD_ARRAYLEN(aActionNextWeaponKeys) , aActionNextWeaponKeys  },
    { STD_ARRAYLEN(aActionToggleLightKeys), aActionToggleLightKeys },
    { STD_ARRAYLEN(aActionFistsKeys)      , aActionFistsKeys       },
    { STD_ARRAYLEN(aActionWhipKeys)       , aActionWhipKeys        },
    { STD_ARRAYLEN(aActionMauserKeys)     , aActionMauserKeys      },
    { STD_ARRAYLEN(aActionPPSH41Keys)     , aActionPPSH41Keys      },
    { STD_ARRAYLEN(aActionPistolKeys)     , aActionPistolKeys      },
    { STD_ARRAYLEN(aActionSimonovKeys)    , aActionSimonovKeys     },
    { STD_ARRAYLEN(aActionTokarevKeys)    , aActionTokarevKeys     },
    { STD_ARRAYLEN(aActionToz34Keys)      , aActionToz34Keys       },
    { STD_ARRAYLEN(aActionBazookaKeys)    , aActionBazookaKeys     },
    { STD_ARRAYLEN(aActionMacheteKeys)    , aActionMacheteKeys     },
    { STD_ARRAYLEN(aActionSatchelKeys)    , aActionSatchelKeys     },
    { STD_ARRAYLEN(aActionGrenadeKeys)    , aActionGrenadeKeys     },
    { STD_ARRAYLEN(aActionMapKeys)        , aActionMapKeys         },
    { STD_ARRAYLEN(aActionInterfaceKeys)  , aActionInterfaceKeys   },
    { STD_ARRAYLEN(aActionHealthKeys)     , aActionHealthKeys      },
    { STD_ARRAYLEN(aActionIMP1Keys)       , aActionIMP1Keys        },
    { STD_ARRAYLEN(aActionIMP2Keys)       , aActionIMP2Keys        },
    { STD_ARRAYLEN(aActionIMP3Keys)       , aActionIMP3Keys        },
    { STD_ARRAYLEN(aActionIMP4Keys)       , aActionIMP4Keys        },
    { STD_ARRAYLEN(aActionIMP5Keys)       , aActionIMP5Keys        },
    { STD_ARRAYLEN(aActionChalk)          , aActionChalk           }
};

static const size_t aActionCrawlJoyButtons[2]     = { STDKEY_C, STDCONTROL_JOYSTICK_GETBUTTON(0, 2) };
static const size_t aActionInterfaceJoyButtons[2] = { STDKEY_I, STDCONTROL_JOYSTICK_GETBUTTON(0, 3) };

static const JonesConfigControlAction jonesConfig_aDfault2Axis4Button[JONESCONTROL_ACTION_NUMACTIONS] =
{
    { STD_ARRAYLEN(aActionWalkFwdKeys)        , aActionWalkFwdKeys },
    { STD_ARRAYLEN(aActionWalkBackKeys)       , aActionWalkBackKeys },
    { STD_ARRAYLEN(aActionTurnLeftKeys)       , aActionTurnLeftKeys },
    { STD_ARRAYLEN(aActionTurnRightKeys)      , aActionTurnRightKeys },
    { STD_ARRAYLEN(aActionStepLeftKeys)       , aActionStepLeftKeys },
    { STD_ARRAYLEN(aActionStepRightKeys)      , aActionStepRightKeys },
    { STD_ARRAYLEN(aActionCrawlJoyButtons)    , aActionCrawlJoyButtons },
    { STD_ARRAYLEN(aActionRunKeys)            , aActionRunKeys },
    { STD_ARRAYLEN(aActionRollKeys)           , aActionRollKeys },
    { STD_ARRAYLEN(aActionJumpKeys)           , aActionJumpKeys },
    { STD_ARRAYLEN(aActionLookKeys)           , aActionLookKeys },
    { STD_ARRAYLEN(aActionActivateKeys)       , aActionActivateKeys },
    { STD_ARRAYLEN(aActionToggleMapKeys)      , aActionToggleMapKeys },
    { STD_ARRAYLEN(aActionPrevWeaponKeys)     , aActionPrevWeaponKeys },
    { STD_ARRAYLEN(aActionNextWeaponKeys)     , aActionNextWeaponKeys },
    { STD_ARRAYLEN(aActionToggleLightKeys)    , aActionToggleLightKeys },
    { STD_ARRAYLEN(aActionFistsKeys)          , aActionFistsKeys },
    { STD_ARRAYLEN(aActionWhipKeys)           , aActionWhipKeys },
    { STD_ARRAYLEN(aActionMauserKeys)         , aActionMauserKeys },
    { STD_ARRAYLEN(aActionPPSH41Keys)         , aActionPPSH41Keys },
    { STD_ARRAYLEN(aActionPistolKeys)         , aActionPistolKeys },
    { STD_ARRAYLEN(aActionSimonovKeys)        , aActionSimonovKeys },
    { STD_ARRAYLEN(aActionTokarevKeys)        , aActionTokarevKeys },
    { STD_ARRAYLEN(aActionToz34Keys)          , aActionToz34Keys },
    { STD_ARRAYLEN(aActionBazookaKeys)        , aActionBazookaKeys },
    { STD_ARRAYLEN(aActionMacheteKeys)        , aActionMacheteKeys },
    { STD_ARRAYLEN(aActionSatchelKeys)        , aActionSatchelKeys },
    { STD_ARRAYLEN(aActionGrenadeKeys)        , aActionGrenadeKeys },
    { STD_ARRAYLEN(aActionMapKeys)            , aActionMapKeys },
    { STD_ARRAYLEN(aActionInterfaceJoyButtons), aActionInterfaceJoyButtons },
    { STD_ARRAYLEN(aActionHealthKeys)         , aActionHealthKeys },
    { STD_ARRAYLEN(aActionIMP1Keys)           , aActionIMP1Keys },
    { STD_ARRAYLEN(aActionIMP2Keys)           , aActionIMP2Keys },
    { STD_ARRAYLEN(aActionIMP3Keys)           , aActionIMP3Keys },
    { STD_ARRAYLEN(aActionIMP4Keys)           , aActionIMP4Keys },
    { STD_ARRAYLEN(aActionIMP5Keys)           , aActionIMP5Keys },
    { STD_ARRAYLEN(aActionChalk)              , aActionChalk }
};

static const size_t aActionRollJoyButtons[2]         = { STDKEY_CAPITAL, STDCONTROL_JOYSTICK_GETBUTTON(0, 4) };
static const size_t aActionNextWeaponJoyButtons[2]   = { STDKEY_PERIOD, STDCONTROL_JOYSTICK_GETBUTTON(0, 5) };
static const size_t aActionToggleWeaponJoyButtons[2] = { STDKEY_SPACE, STDCONTROL_JOYSTICK_GETBUTTON(0, 6) };
static const size_t aActionLookJoyButtons[3]         = { STDKEY_INSERT, STDKEY_NUMPAD0, STDCONTROL_JOYSTICK_GETBUTTON(0, 7) };

static const JonesConfigControlAction jonesConfig_aDefault2Axis8Button[JONESCONTROL_ACTION_NUMACTIONS] =
{
    { STD_ARRAYLEN(aActionWalkFwdKeys)           , aActionWalkFwdKeys            },
    { STD_ARRAYLEN(aActionWalkBackKeys)          , aActionWalkBackKeys           },
    { STD_ARRAYLEN(aActionTurnLeftKeys)          , aActionTurnLeftKeys           },
    { STD_ARRAYLEN(aActionTurnRightKeys)         , aActionTurnRightKeys          },
    { STD_ARRAYLEN(aActionStepLeftKeys)          , aActionStepLeftKeys           },
    { STD_ARRAYLEN(aActionStepRightKeys)         , aActionStepRightKeys          },
    { STD_ARRAYLEN(aActionCrawlJoyButtons)       , aActionCrawlJoyButtons        },
    { STD_ARRAYLEN(aActionRunKeys)               , aActionRunKeys                },
    { STD_ARRAYLEN(aActionRollJoyButtons)        , aActionRollJoyButtons         },
    { STD_ARRAYLEN(aActionJumpKeys)              , aActionJumpKeys               },
    { STD_ARRAYLEN(aActionLookJoyButtons)        , aActionLookJoyButtons         },
    { STD_ARRAYLEN(aActionActivateKeys)          , aActionActivateKeys           },
    { STD_ARRAYLEN(aActionToggleWeaponJoyButtons), aActionToggleWeaponJoyButtons },
    { STD_ARRAYLEN(aActionPrevWeaponKeys)        , aActionPrevWeaponKeys         },
    { STD_ARRAYLEN(aActionNextWeaponJoyButtons)  , aActionNextWeaponJoyButtons   },
    { STD_ARRAYLEN(aActionToggleLightKeys)       , aActionToggleLightKeys        },
    { STD_ARRAYLEN(aActionFistsKeys)             , aActionFistsKeys              },
    { STD_ARRAYLEN(aActionWhipKeys)              , aActionWhipKeys               },
    { STD_ARRAYLEN(aActionMauserKeys)            , aActionMauserKeys             },
    { STD_ARRAYLEN(aActionPPSH41Keys)            , aActionPPSH41Keys             },
    { STD_ARRAYLEN(aActionPistolKeys)            , aActionPistolKeys             },
    { STD_ARRAYLEN(aActionSimonovKeys)           , aActionSimonovKeys            },
    { STD_ARRAYLEN(aActionTokarevKeys)           , aActionTokarevKeys            },
    { STD_ARRAYLEN(aActionToz34Keys)             , aActionToz34Keys              },
    { STD_ARRAYLEN(aActionBazookaKeys)           , aActionBazookaKeys            },
    { STD_ARRAYLEN(aActionMacheteKeys)           , aActionMacheteKeys            },
    { STD_ARRAYLEN(aActionSatchelKeys)           , aActionSatchelKeys            },
    { STD_ARRAYLEN(aActionGrenadeKeys)           , aActionGrenadeKeys            },
    { STD_ARRAYLEN(aActionMapKeys)               , aActionMapKeys                },
    { STD_ARRAYLEN(aActionInterfaceJoyButtons)   , aActionInterfaceJoyButtons    },
    { STD_ARRAYLEN(aActionHealthKeys)            , aActionHealthKeys             },
    { STD_ARRAYLEN(aActionIMP1Keys)              , aActionIMP1Keys               },
    { STD_ARRAYLEN(aActionIMP2Keys)              , aActionIMP2Keys               },
    { STD_ARRAYLEN(aActionIMP3Keys)              , aActionIMP3Keys               },
    { STD_ARRAYLEN(aActionIMP4Keys)              , aActionIMP4Keys               },
    { STD_ARRAYLEN(aActionIMP5Keys)              , aActionIMP5Keys               },
    { STD_ARRAYLEN(aActionChalk)                 , aActionChalk                  }
};

// Added: the keyboard bindings of jonesConfig_aDefaultControlSet plus the ADR-0029 pad layout,
// used as the default scheme for new installs. A = South, B = East, X = West, Y = North, LB/RB are
// buttons 4/5, Back is button 6, L3/R3 are buttons 8/9, the D-pad is POV 0 and LT/RT are the Z and
// RZ axes (Z+ and RZ+; the triggers only read positive). Start stays unbound for the pause menu.
static const size_t aActionStepLeftPadKeys[2]        = { STDKEY_A, STDCONTROL_JOYSTICK_GETBUTTON(0, 4) };
static const size_t aActionStepRightPadKeys[2]       = { STDKEY_S, STDCONTROL_JOYSTICK_GETBUTTON(0, 5) };
static const size_t aActionCrawlPadKeys[2]           = { STDKEY_C, STDCONTROL_JOYSTICK_GETBUTTON(0, 8) };
static const size_t aActionRunPadKeys[3]             = { STDKEY_LSHIFT, STDKEY_RSHIFT, JONESCONFIG_JOYAXISKID_GROUND };
static const size_t aActionRollPadKeys[2]            = { STDKEY_Z, STDCONTROL_JOYSTICK_GETBUTTON(0, 1) };
static const size_t aActionJumpPadKeys[4]            = { STDKEY_X, STDKEY_LMENU, STDKEY_RMENU, STDCONTROL_JOYSTICK_GETBUTTON(0, 0) };
static const size_t aActionLookPadKeys[3]            = { STDKEY_INSERT, STDKEY_NUMPAD0, STDCONTROL_JOYSTICK_GETBUTTON(0, 9) };
static const size_t aActionActivatePadKeys[4]        = { STDKEY_LCONTROL, STDKEY_RCONTROL, STDCONTROL_JOYSTICK_GETBUTTON(0, 2), JONESCONFIG_JOYAXISKID_TWISTR };
static const size_t aActionToggleWeaponPadKeys[2]    = { STDKEY_SPACE, STDCONTROL_JOYSTICK_GETBUTTON(0, 3) };
static const size_t aActionPrevWeaponPadKeys[2]      = { STDKEY_COMMA, STDCONTROL_JOYSTICK_GETPOVLEFT(0, 0) };
static const size_t aActionNextWeaponPadKeys[2]      = { STDKEY_PERIOD, STDCONTROL_JOYSTICK_GETPOVRIGHT(0, 0) };
static const size_t aActionToggleLightPadKeys[2]     = { STDKEY_L, STDCONTROL_JOYSTICK_GETPOVUP(0, 0) };
static const size_t aActionInterfacePadKeys[2]       = { STDKEY_I, STDCONTROL_JOYSTICK_GETBUTTON(0, 6) };
static const size_t aActionHealthPadKeys[2]          = { STDKEY_H, STDCONTROL_JOYSTICK_GETPOVDOWN(0, 0) };

static const JonesConfigControlAction jonesConfig_aDefaultPadControlSet[JONESCONTROL_ACTION_NUMACTIONS] =
{
    { STD_ARRAYLEN(aActionWalkFwdKeys)          , aActionWalkFwdKeys          },
    { STD_ARRAYLEN(aActionWalkBackKeys)         , aActionWalkBackKeys         },
    { STD_ARRAYLEN(aActionTurnLeftKeys)         , aActionTurnLeftKeys         },
    { STD_ARRAYLEN(aActionTurnRightKeys)        , aActionTurnRightKeys        },
    { STD_ARRAYLEN(aActionStepLeftPadKeys)      , aActionStepLeftPadKeys      },
    { STD_ARRAYLEN(aActionStepRightPadKeys)     , aActionStepRightPadKeys     },
    { STD_ARRAYLEN(aActionCrawlPadKeys)         , aActionCrawlPadKeys         },
    { STD_ARRAYLEN(aActionRunPadKeys)           , aActionRunPadKeys           },
    { STD_ARRAYLEN(aActionRollPadKeys)          , aActionRollPadKeys          },
    { STD_ARRAYLEN(aActionJumpPadKeys)          , aActionJumpPadKeys          },
    { STD_ARRAYLEN(aActionLookPadKeys)          , aActionLookPadKeys          },
    { STD_ARRAYLEN(aActionActivatePadKeys)      , aActionActivatePadKeys      },
    { STD_ARRAYLEN(aActionToggleWeaponPadKeys)  , aActionToggleWeaponPadKeys  },
    { STD_ARRAYLEN(aActionPrevWeaponPadKeys)    , aActionPrevWeaponPadKeys    },
    { STD_ARRAYLEN(aActionNextWeaponPadKeys)    , aActionNextWeaponPadKeys    },
    { STD_ARRAYLEN(aActionToggleLightPadKeys)   , aActionToggleLightPadKeys   },
    { STD_ARRAYLEN(aActionFistsKeys)            , aActionFistsKeys            },
    { STD_ARRAYLEN(aActionWhipKeys)             , aActionWhipKeys             },
    { STD_ARRAYLEN(aActionMauserKeys)           , aActionMauserKeys           },
    { STD_ARRAYLEN(aActionPPSH41Keys)           , aActionPPSH41Keys           },
    { STD_ARRAYLEN(aActionPistolKeys)           , aActionPistolKeys           },
    { STD_ARRAYLEN(aActionSimonovKeys)          , aActionSimonovKeys          },
    { STD_ARRAYLEN(aActionTokarevKeys)          , aActionTokarevKeys          },
    { STD_ARRAYLEN(aActionToz34Keys)            , aActionToz34Keys            },
    { STD_ARRAYLEN(aActionBazookaKeys)          , aActionBazookaKeys          },
    { STD_ARRAYLEN(aActionMacheteKeys)          , aActionMacheteKeys          },
    { STD_ARRAYLEN(aActionSatchelKeys)          , aActionSatchelKeys          },
    { STD_ARRAYLEN(aActionGrenadeKeys)          , aActionGrenadeKeys          },
    { STD_ARRAYLEN(aActionMapKeys)              , aActionMapKeys              },
    { STD_ARRAYLEN(aActionInterfacePadKeys)     , aActionInterfacePadKeys     },
    { STD_ARRAYLEN(aActionHealthPadKeys)        , aActionHealthPadKeys        },
    { STD_ARRAYLEN(aActionIMP1Keys)             , aActionIMP1Keys             },
    { STD_ARRAYLEN(aActionIMP2Keys)             , aActionIMP2Keys             },
    { STD_ARRAYLEN(aActionIMP3Keys)             , aActionIMP3Keys             },
    { STD_ARRAYLEN(aActionIMP4Keys)             , aActionIMP4Keys             },
    { STD_ARRAYLEN(aActionIMP5Keys)             , aActionIMP5Keys             },
    { STD_ARRAYLEN(aActionChalk)                , aActionChalk                }
};

// Added: the Modern keyboard and gamepad default (ADR-0029). Its keyboard follows the Modern
// model: WASD and the arrows move, Left Alt is the quick-step modifier for a held direction,
// Space jumps, Ctrl and the left mouse button activate and fire, R draws, V and the middle mouse
// button snap the camera. The step-right row carries the quick-step key, which the Modern binder
// maps to SITHCONTROL_QUICKSTEP; the step-left row is free, so LB is unbound in this scheme. Its
// gamepad part is the keyboard-and-gamepad scheme's, with LT still stored as Run so the Modern
// binder maps it to target lock.
static const size_t aActionWalkFwdModernKeys[2]       = { STDKEY_W, STDKEY_UP };
static const size_t aActionWalkBackModernKeys[2]      = { STDKEY_S, STDKEY_DOWN };
static const size_t aActionTurnLeftModernKeys[2]      = { STDKEY_A, STDKEY_LEFT };
static const size_t aActionTurnRightModernKeys[2]     = { STDKEY_D, STDKEY_RIGHT };
static const size_t aActionStepRightModernKeys[2]     = { STDKEY_LMENU, STDCONTROL_JOYSTICK_GETBUTTON(0, 5) };
static const size_t aActionCrawlModernKeys[2]         = { STDKEY_C, STDCONTROL_JOYSTICK_GETBUTTON(0, 8) };
static const size_t aActionRunModernKeys[2]           = { STDKEY_LSHIFT, JONESCONFIG_JOYAXISKID_GROUND };
static const size_t aActionRollModernKeys[2]          = { STDKEY_Z, STDCONTROL_JOYSTICK_GETBUTTON(0, 1) };
static const size_t aActionJumpModernKeys[2]          = { STDKEY_SPACE, STDCONTROL_JOYSTICK_GETBUTTON(0, 0) };
static const size_t aActionLookModernKeys[3]          = { STDKEY_V, STDCONTROL_KID_MOUSE_MBUTTON, STDCONTROL_JOYSTICK_GETBUTTON(0, 9) };
static const size_t aActionActivateModernKeys[4]      = { STDKEY_LCONTROL, STDCONTROL_KID_MOUSE_LBUTTON, STDCONTROL_JOYSTICK_GETBUTTON(0, 2), JONESCONFIG_JOYAXISKID_TWISTR };
static const size_t aActionToggleWeaponModernKeys[2]  = { STDKEY_R, STDCONTROL_JOYSTICK_GETBUTTON(0, 3) };
static const size_t aActionPrevWeaponModernKeys[2]    = { STDKEY_COMMA, STDCONTROL_JOYSTICK_GETPOVLEFT(0, 0) };
static const size_t aActionNextWeaponModernKeys[2]    = { STDKEY_PERIOD, STDCONTROL_JOYSTICK_GETPOVRIGHT(0, 0) };
static const size_t aActionToggleLightModernKeys[2]   = { STDKEY_L, STDCONTROL_JOYSTICK_GETPOVUP(0, 0) };
static const size_t aActionInterfaceModernKeys[3]     = { STDKEY_TAB, STDKEY_I, STDCONTROL_JOYSTICK_GETBUTTON(0, 6) };
static const size_t aActionHealthModernKeys[2]        = { STDKEY_H, STDCONTROL_JOYSTICK_GETPOVDOWN(0, 0) };

// TODO: D-pad left/right should cycle the inventory's favourite weapons once that feature exists;
// until then they stay previous/next owned weapon.
static const JonesConfigControlAction jonesConfig_aDefaultModernControlSet[JONESCONTROL_ACTION_NUMACTIONS] =
{
    { STD_ARRAYLEN(aActionWalkFwdModernKeys)     , aActionWalkFwdModernKeys      },
    { STD_ARRAYLEN(aActionWalkBackModernKeys)    , aActionWalkBackModernKeys     },
    { STD_ARRAYLEN(aActionTurnLeftModernKeys)    , aActionTurnLeftModernKeys     },
    { STD_ARRAYLEN(aActionTurnRightModernKeys)   , aActionTurnRightModernKeys    },
    { 0                                          , NULL                          },
    { STD_ARRAYLEN(aActionStepRightModernKeys)   , aActionStepRightModernKeys    },
    { STD_ARRAYLEN(aActionCrawlModernKeys)       , aActionCrawlModernKeys        },
    { STD_ARRAYLEN(aActionRunModernKeys)         , aActionRunModernKeys          },
    { STD_ARRAYLEN(aActionRollModernKeys)        , aActionRollModernKeys         },
    { STD_ARRAYLEN(aActionJumpModernKeys)        , aActionJumpModernKeys         },
    { STD_ARRAYLEN(aActionLookModernKeys)        , aActionLookModernKeys         },
    { STD_ARRAYLEN(aActionActivateModernKeys)    , aActionActivateModernKeys     },
    { STD_ARRAYLEN(aActionToggleWeaponModernKeys), aActionToggleWeaponModernKeys },
    { STD_ARRAYLEN(aActionPrevWeaponModernKeys)  , aActionPrevWeaponModernKeys   },
    { STD_ARRAYLEN(aActionNextWeaponModernKeys)  , aActionNextWeaponModernKeys   },
    { STD_ARRAYLEN(aActionToggleLightModernKeys) , aActionToggleLightModernKeys  },
    { STD_ARRAYLEN(aActionFistsKeys)             , aActionFistsKeys              },
    { STD_ARRAYLEN(aActionWhipKeys)              , aActionWhipKeys               },
    { STD_ARRAYLEN(aActionMauserKeys)            , aActionMauserKeys             },
    { STD_ARRAYLEN(aActionPPSH41Keys)            , aActionPPSH41Keys             },
    { STD_ARRAYLEN(aActionPistolKeys)            , aActionPistolKeys             },
    { STD_ARRAYLEN(aActionSimonovKeys)           , aActionSimonovKeys            },
    { STD_ARRAYLEN(aActionTokarevKeys)           , aActionTokarevKeys            },
    { STD_ARRAYLEN(aActionToz34Keys)             , aActionToz34Keys              },
    { STD_ARRAYLEN(aActionBazookaKeys)           , aActionBazookaKeys            },
    { STD_ARRAYLEN(aActionMacheteKeys)           , aActionMacheteKeys            },
    { STD_ARRAYLEN(aActionSatchelKeys)           , aActionSatchelKeys            },
    { STD_ARRAYLEN(aActionGrenadeKeys)           , aActionGrenadeKeys            },
    { STD_ARRAYLEN(aActionMapKeys)               , aActionMapKeys                },
    { STD_ARRAYLEN(aActionInterfaceModernKeys)   , aActionInterfaceModernKeys    },
    { STD_ARRAYLEN(aActionHealthModernKeys)      , aActionHealthModernKeys       },
    { 0                                          , NULL                          },
    { 0                                          , NULL                          },
    { 0                                          , NULL                          },
    { 0                                          , NULL                          },
    { 0                                          , NULL                          },
    { STD_ARRAYLEN(aActionChalk)                 , aActionChalk                  }
};

static const JonesConfigControlAction* jonesConfig_aDfltKeySets[] =
{
    jonesConfig_aDefaultControlSet,
    jonesConfig_aDfault2Axis4Button,
    jonesConfig_aDefault2Axis8Button,
    jonesConfig_aDefaultPadControlSet,
    jonesConfig_aDefaultModernControlSet
};

static const char* jonesConfig_aDfltKeySetNames[JONESCONFIG_NUMDEFAULTCONTROLSCHEMES] =
{
    "JONES_STR_DFLTKEY",
    "JONES_STR_DFLT2A4B",
    "JONES_STR_DFLT2A8B",
    "JONES_STR_DFLTPAD",
    "JONES_STR_DFLTMODERN"
};

static const char* jonesConfig_aDfltKeySetFallbackNames[JONESCONFIG_NUMDEFAULTCONTROLSCHEMES] =
{
    "JONES_STR_DFLTKEY",
    "JONES_STR_DFLT2A4B",
    "JONES_STR_DFLT2A8B",
    "Keyboard and gamepad",
    "Modern keyboard and gamepad"
};

static int JonesControlSchemes_IsMouseButton(size_t controlId)
{
    return controlId >= STDCONTROL_KID_MOUSE_LBUTTON && controlId <= STDCONTROL_KID_MOUSE_XBUTTON;
}

static int JonesControlSchemes_IsOriginalAxisControl(size_t controlId)
{
    return controlId == JONESCONFIG_JOYAXISKID_UP
        || controlId == JONESCONFIG_JOYAXISKID_RIGHT
        || controlId == JONESCONFIG_JOYAXISKID_DOWN
        || controlId == JONESCONFIG_JOYAXISKID_LEFT
        || controlId == JONESCONFIG_JOYAXISKID_TWISTL
        || controlId == JONESCONFIG_JOYAXISKID_TWISTR;
}

int J3DAPI JonesControlSchemes_IsExtraControl(size_t controlId)
{
    return JonesControlSchemes_IsMouseButton(controlId)
        || controlId == JONESCONFIG_JOYAXISKID_AIR
        || controlId == JONESCONFIG_JOYAXISKID_GROUND
        || controlId == JONESCONTROLSCHEMES_JOYAXIS_RX_NEG
        || controlId == JONESCONTROLSCHEMES_JOYAXIS_RX_POS
        || controlId == JONESCONTROLSCHEMES_JOYAXIS_RY_NEG
        || controlId == JONESCONTROLSCHEMES_JOYAXIS_RY_POS;
}

static int JonesControlSchemes_IsKeyboardControl(size_t controlId)
{
    return controlId < STDCONTROL_MAX_KEYBOARD_BUTTONS;
}

static int JonesControlSchemes_IsJoystickControl(size_t controlId)
{
    return controlId >= STDCONTROL_JOYSTICK_FIRSTKID && controlId <= STDCONTROL_JOYSTICK_LASTPOVCID;
}

static int JonesControlSchemes_IsValidControl(size_t controlId)
{
    return JonesControlSchemes_IsKeyboardControl(controlId)
        || JonesControlSchemes_IsJoystickControl(controlId)
        || JonesControlSchemes_IsMouseButton(controlId)
        || JonesControlSchemes_IsOriginalAxisControl(controlId)
        || controlId == JONESCONFIG_JOYAXISKID_AIR
        || controlId == JONESCONFIG_JOYAXISKID_GROUND
        || controlId == JONESCONTROLSCHEMES_JOYAXIS_RX_NEG
        || controlId == JONESCONTROLSCHEMES_JOYAXIS_RX_POS
        || controlId == JONESCONTROLSCHEMES_JOYAXIS_RY_NEG
        || controlId == JONESCONTROLSCHEMES_JOYAXIS_RY_POS;
}

// Added: true for the key ids the original ControlToString named. The table also has names for
// SCROLL, SYSRQ and APPS, but the original switch sent those to the joystick branch and produced an
// empty string, so they stay unnamed here to keep the writer's bytes identical.
static int JonesControlSchemes_IsNamedKeyId(size_t controlId)
{
    switch ( controlId )
    {
        case STDKEY_1:
        case STDKEY_2:
        case STDKEY_3:
        case STDKEY_4:
        case STDKEY_5:
        case STDKEY_6:
        case STDKEY_7:
        case STDKEY_8:
        case STDKEY_9:
        case STDKEY_0:
        case STDKEY_Q:
        case STDKEY_W:
        case STDKEY_E:
        case STDKEY_R:
        case STDKEY_T:
        case STDKEY_Y:
        case STDKEY_U:
        case STDKEY_I:
        case STDKEY_O:
        case STDKEY_P:
        case STDKEY_A:
        case STDKEY_S:
        case STDKEY_D:
        case STDKEY_F:
        case STDKEY_G:
        case STDKEY_H:
        case STDKEY_J:
        case STDKEY_K:
        case STDKEY_L:
        case STDKEY_SEMICOLON:
        case STDKEY_APOSTROPHE:
        case STDKEY_GRAVE:
        case STDKEY_BACKSLASH:
        case STDKEY_Z:
        case STDKEY_X:
        case STDKEY_C:
        case STDKEY_V:
        case STDKEY_B:
        case STDKEY_N:
        case STDKEY_M:
        case STDKEY_PERIOD:
        case STDKEY_SLASH:
        case STDKEY_ESCAPE:
        case STDKEY_MINUS:
        case STDKEY_EQUALS:
        case STDKEY_BACK:
        case STDKEY_TAB:
        case STDKEY_LBRACKET:
        case STDKEY_RBRACKET:
        case STDKEY_RETURN:
        case STDKEY_LCONTROL:
        case STDKEY_LSHIFT:
        case STDKEY_COMMA:
        case STDKEY_RSHIFT:
        case STDKEY_MULTIPLY:
        case STDKEY_LMENU:
        case STDKEY_SPACE:
        case STDKEY_CAPITAL:
        case STDKEY_F1:
        case STDKEY_F2:
        case STDKEY_F3:
        case STDKEY_F4:
        case STDKEY_F5:
        case STDKEY_F6:
        case STDKEY_F7:
        case STDKEY_F8:
        case STDKEY_F9:
        case STDKEY_F10:
        case STDKEY_NUMLOCK:
        case STDKEY_NUMPAD7:
        case STDKEY_NUMPAD8:
        case STDKEY_NUMPAD9:
        case STDKEY_SUBTRACT:
        case STDKEY_NUMPAD4:
        case STDKEY_NUMPAD5:
        case STDKEY_NUMPAD6:
        case STDKEY_ADD:
        case STDKEY_NUMPAD1:
        case STDKEY_NUMPAD2:
        case STDKEY_NUMPAD3:
        case STDKEY_NUMPAD0:
        case STDKEY_DECIMAL:
        case STDKEY_F11:
        case STDKEY_F12:
        case STDKEY_F13:
        case STDKEY_F14:
        case STDKEY_F15:
        case STDKEY_NUMPADEQUALS:
        case STDKEY_NUMPADENTER:
        case STDKEY_RCONTROL:
        case STDKEY_NUMPADCOMMA:
        case STDKEY_DIVIDE:
        case STDKEY_RMENU:
        case STDKEY_HOME:
        case STDKEY_UP:
        case STDKEY_PRIOR:
        case STDKEY_LEFT:
        case STDKEY_RIGHT:
        case STDKEY_END:
        case STDKEY_DOWN:
        case STDKEY_NEXT:
        case STDKEY_INSERT:
        case STDKEY_DELETE:
        case STDKEY_LWIN:
        case STDKEY_RWIN:
            return 1;
        default:
            return 0;
    }
}

static const char* JonesControlSchemes_JoystickButtonFormat(void)
{
    return JonesControlSchemes_ResolveOr("JONES_STR_CAPS_BUTTON", "BUTTON");
}

static const char* JonesControlSchemes_HatFormat(const char* pKey, const char* pCanonical)
{
    return JonesControlSchemes_ResolveOr(pKey, pCanonical);
}

static void JonesControlSchemes_FormatPadded(char* pDst, size_t dstSize, const char* pText, size_t padTo)
{
    size_t len = pText ? strlen(pText) : 0;
    if ( padTo <= len )
    {
        stdUtil_StringCopy(pDst, dstSize, pText ? pText : "");
        return;
    }

    stdUtil_StringCopy(pDst, dstSize, pText ? pText : "");
    size_t cur = strlen(pDst);
    while ( cur + 1 < dstSize && cur < padTo )
    {
        pDst[cur++] = ' ';
    }
    pDst[cur] = '\0';
}

void J3DAPI jonesConfig_ControlToString(size_t controlId, char* pDest)
{
    if ( !pDest )
    {
        return;
    }

    memset(pDest, 0, JONESCONFIG_CONTROLSTRING_MAXLEN);

    if ( controlId < JONESCONFIG_JOYAXISKID_UP )
    {
        if ( JonesControlSchemes_IsKeyboardControl(controlId) )
        {
            if ( JonesControlSchemes_IsNamedKeyId(controlId) )
            {
                const char* pName = jonesConfig_aControlKeyStrings[controlId];
                if ( pName )
                {
                    stdUtil_StringCopy(pDest, JONESCONFIG_CONTROLSTRING_MAXLEN, JonesControlSchemes_ResolveKey(pName));
                    stdUtil_ToUpper(pDest);
                }
            }
            return;
        }

        // Added: mouse buttons were unnamed in the original; the Controls screen needs them.
        if ( JonesControlSchemes_IsMouseButton(controlId) )
        {
            stdUtil_Format(pDest, JONESCONFIG_CONTROLSTRING_MAXLEN, "MOUSE%u", (unsigned)(controlId - STDCONTROL_KID_MOUSE_LBUTTON + 1));
            return;
        }

        // Joystick buttons and POV directions, as the original joystick_controls branch.
        if ( controlId >= STDCONTROL_JOYSTICK_FIRSTKID && controlId <= STDCONTROL_JOYSTICK_LASTPOVCID )
        {
            const char* pName = NULL;
            if ( controlId >= STDCONTROL_JOYSTICK_FIRTPOVCID && controlId <= STDCONTROL_JOYSTICK_LASTPOVCID )
            {
                switch ( STDCONTROL_JOYSTICK_GETPOVDIRECTIONINDEX(controlId) )
                {
                    case STDCONTROL_JOYSTICK_POVDIRLEFT:
                        pName = JonesControlSchemes_ResolveOr("JONES_STR_HATLEFT", "HAT%i LEFT");
                        break;
                    case STDCONTROL_JOYSTICK_POVDIRUP:
                        pName = JonesControlSchemes_ResolveOr("JONES_STR_HATUP", "HAT%i UP");
                        break;
                    case STDCONTROL_JOYSTICK_POVDIRRIGHT:
                        pName = JonesControlSchemes_ResolveOr("JONES_STR_HATRIGHT", "HAT%i RIGHT");
                        break;
                    case STDCONTROL_JOYSTICK_POVDIRDOWN:
                        pName = JonesControlSchemes_ResolveOr("JONES_STR_HATDOWN", "HAT%i DOWN");
                        break;
                    default:
                        break;
                }

                if ( pName )
                {
                    stdUtil_Format(pDest, JONESCONFIG_CONTROLSTRING_MAXLEN, pName, (unsigned)(STDCONTROL_JOYSTICK_GETPOVINDEX(controlId) + 1));
                    return;
                }
            }
            else
            {
                pName = JonesControlSchemes_ResolveOr("JONES_STR_JOYSTICK", "BUTTON%i");
                stdUtil_Format(pDest, JONESCONFIG_CONTROLSTRING_MAXLEN, pName, (unsigned)(STDCONTROL_JOYSTICK_GETBUTTONINDEX(controlId) + 1));
                return;
            }
        }

        return;
    }

    // Added: joystick axes the original cannot express.
    const char* pExtra = NULL;
    switch ( controlId )
    {
        case JONESCONFIG_JOYAXISKID_UP:
            pExtra = JonesControlSchemes_ResolveOr("JONES_STR_JOYUP", "FORWARD");
            break;
        case JONESCONFIG_JOYAXISKID_RIGHT:
            pExtra = JonesControlSchemes_ResolveOr("JONES_STR_JOYRIGHT", "RIGHT");
            break;
        case JONESCONFIG_JOYAXISKID_DOWN:
            pExtra = JonesControlSchemes_ResolveOr("JONES_STR_JOYDOWN", "BACKWARD");
            break;
        case JONESCONFIG_JOYAXISKID_LEFT:
            pExtra = JonesControlSchemes_ResolveOr("JONES_STR_JOYLEFT", "LEFT");
            break;
        case JONESCONFIG_JOYAXISKID_TWISTL:
            pExtra = JonesControlSchemes_ResolveOr("JONES_STR_JOYTWISTL", "TWISTL");
            break;
        case JONESCONFIG_JOYAXISKID_TWISTR:
            pExtra = JonesControlSchemes_ResolveOr("JONES_STR_JOYTWISTR", "TWISTR");
            break;
        case JONESCONFIG_JOYAXISKID_AIR:
            pExtra = "Z-";
            break;
        case JONESCONFIG_JOYAXISKID_GROUND:
            pExtra = "Z+";
            break;
        case JONESCONTROLSCHEMES_JOYAXIS_RX_NEG:
            pExtra = "RX-";
            break;
        case JONESCONTROLSCHEMES_JOYAXIS_RX_POS:
            pExtra = "RX+";
            break;
        case JONESCONTROLSCHEMES_JOYAXIS_RY_NEG:
            pExtra = "RY-";
            break;
        case JONESCONTROLSCHEMES_JOYAXIS_RY_POS:
            pExtra = "RY+";
            break;
        default:
            break;
    }

    if ( pExtra )
    {
        stdUtil_StringCopy(pDest, JONESCONFIG_CONTROLSTRING_MAXLEN, pExtra);
    }
}

int J3DAPI JonesControlSchemes_ParseControlName(const char* pName, size_t* pControlId)
{
    if ( !pName || !pControlId )
    {
        return 0;
    }

    char aName[256] = { 0 };
    stdUtil_StringCopy(aName, STD_ARRAYLEN(aName), pName);
    stdUtil_ToUpper(aName);

    // Keyboard and named keys, case-insensitively, including the JONES_STR_ key fallback.
    for ( size_t i = 0; i < STD_ARRAYLEN(jonesConfig_aControlKeyStrings); ++i )
    {
        const char* pKey = jonesConfig_aControlKeyStrings[i];
        if ( !pKey )
        {
            continue;
        }

        const char* pValue = JonesControlSchemes_ResolveKey(pKey);
        if ( pValue && strneqi(aName, pValue, strlen(pValue)) && strlen(aName) == strlen(pValue) )
        {
            *pControlId = i;
            return 1;
        }
    }

    // Mouse buttons.
    if ( strneqi(aName, "MOUSE", 5) )
    {
        const char* pDigits = aName + 5;
        if ( *pDigits )
        {
            char* pEnd = NULL;
            long button = strtol(pDigits, &pEnd, 10);
            if ( pEnd && *pEnd == '\0' && button >= 1 && button <= (long)STDCONTROL_MAX_MOUSE_BUTTONS )
            {
                *pControlId = STDCONTROL_KID_MOUSE_LBUTTON + (size_t)(button - 1);
                return 1;
            }
        }
    }

    // Extra axes.
    if ( strneqi(aName, "Z-", 2) )
    {
        *pControlId = JONESCONFIG_JOYAXISKID_AIR;
        return 1;
    }
    if ( strneqi(aName, "Z+", 2) )
    {
        *pControlId = JONESCONFIG_JOYAXISKID_GROUND;
        return 1;
    }
    if ( strneqi(aName, "RX-", 3) )
    {
        *pControlId = JONESCONTROLSCHEMES_JOYAXIS_RX_NEG;
        return 1;
    }
    if ( strneqi(aName, "RX+", 3) )
    {
        *pControlId = JONESCONTROLSCHEMES_JOYAXIS_RX_POS;
        return 1;
    }
    if ( strneqi(aName, "RY-", 3) )
    {
        *pControlId = JONESCONTROLSCHEMES_JOYAXIS_RY_NEG;
        return 1;
    }
    if ( strneqi(aName, "RY+", 3) )
    {
        *pControlId = JONESCONTROLSCHEMES_JOYAXIS_RY_POS;
        return 1;
    }

    // Joystick button: <button format><n>. Try the localized format and the canonical one.
    const char* aButtonFormats[2] = { JonesControlSchemes_JoystickButtonFormat(), "BUTTON" };
    for ( size_t f = 0; f < STD_ARRAYLEN(aButtonFormats); ++f )
    {
        size_t formatLen = strlen(aButtonFormats[f]);
        if ( formatLen && strneqi(aName, aButtonFormats[f], formatLen) )
        {
            const char* pDigits = aName + formatLen;
            if ( *pDigits )
            {
                char* pEnd = NULL;
                long button = strtol(pDigits, &pEnd, 10);
                if ( pEnd && *pEnd == '\0' && button >= 1 && button <= (long)STDCONTROL_NUM_JOYSTICK_BUTTONS )
                {
                    *pControlId = STDCONTROL_JOYSTICK_GETBUTTON(0, (size_t)(button - 1));
                    return 1;
                }
            }
        }
    }

    // POV: <hat format with %i> where the number is the POV index + 1.
    static const char* aHatKeys[4] = { "JONES_STR_HATLEFT", "JONES_STR_HATUP", "JONES_STR_HATRIGHT", "JONES_STR_HATDOWN" };
    static const char* aHatCanon[4] = { "HAT%i LEFT", "HAT%i UP", "HAT%i RIGHT", "HAT%i DOWN" };
    for ( size_t dir = 0; dir < 4; ++dir )
    {
        const char* aFormats[2] = { JonesControlSchemes_HatFormat(aHatKeys[dir], aHatCanon[dir]), aHatCanon[dir] };
        for ( size_t f = 0; f < STD_ARRAYLEN(aFormats); ++f )
        {
            const char* pPercent = strstr(aFormats[f], "%i");
            if ( !pPercent )
            {
                continue;
            }

            size_t prefixLen = (size_t)(pPercent - aFormats[f]);
            const char* pSuffix = pPercent + 2;
            size_t suffixLen = strlen(pSuffix);
            size_t nameLen = strlen(aName);
            if ( nameLen < prefixLen + 1 + suffixLen )
            {
                continue;
            }

            if ( !strneqi(aName, aFormats[f], prefixLen)
                || !strneqi(aName + nameLen - suffixLen, pSuffix, suffixLen) )
            {
                continue;
            }

            char aNumber[16] = { 0 };
            size_t numLen = nameLen - prefixLen - suffixLen;
            if ( numLen == 0 || numLen >= STD_ARRAYLEN(aNumber) )
            {
                continue;
            }

            memcpy(aNumber, aName + prefixLen, numLen);
            aNumber[numLen] = '\0';
            char* pEnd = NULL;
            long pov = strtol(aNumber, &pEnd, 10);
            if ( !pEnd || *pEnd != '\0' || pov < 1 || pov > (long)STDCONTROL_MAX_JOYSTICK_POVCONTROLERS )
            {
                continue;
            }

            *pControlId = STDCONTROL_JOYSTICK_GETPOV(0, (size_t)(pov - 1), dir);
            return 1;
        }
    }

    // Joystick axes: FORWARD/BACKWARD/LEFT/RIGHT and the twist names.
    struct
    {
        const char* pKey;
        const char* pCanonical;
        size_t controlId;
    } aAxes[6] =
    {
        { "JONES_STR_JOYUP", "FORWARD", JONESCONFIG_JOYAXISKID_UP },
        { "JONES_STR_JOYDOWN", "BACKWARD", JONESCONFIG_JOYAXISKID_DOWN },
        { "JONES_STR_JOYLEFT", "LEFT", JONESCONFIG_JOYAXISKID_LEFT },
        { "JONES_STR_JOYRIGHT", "RIGHT", JONESCONFIG_JOYAXISKID_RIGHT },
        { "JONES_STR_JOYTWISTL", "TWISTL", JONESCONFIG_JOYAXISKID_TWISTL },
        { "JONES_STR_JOYTWISTR", "TWISTR", JONESCONFIG_JOYAXISKID_TWISTR }
    };

    for ( size_t i = 0; i < STD_ARRAYLEN(aAxes); ++i )
    {
        const char* aValues[2] = { JonesControlSchemes_ResolveKey(aAxes[i].pKey), aAxes[i].pCanonical };
        for ( size_t v = 0; v < STD_ARRAYLEN(aValues); ++v )
        {
            if ( aValues[v] && strneqi(aName, aValues[v], strlen(aValues[v])) && strlen(aName) == strlen(aValues[v]) )
            {
                *pControlId = aAxes[i].controlId;
                return 1;
            }
        }
    }

    return 0;
}

size_t J3DAPI JonesControlSchemes_MapAxisControl(size_t engineAxisControlId)
{
    size_t aid = STDCONTROL_GETAID(engineAxisControlId);
    if ( aid >= STDCONTROL_MAX_JOYSTICK_AXES )
    {
        return (size_t)-1;
    }

    size_t axis  = aid % STDCONTROL_JOYSTICK_NUMAXES;
    int negative = (engineAxisControlId & STDCONTROL_AID_NEGATIVE_AXIS) == STDCONTROL_AID_NEGATIVE_AXIS;

    switch ( axis )
    {
        case STDCONTROL_JOYSTICK_AXIS_X:
            return negative ? JONESCONFIG_JOYAXISKID_LEFT : JONESCONFIG_JOYAXISKID_RIGHT;
        case STDCONTROL_JOYSTICK_AXIS_Y:
            return negative ? JONESCONFIG_JOYAXISKID_UP : JONESCONFIG_JOYAXISKID_DOWN;
        case STDCONTROL_JOYSTICK_AXIS_Z:
            return negative ? JONESCONFIG_JOYAXISKID_AIR : JONESCONFIG_JOYAXISKID_GROUND;
        case STDCONTROL_JOYSTICK_AXIS_RX:
            return negative ? JONESCONTROLSCHEMES_JOYAXIS_RX_NEG : JONESCONTROLSCHEMES_JOYAXIS_RX_POS;
        case STDCONTROL_JOYSTICK_AXIS_RY:
            return negative ? JONESCONTROLSCHEMES_JOYAXIS_RY_NEG : JONESCONTROLSCHEMES_JOYAXIS_RY_POS;
        case STDCONTROL_JOYSTICK_AXIS_RZ:
            return negative ? JONESCONFIG_JOYAXISKID_TWISTL : JONESCONFIG_JOYAXISKID_TWISTR;
        default:
            return (size_t)-1;
    }
}

// --- Key-sets directory and table initialization ---------------------------------------------

int J3DAPI jonesConfig_InitKeySetsPath(void)
{
    char aKeysets[128] = { 0 };

    if ( sithString_HasStarted() )
    {
        const wchar_t* pwKeySets = sithString_GetString("SITHSTRING_KEYSETS");
        if ( pwKeySets )
        {
            char* pKeySets = stdUtil_ToAString(pwKeySets);
            if ( pKeySets )
            {
                stdUtil_StringCopy(aKeysets, STD_ARRAYLEN(aKeysets), pKeySets);
                stdMemory_Free(pKeySets);
            }
        }
    }

    if ( !aKeysets[0] )
    {
        // Added: the original required the game string table. Without game data the Babylon
        // column still needs a directory for headless tests and the Controls screen.
        stdUtil_StringCopy(aKeysets, STD_ARRAYLEN(aKeysets), "keysets");
    }

    char aInstallPath[256] = { 0 };
    stdConfig_GetString(SITH_CFG_INSTALLPATH, aInstallPath, STD_ARRAYLEN(aInstallPath), "");

    char aShippedDirPath[JONESCONFIG_KEYSDIRPATH_MAX] = { 0 };
    if ( !aInstallPath[0] )
    {
        STD_FORMAT(aShippedDirPath, "..\\\\%s", aKeysets);
    }
    else
    {
        size_t len = strlen(aInstallPath);
        if ( aInstallPath[len - 1] == '\\' || aInstallPath[len - 1] == '/' )
        {
            STD_FORMAT(aShippedDirPath, "%s%s", aInstallPath, aKeysets);
        }
        else
        {
            STD_FORMAT(aShippedDirPath, "%s\\%s", aInstallPath, aKeysets);
        }
    }

    // Added: user-made key sets go to <user dir>/KeySets; the shipped directory stays readable.
    if ( stdUserDir_GetPath()[0] != '\0' )
    {
        stdUtil_StringCopy(jonesConfig_aShippedKeySetsDirPath,
            STD_ARRAYLEN(jonesConfig_aShippedKeySetsDirPath), aShippedDirPath);

        if ( !stdUserDir_MakePath(jonesConfig_aKeySetsDirPath,
                STD_ARRAYLEN(jonesConfig_aKeySetsDirPath), "KeySets") )
        {
            STDLOG_ERROR("Control scheme: the user directory path is too long for KeySets.\n");
            jonesConfig_aKeySetsDirPath[0] = '\0';
        }
    }
    else
    {
        stdUtil_StringCopy(jonesConfig_aKeySetsDirPath,
            STD_ARRAYLEN(jonesConfig_aKeySetsDirPath), aShippedDirPath);
        jonesConfig_aShippedKeySetsDirPath[0] = '\0';
    }

    return 1;
}

static size_t JonesControlSchemes_CountWords(const char* pText)
{
    if ( !pText || !*pText )
    {
        return 0;
    }

    size_t numWords = 1;
    for ( const char* p = pText; *p; ++p )
    {
        if ( *p == ' ' )
        {
            ++numWords;
        }
    }

    return numWords;
}

static size_t JonesControlSchemes_RoundUpToFive(size_t value)
{
    return value + (5 - value % 5);
}

int J3DAPI jonesConfig_InitKeyActions(void)
{
    jonesConfig_maxActionWords  = 0;
    jonesConfig_maxActionStrLen = 0;
    jonesConfig_maxControlWords  = 0;
    jonesConfig_maxControlStrLen = 0;

    for ( size_t i = 0; i < STD_ARRAYLEN(jonesConfig_aJonesCapControlActionNames); ++i )
    {
        const char* pActionName = JonesControlSchemes_ResolveKey(jonesConfig_aJonesCapControlActionNames[i]);
        if ( !pActionName )
        {
            return 0;
        }

        size_t numWords = JonesControlSchemes_CountWords(pActionName);
        if ( numWords > jonesConfig_maxActionWords )
        {
            jonesConfig_maxActionWords = numWords;
        }

        size_t len = strlen(pActionName);
        if ( len > jonesConfig_maxActionStrLen )
        {
            jonesConfig_maxActionStrLen = len;
        }
    }

    for ( size_t i = 0; i < STDCONTROL_MAX_KEYID + JONESCONFIG_NUM_JOYAXISKIDS + 4; ++i )
    {
        char aControlStr[JONESCONFIG_CONTROLSTRING_MAXLEN] = { 0 };
        switch ( i )
        {
            case 644:
                jonesConfig_ControlToString(JONESCONFIG_JOYAXISKID_UP, aControlStr);
                break;
            case 645:
                jonesConfig_ControlToString(JONESCONFIG_JOYAXISKID_DOWN, aControlStr);
                break;
            case 646:
                jonesConfig_ControlToString(JONESCONFIG_JOYAXISKID_LEFT, aControlStr);
                break;
            case 647:
                jonesConfig_ControlToString(JONESCONFIG_JOYAXISKID_RIGHT, aControlStr);
                break;
            case 648:
                jonesConfig_ControlToString(JONESCONFIG_JOYAXISKID_AIR, aControlStr);
                break;
            case 649:
                jonesConfig_ControlToString(JONESCONFIG_JOYAXISKID_GROUND, aControlStr);
                break;
            case 650:
                jonesConfig_ControlToString(JONESCONFIG_JOYAXISKID_TWISTL, aControlStr);
                break;
            case 651:
                jonesConfig_ControlToString(JONESCONFIG_JOYAXISKID_TWISTR, aControlStr);
                break;
            default:
                jonesConfig_ControlToString(i, aControlStr);
                break;
        }

        size_t numWords = JonesControlSchemes_CountWords(aControlStr);
        if ( numWords > jonesConfig_maxControlWords )
        {
            jonesConfig_maxControlWords = numWords;
        }

        size_t len = strlen(aControlStr);
        if ( len > jonesConfig_maxControlStrLen )
        {
            jonesConfig_maxControlStrLen = len;
        }
    }

    jonesConfig_maxActionPaddedStrLen  = JonesControlSchemes_RoundUpToFive(jonesConfig_maxActionStrLen);
    jonesConfig_maxControlPaddedStrLen = JonesControlSchemes_RoundUpToFive(jonesConfig_maxControlStrLen);
    jonesConfig_bKeyActionsInited = 1;
    return 1;
}

static void JonesControlSchemes_EnsureTables(void)
{
    if ( !jonesConfig_bKeyActionsInited )
    {
        jonesConfig_InitKeyActions();
    }

    if ( !jonesConfig_aKeySetsDirPath[0] )
    {
        jonesConfig_InitKeySetsPath();
    }
}

// --- Scheme memory ----------------------------------------------------------------------------

void J3DAPI jonesConfig_FreeControlScheme(JonesControlsScheme* pScheme)
{
    if ( !pScheme )
    {
        return;
    }

    memset(pScheme->aActions, 0, sizeof(pScheme->aActions));
    memset(pScheme->aName, 0, sizeof(pScheme->aName));
    stdMemory_Free(pScheme);
}

void J3DAPI jonesConfig_FreeControlConfigEntry(JonesControlsConfig* pConfig)
{
    if ( !pConfig )
    {
        return;
    }

    if ( pConfig->aSchemes )
    {
        memset(pConfig->aSchemes, 0, sizeof(JonesControlsScheme) * pConfig->numSchemes);
        stdMemory_Free(pConfig->aSchemes);
        pConfig->aSchemes = NULL;
    }

    if ( pConfig->paDeleteSchemes )
    {
        stdMemory_Free(pConfig->paDeleteSchemes);
        pConfig->paDeleteSchemes = NULL;
    }

    pConfig->numSchemes      = 0;
    pConfig->numDeleteSchemes = 0;
    pConfig->selectedShemeIdx = -1;
    pConfig->maxNameLen       = 0;
}

JonesControlsScheme* J3DAPI jonesConfig_CloneControlSchemesList(JonesControlsScheme* aSchemes, size_t start, size_t numSchemes)
{
    if ( !aSchemes || !numSchemes || start >= numSchemes + start )
    {
        return NULL;
    }

    JonesControlsScheme* aNewSchemes = (JonesControlsScheme*)STDMALLOC(sizeof(JonesControlsScheme) * numSchemes);
    if ( !aNewSchemes )
    {
        return NULL;
    }

    memset(aNewSchemes, 0, sizeof(JonesControlsScheme) * numSchemes);

    for ( size_t i = start; i < numSchemes + start; ++i )
    {
        STD_STRCPY(aNewSchemes[i - start].aName, aSchemes[i].aName);
        for ( size_t j = 0; j < STD_ARRAYLEN(aSchemes[i].aActions); ++j )
        {
            for ( size_t k = 0; k <= JONESCONTROL_ACTION_GETNUMBINDS(aSchemes[i].aActions[j]); ++k )
            {
                aNewSchemes[i - start].aActions[j][k] = aSchemes[i].aActions[j][k];
            }
        }
    }

    return aNewSchemes;
}

int J3DAPI jonesConfig_CopyControlConfig(JonesControlsConfig* pSrc, JonesControlsConfig* pDst)
{
    if ( !pSrc || !pDst )
    {
        return 0;
    }

    JonesControlsScheme* apSchemes = jonesConfig_CloneControlSchemesList(pSrc->aSchemes, 0, pSrc->numSchemes);
    if ( pSrc->numSchemes && !apSchemes )
    {
        return 0;
    }

    if ( pDst->aSchemes )
    {
        jonesConfig_FreeControlConfigEntry(pDst);
    }

    pDst->numSchemes       = pSrc->numSchemes;
    pDst->selectedShemeIdx = pSrc->selectedShemeIdx;
    pDst->maxNameLen       = pSrc->maxNameLen;
    pDst->aSchemes         = apSchemes;
    pDst->paDeleteSchemes  = NULL;
    pDst->numDeleteSchemes = 0;
    return 1;
}

// --- Default schemes --------------------------------------------------------------------------

size_t J3DAPI JonesControlSchemes_GetNumDefaultSchemes(void)
{
    return JONESCONFIG_NUMDEFAULTCONTROLSCHEMES;
}

const char* J3DAPI JonesControlSchemes_GetDefaultSchemeName(size_t index)
{
    if ( index >= JONESCONFIG_NUMDEFAULTCONTROLSCHEMES )
    {
        return NULL;
    }

    return JonesControlSchemes_ResolveOr(jonesConfig_aDfltKeySetNames[index],
        jonesConfig_aDfltKeySetFallbackNames[index]);
}

int J3DAPI jonesConfig_SetDefaultControlScheme(JonesControlsScheme* pScheme, size_t num)
{
    if ( !pScheme || num >= JONESCONFIG_NUMDEFAULTCONTROLSCHEMES )
    {
        return 0;
    }

    const char* pDefaultKeySetName = JonesControlSchemes_GetDefaultSchemeName(num);
    if ( !pDefaultKeySetName )
    {
        return 0;
    }

    memset(pScheme, 0, sizeof(*pScheme));
    STD_STRCPY(pScheme->aName, pDefaultKeySetName);

    for ( size_t i = 0; i < JONESCONTROL_ACTION_NUMACTIONS; ++i )
    {
        size_t* pAction = pScheme->aActions[i];
        *pAction = 0;

        for ( size_t j = 1; j <= jonesConfig_aDfltKeySets[num][i].numBinds; ++j )
        {
            size_t controlId = jonesConfig_aDfltKeySets[num][i].aBindings[j - 1];
            pAction[j] = controlId;

            if ( JonesControlSchemes_IsKeyboardControl(controlId) )
            {
                JONESCONTROL_ACTION_UPDATENUMBINDINGS_KEYBOARD(pScheme->aActions[i], +1);
            }
            else
            {
                JONESCONTROL_ACTION_UPDATENUMBINDINGS_CONTROLLER(pScheme->aActions[i], +1);
            }
        }
    }

    return 1;
}

int J3DAPI JonesControlSchemes_SetDefaultScheme(JonesControlsScheme* pScheme, size_t index)
{
    return jonesConfig_SetDefaultControlScheme(pScheme, index);
}

// Added: a new install gets the Modern keyboard and gamepad scheme when the control style is
// Modern, and the Keyboard and gamepad scheme in Classic. A remembered active scheme stays as it
// is; a connected pad is always on, so the pad count no longer picks a default.
size_t J3DAPI JonesControlSchemes_GetDefaultSchemeIndex(void)
{
    return sithControl_IsModernControlStyle() ? 4u : 3u;
}

static size_t JonesControlSchemes_DefaultSchemeIndex(void)
{
    return JonesControlSchemes_GetDefaultSchemeIndex();
}

// --- Reading ----------------------------------------------------------------------------------

static int JonesControlSchemes_IsActionName(const char* pName, size_t* pAction)
{
    char aName[512] = { 0 };
    stdUtil_StringCopy(aName, STD_ARRAYLEN(aName), pName);
    stdUtil_ToUpper(aName);

    for ( size_t i = 0; i < JONESCONTROL_ACTION_NUMACTIONS; ++i )
    {
        const char* pValue = JonesControlSchemes_ResolveKey(jonesConfig_aJonesControlActionNames[i]);
        if ( pValue && strneqi(aName, pValue, strlen(pValue)) && strlen(aName) == strlen(pValue) )
        {
            *pAction = i;
            return 1;
        }
    }

    return 0;
}

static int JonesControlSchemes_AddParsedBinding(JonesControlsScheme* pConfig, size_t action, size_t controlId)
{
    size_t* pAction = pConfig->aActions[action];
    if ( JONESCONTROL_ACTION_GETNUMBINDS(pAction) >= JONESCONTROL_ACTION_MAXBINDS )
    {
        STDLOG_ERROR("Control scheme: action %zu has more than %zu bindings.\n", action, JONESCONTROL_ACTION_MAXBINDS);
        return 0;
    }

    size_t bindIndex = JONESCONTROL_ACTION_GETNUMBINDS(pAction) + 1;
    pAction[bindIndex] = controlId;

    if ( JonesControlSchemes_IsKeyboardControl(controlId) )
    {
        JONESCONTROL_ACTION_UPDATENUMBINDINGS_KEYBOARD(pAction, +1);
    }
    else
    {
        JONESCONTROL_ACTION_UPDATENUMBINDINGS_CONTROLLER(pAction, +1);
    }

    return 1;
}

int J3DAPI jonesConfig_LoadControlScheme(const char* pFilePath, JonesControlsScheme* pConfig)
{
    if ( !pFilePath || !pConfig )
    {
        return 0;
    }

    memset(pConfig, 0, sizeof(*pConfig));
    if ( !stdConffile_Open(pFilePath) )
    {
        return 0;
    }

    // The name is the file stem, not the full path the caller passed. Engine paths use
    // backslashes; the Babylon file system also accepts '/', so find either separator.
    const char* pName = strrchr(pFilePath, '\\');
    const char* pSlash = strrchr(pFilePath, '/');
    if ( pSlash && (!pName || pSlash > pName) )
    {
        pName = pSlash;
    }
    pName = ( pName ) ? pName + 1 : pFilePath;
    if ( !*pName )
    {
        pName = pFilePath;
    }

    stdUtil_StringCopy(pConfig->aName, STD_ARRAYLEN(pConfig->aName), pName);
    size_t nameLen = strlen(pConfig->aName);
    if ( nameLen > 4 && strneqi(pConfig->aName + nameLen - 4, JONESCONFIG_KFG_EXT, 4) )
    {
        pConfig->aName[nameLen - 4] = '\0';
    }

    int bValid = 0;
    int bSeenActions[JONESCONTROL_ACTION_NUMACTIONS] = { 0 };
    size_t numActions = 0;
    int bExtras = 0;

    if ( !stdConffile_ReadArgs() ) // heading
    {
        goto done;
    }

    if ( !stdConffile_ReadArgs() ) // enabled
    {
        goto done;
    }

    if ( stdConffile_g_entry.numArgs == 0 || !strneqi(stdConffile_g_entry.aArgs[0].argName, "1", 128u) )
    {
        STDLOG_ERROR("Control scheme '%s': not enabled.\n", pFilePath);
        goto done;
    }

    if ( !stdConffile_ReadArgs() ) // column header
    {
        goto done;
    }

    for ( size_t i = 0; i < JONESCONTROL_ACTION_NUMACTIONS; ++i )
    {
        if ( !stdConffile_ReadArgs() )
        {
            STDLOG_ERROR("Control scheme '%s': expected %zu actions, found %zu.\n",
                pFilePath, (size_t)JONESCONTROL_ACTION_NUMACTIONS, numActions);
            goto done;
        }

        size_t action = 0;
        if ( stdConffile_g_entry.numArgs == 0 || !JonesControlSchemes_IsActionName(stdConffile_g_entry.aArgs[0].argName, &action) )
        {
            STDLOG_ERROR("Control scheme '%s': unknown action '%s'.\n", pFilePath,
                stdConffile_g_entry.numArgs ? stdConffile_g_entry.aArgs[0].argName : "");
            goto done;
        }

        if ( bSeenActions[action] )
        {
            STDLOG_ERROR("Control scheme '%s': action %zu appears twice.\n", pFilePath, action);
            goto done;
        }
        bSeenActions[action] = 1;
        ++numActions;

        for ( size_t argNum = 1; argNum < stdConffile_g_entry.numArgs; ++argNum )
        {
            size_t controlId = 0;
            if ( !JonesControlSchemes_ParseControlName(stdConffile_g_entry.aArgs[argNum].argName, &controlId) )
            {
                STDLOG_ERROR("Control scheme '%s': unknown control '%s'.\n", pFilePath,
                    stdConffile_g_entry.aArgs[argNum].argName);
                goto done;
            }

            if ( !JonesControlSchemes_IsValidControl(controlId) )
            {
                STDLOG_ERROR("Control scheme '%s': control id %zu out of range.\n", pFilePath, controlId);
                goto done;
            }

            if ( !JonesControlSchemes_AddParsedBinding(pConfig, action, controlId) )
            {
                goto done;
            }
        }
    }

    if ( numActions != JONESCONTROL_ACTION_NUMACTIONS )
    {
        STDLOG_ERROR("Control scheme '%s': expected %zu actions, found %zu.\n",
            pFilePath, (size_t)JONESCONTROL_ACTION_NUMACTIONS, numActions);
        goto done;
    }

    // Added: the trailing section for bindings the original action lines cannot express. It comes
    // after all 37 action lines, so the original reader (whose loop ends there) never reads it.
    while ( stdConffile_ReadArgs() )
    {
        if ( stdConffile_g_entry.numArgs == 0 )
        {
            continue;
        }

        if ( !bExtras )
        {
            if ( strneqi(stdConffile_g_entry.aArgs[0].argName, "EXTRAS", 6u) )
            {
                bExtras = 1;
                continue;
            }

            STDLOG_WARNING("Control scheme '%s': ignoring trailing line '%s'.\n", pFilePath,
                stdConffile_g_entry.aArgs[0].argName);
            continue;
        }

        if ( stdConffile_g_entry.numArgs != 2 )
        {
            STDLOG_ERROR("Control scheme '%s': malformed EXTRAS line.\n", pFilePath);
            goto done;
        }

        size_t action = 0;
        if ( !JonesControlSchemes_IsActionName(stdConffile_g_entry.aArgs[0].argName, &action) )
        {
            STDLOG_ERROR("Control scheme '%s': unknown EXTRAS action '%s'.\n", pFilePath,
                stdConffile_g_entry.aArgs[0].argName);
            goto done;
        }

        size_t controlId = 0;
        if ( !JonesControlSchemes_ParseControlName(stdConffile_g_entry.aArgs[1].argName, &controlId) )
        {
            STDLOG_ERROR("Control scheme '%s': unknown EXTRAS control '%s'.\n", pFilePath,
                stdConffile_g_entry.aArgs[1].argName);
            goto done;
        }

        if ( !JonesControlSchemes_IsValidControl(controlId) )
        {
            STDLOG_ERROR("Control scheme '%s': EXTRAS control id %zu out of range.\n", pFilePath, controlId);
            goto done;
        }

        if ( !JonesControlSchemes_AddParsedBinding(pConfig, action, controlId) )
        {
            goto done;
        }
    }

    bValid = 1;

done:
    stdConffile_Close();
    if ( !bValid )
    {
        memset(pConfig, 0, sizeof(*pConfig));
    }

    return bValid;
}

int J3DAPI JonesControlSchemes_LoadScheme(const char* pFilePath, JonesControlsScheme* pConfig)
{
    return jonesConfig_LoadControlScheme(pFilePath, pConfig);
}

// --- Writing ----------------------------------------------------------------------------------

static int JonesControlSchemes_HasExtraBindings(const JonesControlsScheme* pScheme)
{
    for ( size_t i = 0; i < JONESCONTROL_ACTION_NUMACTIONS; ++i )
    {
        size_t count = JONESCONTROL_ACTION_GETNUMBINDS(pScheme->aActions[i]);
        for ( size_t j = 1; j <= count; ++j )
        {
            if ( JonesControlSchemes_IsExtraControl(pScheme->aActions[i][j]) )
            {
                return 1;
            }
        }
    }

    return 0;
}

static int JonesControlSchemes_WriteAll(const char* pText, size_t size)
{
    return size == 0 || stdConffile_Write(pText, size) == 0;
}

static int JonesControlSchemes_WriteString(const char* pText)
{
    return JonesControlSchemes_WriteAll(pText, pText ? strlen(pText) : 0);
}

static int JonesControlSchemes_WriteSpaces(size_t count)
{
    char aSpaces[256];
    memset(aSpaces, ' ', sizeof(aSpaces) - 1);
    aSpaces[sizeof(aSpaces) - 1] = '\0';

    while ( count )
    {
        size_t chunk = count < sizeof(aSpaces) - 1 ? count : sizeof(aSpaces) - 1;
        if ( !JonesControlSchemes_WriteAll(aSpaces, chunk) )
        {
            return 0;
        }
        count -= chunk;
    }

    return 1;
}

int J3DAPI jonesConfig_ControlOptions_WriteScheme(JonesControlsScheme* pScheme)
{
    if ( !pScheme )
    {
        return 0;
    }

    JonesControlSchemes_EnsureTables();

    char aDir[JONESCONFIG_KEYSDIRPATH_MAX] = { 0 };
    stdUtil_StringCopy(aDir, STD_ARRAYLEN(aDir), jonesConfig_aKeySetsDirPath);
    if ( !aDir[0] )
    {
        return 0;
    }

    // The original created the directory on first write. Ignore "already exists".
    stdFileUtil_MkDir(aDir);

    char aPath[JONESCONFIG_KEYSDIRPATH_MAX + 160] = { 0 };
    STD_FORMAT(aPath, "%s\\%s%s", aDir, pScheme->aName, JONESCONFIG_KFG_EXT);

    if ( !stdConffile_OpenWrite(aPath) )
    {
        STDLOG_ERROR("Control scheme: could not open '%s' for writing.\n", aPath);
        return 0;
    }

    int bOk = 1;

    // Heading, then the enabled marker, then the column header. The original opened the file in
    // text mode on Windows, so its line endings were CRLF; write them explicitly on every host.
    const char* pHeading = JonesControlSchemes_ResolveKey("JONES_STR_HEADING");
    bOk = bOk && JonesControlSchemes_WriteString(pHeading) && JonesControlSchemes_WriteString("\r\n");
    bOk = bOk && JonesControlSchemes_WriteString("1\r\n\r\n");

    char aColumnTitle[512] = { 0 };
    const char* pActionsTitle = JonesControlSchemes_ResolveKey("JONES_STR_ACTIONS");
    const char* pKeysTitle    = JonesControlSchemes_ResolveKey("JONES_STR_KEYS");
    JonesControlSchemes_FormatPadded(aColumnTitle, STD_ARRAYLEN(aColumnTitle), pActionsTitle,
        jonesConfig_maxActionPaddedStrLen);
    stdUtil_StringCat(aColumnTitle, STD_ARRAYLEN(aColumnTitle), pKeysTitle);
    bOk = bOk && JonesControlSchemes_WriteString(aColumnTitle) && JonesControlSchemes_WriteString("\r\n\r\n");

    for ( size_t i = 0; i < JONESCONTROL_ACTION_NUMACTIONS && bOk; ++i )
    {
        const char* pActionName = JonesControlSchemes_ResolveKey(jonesConfig_aJonesControlActionNames[i]);
        char aQuoted[512] = { 0 };
        STD_FORMAT(aQuoted, "\"%s\"", pActionName ? pActionName : "");
        bOk = bOk && JonesControlSchemes_WriteString(aQuoted);

        size_t actionPad = jonesConfig_maxActionPaddedStrLen > strlen(aQuoted)
            ? jonesConfig_maxActionPaddedStrLen - strlen(aQuoted) : 0;
        bOk = bOk && JonesControlSchemes_WriteSpaces(actionPad);

        size_t count = JONESCONTROL_ACTION_GETNUMBINDS(pScheme->aActions[i]);
        for ( size_t j = 1; j <= count && bOk; ++j )
        {
            size_t controlId = pScheme->aActions[i][j];
            if ( JonesControlSchemes_IsExtraControl(controlId) )
            {
                continue;
            }

            char aControl[JONESCONFIG_CONTROLSTRING_MAXLEN] = { 0 };
            jonesConfig_ControlToString(controlId, aControl);

            char aQuotedControl[JONESCONFIG_CONTROLSTRING_MAXLEN + 4] = { 0 };
            STD_FORMAT(aQuotedControl, "\"%s\"", aControl);
            bOk = bOk && JonesControlSchemes_WriteString(aQuotedControl);

            size_t controlPad = jonesConfig_maxControlPaddedStrLen > strlen(aQuotedControl)
                ? jonesConfig_maxControlPaddedStrLen - strlen(aQuotedControl) : 0;
            bOk = bOk && JonesControlSchemes_WriteSpaces(controlPad);
        }

        bOk = bOk && JonesControlSchemes_WriteString("\r\n");
    }

    if ( bOk && JonesControlSchemes_HasExtraBindings(pScheme) )
    {
        bOk = bOk && JonesControlSchemes_WriteString("EXTRAS\r\n");
        for ( size_t i = 0; i < JONESCONTROL_ACTION_NUMACTIONS && bOk; ++i )
        {
            const char* pActionName = JonesControlSchemes_ResolveKey(jonesConfig_aJonesControlActionNames[i]);
            size_t count = JONESCONTROL_ACTION_GETNUMBINDS(pScheme->aActions[i]);
            for ( size_t j = 1; j <= count && bOk; ++j )
            {
                size_t controlId = pScheme->aActions[i][j];
                if ( !JonesControlSchemes_IsExtraControl(controlId) )
                {
                    continue;
                }

                char aControl[JONESCONFIG_CONTROLSTRING_MAXLEN] = { 0 };
                jonesConfig_ControlToString(controlId, aControl);

                char aLine[JONESCONFIG_CONTROLSTRING_MAXLEN + 512] = { 0 };
                STD_FORMAT(aLine, "\"%s\" \"%s\"\r\n", pActionName ? pActionName : "", aControl);
                bOk = bOk && JonesControlSchemes_WriteString(aLine);
            }
        }
    }

    stdConffile_CloseWrite();
    if ( !bOk )
    {
        STDLOG_ERROR("Control scheme: failed writing '%s'.\n", aPath);
    }

    return bOk;
}

int J3DAPI JonesControlSchemes_SaveScheme(const JonesControlsScheme* pScheme)
{
    return jonesConfig_ControlOptions_WriteScheme((JonesControlsScheme*)pScheme);
}

// --- Active scheme and binding ----------------------------------------------------------------

static void JonesControlSchemes_MakeSchemePathInDir(const char* pDir, const char* pName,
    char* pDst, size_t dstSize)
{
    stdUtil_Format(pDst, dstSize, "%s\\%s%s", pDir, pName, JONESCONFIG_KFG_EXT);
}

static void JonesControlSchemes_MakeSchemePath(const char* pName, char* pDst, size_t dstSize)
{
    JonesControlSchemes_MakeSchemePathInDir(jonesConfig_aKeySetsDirPath, pName, pDst, dstSize);
}

JonesControlsScheme* J3DAPI jonesConfig_LoadActiveControlScheme(void)
{
    JonesControlSchemes_EnsureTables();

    JonesControlsScheme* pScheme = (JonesControlsScheme*)STDMALLOC(sizeof(JonesControlsScheme));
    if ( !pScheme )
    {
        return NULL;
    }
    memset(pScheme, 0, sizeof(*pScheme));

    char aFilename[128] = { 0 };
    stdConfig_GetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, aFilename, STD_ARRAYLEN(aFilename), "");

    if ( strlen(aFilename) )
    {
        char aFilePath[JONESCONFIG_KEYSDIRPATH_MAX + 160] = { 0 };
        JonesControlSchemes_MakeSchemePath(aFilename, aFilePath, STD_ARRAYLEN(aFilePath));

        if ( jonesConfig_LoadControlScheme(aFilePath, pScheme) )
        {
            return pScheme;
        }

        // Added: a shipped key set keeps loading from its original directory when the user actually
        // has not copied or re-saved it into the user directory.
        if ( jonesConfig_aShippedKeySetsDirPath[0] )
        {
            JonesControlSchemes_MakeSchemePathInDir(jonesConfig_aShippedKeySetsDirPath, aFilename,
                aFilePath, STD_ARRAYLEN(aFilePath));
            if ( jonesConfig_LoadControlScheme(aFilePath, pScheme) )
            {
                return pScheme;
            }
        }

        // A scheme name that matches a built-in default is re-created from the built-in data.
        for ( size_t i = 0; i < JONESCONFIG_NUMDEFAULTCONTROLSCHEMES; ++i )
        {
            const char* pDefaultName = JonesControlSchemes_GetDefaultSchemeName(i);
            if ( pDefaultName && streqi(aFilename, pDefaultName) )
            {
                if ( jonesConfig_SetDefaultControlScheme(pScheme, i) )
                {
                    return pScheme;
                }
            }
        }

        STDLOG_WARNING("Failed to load control scheme '%s', reverting to default.\n", aFilePath);
        stdConfig_SetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, "");
    }

    size_t schemeIndex = JonesControlSchemes_DefaultSchemeIndex();
    const char* pDefaultName = JonesControlSchemes_GetDefaultSchemeName(schemeIndex);
    if ( pDefaultName && jonesConfig_SetDefaultControlScheme(pScheme, schemeIndex) )
    {
        stdConfig_SetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, pDefaultName);
        return pScheme;
    }

    jonesConfig_FreeControlScheme(pScheme);
    return NULL;
}

JonesControlsScheme* J3DAPI JonesControlSchemes_LoadActiveScheme(void)
{
    return jonesConfig_LoadActiveControlScheme();
}

int J3DAPI JonesControlSchemes_SetActiveScheme(const JonesControlsScheme* pScheme)
{
    if ( !pScheme )
    {
        return 0;
    }

    if ( !stdConfig_SetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, pScheme->aName) )
    {
        return 0;
    }

    if ( !jonesConfig_pActiveScheme )
    {
        jonesConfig_pActiveScheme = (JonesControlsScheme*)STDMALLOC(sizeof(JonesControlsScheme));
        if ( !jonesConfig_pActiveScheme )
        {
            return 0;
        }
    }

    memcpy(jonesConfig_pActiveScheme, pScheme, sizeof(*pScheme));
    jonesConfig_BindControls(jonesConfig_pActiveScheme);
    return 1;
}

const JonesControlsScheme* J3DAPI JonesControlSchemes_GetActiveScheme(void)
{
    return jonesConfig_pActiveScheme;
}

const char* J3DAPI JonesControlSchemes_GetDirectory(void)
{
    return jonesConfig_aKeySetsDirPath;
}

void J3DAPI jonesConfig_EnableMouseControl(int bEnable)
{
    if ( bEnable )
    {
        sithControl_RebindMouse();
    }
    else
    {
        JonesDisplaySettings* pSettings = JonesMain_GetDisplaySettings();
        if ( pSettings && pSettings->bWindowMode )
        {
            stdControl_EnableMouse(0);
        }
        else
        {
            sithControl_UnbindMouseAxes();
            stdControl_EnableMouse(1);
        }
    }
}

void J3DAPI jonesConfig_BindJoystickControl(SithControlFunction functionId, size_t controlId)
{
    int axis = -1;
    int flags = 0;

    switch ( controlId )
    {
        case JONESCONFIG_JOYAXISKID_UP:
            axis  = STDCONTROL_JOYSTICK_AXIS_Y;
            flags = STDCONTROL_AID_NEGATIVE_AXIS;
            break;
        case JONESCONFIG_JOYAXISKID_RIGHT:
            axis  = STDCONTROL_JOYSTICK_AXIS_X;
            flags = STDCONTROL_AID_POSITIVE_AXIS;
            break;
        case JONESCONFIG_JOYAXISKID_DOWN:
            axis  = STDCONTROL_JOYSTICK_AXIS_Y;
            flags = STDCONTROL_AID_POSITIVE_AXIS;
            break;
        case JONESCONFIG_JOYAXISKID_LEFT:
            axis  = STDCONTROL_JOYSTICK_AXIS_X;
            flags = STDCONTROL_AID_NEGATIVE_AXIS;
            break;
        case JONESCONFIG_JOYAXISKID_TWISTL:
            axis  = STDCONTROL_JOYSTICK_AXIS_RZ;
            flags = STDCONTROL_AID_NEGATIVE_AXIS;
            break;
        case JONESCONFIG_JOYAXISKID_TWISTR:
            axis  = STDCONTROL_JOYSTICK_AXIS_RZ;
            flags = STDCONTROL_AID_POSITIVE_AXIS;
            break;
        case JONESCONFIG_JOYAXISKID_AIR:
            axis  = STDCONTROL_JOYSTICK_AXIS_Z;
            flags = STDCONTROL_AID_NEGATIVE_AXIS;
            break;
        case JONESCONFIG_JOYAXISKID_GROUND:
            axis  = STDCONTROL_JOYSTICK_AXIS_Z;
            flags = STDCONTROL_AID_POSITIVE_AXIS;
            break;
        // Added: the right-stick axes the original scheme format cannot name.
        case JONESCONTROLSCHEMES_JOYAXIS_RX_NEG:
            axis  = STDCONTROL_JOYSTICK_AXIS_RX;
            flags = STDCONTROL_AID_NEGATIVE_AXIS;
            break;
        case JONESCONTROLSCHEMES_JOYAXIS_RX_POS:
            axis  = STDCONTROL_JOYSTICK_AXIS_RX;
            flags = STDCONTROL_AID_POSITIVE_AXIS;
            break;
        case JONESCONTROLSCHEMES_JOYAXIS_RY_NEG:
            axis  = STDCONTROL_JOYSTICK_AXIS_RY;
            flags = STDCONTROL_AID_NEGATIVE_AXIS;
            break;
        case JONESCONTROLSCHEMES_JOYAXIS_RY_POS:
            axis  = STDCONTROL_JOYSTICK_AXIS_RY;
            flags = STDCONTROL_AID_POSITIVE_AXIS;
            break;
        default:
            axis = -1;
            break;
    }

    // Added: the original let mouse buttons fall into the joystick loop and bound button 0.
    if ( JonesControlSchemes_IsMouseButton(controlId) )
    {
        sithControl_BindControl(functionId, controlId, (SithControlBindFlag)0);
        return;
    }

    const size_t numJoysticks = stdControl_GetNumJoysticks();
    if ( axis == -1 )
    {
        for ( size_t joyNum = 0; joyNum < numJoysticks; ++joyNum )
        {
            sithControl_BindControl(functionId,
                STDCONTROL_JOYSTICK_GETBUTTON(joyNum, STDCONTROL_JOYSTICK_GETBUTTONINDEX(controlId)),
                (SithControlBindFlag)0);
        }
    }
    else
    {
        for ( size_t joyNum = 0; joyNum < numJoysticks; ++joyNum )
        {
            sithControl_BindAxis(functionId, STDCONTROL_GET_JOYSTICK_AXIS(joyNum, axis) | flags,
                (SithControlBindFlag)0);
            if ( functionId == SITHCONTROL_FORWARD && !stdControl_IsGamePad(joyNum) )
            {
                flags |= STDCONTROL_AID_LOW_SENSITIVITY;
                functionId = SITHCONTROL_RUNFWD;
                sithControl_BindAxis(SITHCONTROL_RUNFWD,
                    STDCONTROL_GET_JOYSTICK_AXIS(joyNum, axis) | flags, (SithControlBindFlag)0);
            }
        }
    }
}

void J3DAPI jonesConfig_BindControls(JonesControlsScheme* pConfig)
{
    if ( !pConfig )
    {
        return;
    }

    int bMouse = stdConfig_GetBool(JONESCONTROL_CFG_CONTROLS_MOUSE, false);

    // Added: in Modern the scheme's step rows become quick-step modifiers, so the old held
    // sidestep bindings never survive a rebind. The two rows share SITHCONTROL_QUICKSTEP, so it
    // is unbound once here instead of before each row.
    bool bModern = sithControl_IsModernControlStyle();
    if ( bModern )
    {
        sithControl_UnbindFunction(SITHCONTROL_STPLEFT);
        sithControl_UnbindFunction(SITHCONTROL_STPRIGHT);
    }
    sithControl_UnbindFunction(SITHCONTROL_QUICKSTEP);

    // Altered: enable/disable the mouse before the action loop, not after it. sithControl_RebindMouse
    // removes every mouse-button binding, so the original order dropped a scheme's mouse bindings.
    jonesConfig_EnableMouseControl(bMouse);

    JonesMain_BindToggleMenuControlKeys(&pConfig->aActions[JONESCONTROL_ACTION_INTERFACE][1],
        JONESCONTROL_ACTION_GETNUMBINDS(pConfig->aActions[JONESCONTROL_ACTION_INTERFACE]));

    JonesHud_BindActivateControlKeys(&pConfig->aActions[JONESCONTROL_ACTION_ACTIVATE][1],
        JONESCONTROL_ACTION_GETNUMBINDS(pConfig->aActions[JONESCONTROL_ACTION_ACTIVATE]));

    for ( SithControlFunction fctnID = SITHCONTROL_WSELECT0; fctnID <= SITHCONTROL_WSELECT16; ++fctnID )
    {
        sithControl_UnbindFunction(fctnID);
    }

    for ( JonesControlActions actionId = JONESCONTROL_ACTION_WALKFORWARD; actionId < JONESCONTROL_ACTION_NUMACTIONS; ++actionId )
    {
        SithControlFunction functionId = (SithControlFunction)-1;

        if ( actionId == JONESCONTROL_ACTION_ACTIVATE || actionId == JONESCONTROL_ACTION_TURNLEFT || actionId == JONESCONTROL_ACTION_TURNRIGHT )
        {
            size_t curBindNum = 1;
            size_t numActionBinds = 3;
            if ( actionId <= JONESCONTROL_ACTION_TURNRIGHT )
            {
                numActionBinds = 2;
            }

            while ( curBindNum <= numActionBinds )
            {
                if ( actionId == JONESCONTROL_ACTION_TURNLEFT )
                {
                    functionId = curBindNum == 1 ? SITHCONTROL_LEFT : SITHCONTROL_TURNLEFT;
                }
                else if ( actionId == JONESCONTROL_ACTION_TURNRIGHT )
                {
                    functionId = curBindNum == 1 ? SITHCONTROL_RIGHT : SITHCONTROL_TURNRIGHT;
                }
                else
                {
                    switch ( curBindNum )
                    {
                        case 1:
                            functionId = SITHCONTROL_ACT2;
                            break;
                        case 2:
                            functionId = SITHCONTROL_FIRE1;
                            break;
                        case 3:
                            functionId = SITHCONTROL_ACTIVATE;
                            break;
                        default:
                            break;
                    }
                }

                if ( functionId != (SithControlFunction)-1 )
                {
                    sithControl_UnbindFunction(functionId);

                    for ( size_t i = 1; i <= JONESCONTROL_ACTION_GETNUMBINDS(pConfig->aActions[actionId]); ++i )
                    {
                        size_t controlId = pConfig->aActions[actionId][i];
                        if ( JonesControlSchemes_IsKeyboardControl(controlId) )
                        {
                            sithControl_BindControl(functionId, controlId, (SithControlBindFlag)0);
                        }
                        else if ( JonesControlSchemes_IsMouseButton(controlId) )
                        {
                            if ( bMouse )
                            {
                                sithControl_BindControl(functionId, controlId, (SithControlBindFlag)0);
                            }
                        }
                        else
                        {
                            jonesConfig_BindJoystickControl(functionId, controlId);
                        }
                    }
                }

                ++curBindNum;
            }
        }
        else if ( actionId >= JONESCONTROL_ACTION_FISTS && actionId != JONESCONTROL_ACTION_MAP && actionId != JONESCONTROL_ACTION_INTERFACE )
        {
            for ( size_t i = 1; i <= JONESCONTROL_ACTION_GETNUMBINDS(pConfig->aActions[actionId]); ++i )
            {
                switch ( actionId )
                {
                    case JONESCONTROL_ACTION_FISTS:        functionId = SITHCONTROL_WSELECT0; break;
                    case JONESCONTROL_ACTION_WHIP:         functionId = SITHCONTROL_WSELECT1; break;
                    case JONESCONTROL_ACTION_MAUSER:       functionId = SITHCONTROL_WSELECT4; break;
                    case JONESCONTROL_ACTION_PPSH41:       functionId = SITHCONTROL_WSELECT7; break;
                    case JONESCONTROL_ACTION_PISTOL:       functionId = SITHCONTROL_WSELECT2; break;
                    case JONESCONTROL_ACTION_SIMONOV:      functionId = SITHCONTROL_WSELECT5; break;
                    case JONESCONTROL_ACTION_TOKEREV:      functionId = SITHCONTROL_WSELECT3; break;
                    case JONESCONTROL_ACTION_TOZ34:        functionId = SITHCONTROL_WSELECT8; break;
                    case JONESCONTROL_ACTION_BAZOOKA:      functionId = SITHCONTROL_WSELECT9; break;
                    case JONESCONTROL_ACTION_MACHETE:      functionId = SITHCONTROL_WSELECT6; break;
                    case JONESCONTROL_ACTION_SATCHEL:      functionId = SITHCONTROL_WSELECT11; break;
                    case JONESCONTROL_ACTION_GRENADE:      functionId = SITHCONTROL_WSELECT10; break;
                    case JONESCONTROL_ACTION_HEALTH:       functionId = SITHCONTROL_HEALTH; break;
                    case JONESCONTROL_ACTION_IMP1:         functionId = SITHCONTROL_WSELECT12; break;
                    case JONESCONTROL_ACTION_IMP2:         functionId = SITHCONTROL_WSELECT13; break;
                    case JONESCONTROL_ACTION_IMP3:         functionId = SITHCONTROL_WSELECT14; break;
                    case JONESCONTROL_ACTION_IMP4:         functionId = SITHCONTROL_WSELECT15; break;
                    case JONESCONTROL_ACTION_IMP5:         functionId = SITHCONTROL_WSELECT16; break;
                    case JONESCONTROL_ACTION_CHALK:        functionId = SITHCONTROL_CHALK; break;
                    default:                               functionId = (SithControlFunction)-1; break;
                }

                if ( functionId == (SithControlFunction)-1 )
                {
                    continue;
                }

                size_t controlId = pConfig->aActions[actionId][i];
                if ( JonesControlSchemes_IsKeyboardControl(controlId) )
                {
                    sithControl_BindControl(functionId, controlId, (SithControlBindFlag)0);
                }
                else if ( JonesControlSchemes_IsMouseButton(controlId) )
                {
                    if ( bMouse )
                    {
                        sithControl_BindControl(functionId, controlId, (SithControlBindFlag)0);
                    }
                }
                else
                {
                    jonesConfig_BindJoystickControl(functionId, controlId);
                }
            }
        }
        else
        {
            switch ( actionId )
            {
                case JONESCONTROL_ACTION_WALKFORWARD:  functionId = SITHCONTROL_FORWARD; break;
                case JONESCONTROL_ACTION_WALKBACK:     functionId = SITHCONTROL_BACK; break;
                case JONESCONTROL_ACTION_STEPLEFT:     functionId = SITHCONTROL_STPLEFT; break;
                case JONESCONTROL_ACTION_STEPRIGHT:    functionId = SITHCONTROL_STPRIGHT; break;
                case JONESCONTROL_ACTION_CRAWL:        functionId = SITHCONTROL_CRAWLTOGGLE; break;
                case JONESCONTROL_ACTION_RUN:          functionId = SITHCONTROL_ACT1; break;
                case JONESCONTROL_ACTION_ROLL:         functionId = SITHCONTROL_ACT3; break;
                case JONESCONTROL_ACTION_JUMP:         functionId = SITHCONTROL_JUMP; break;
                case JONESCONTROL_ACTION_LOOK:         functionId = SITHCONTROL_LOOK; break;
                case JONESCONTROL_ACTION_TOGGLEWEAPON: functionId = SITHCONTROL_WEAPONTOGGLE; break;
                case JONESCONTROL_ACTION_PREVWEAPON:   functionId = SITHCONTROL_PREVWEAPON; break;
                case JONESCONTROL_ACTION_NEXTWEAPON:   functionId = SITHCONTROL_NEXTWEAPON; break;
                case JONESCONTROL_ACTION_TOGGLELIGHT:  functionId = SITHCONTROL_LIGHTERTOGGLE; break;
                case JONESCONTROL_ACTION_MAP:          functionId = SITHCONTROL_MAP; break;
                default:                               functionId = (SithControlFunction)-1; break;
            }

            if ( functionId != (SithControlFunction)-1 )
            {
                // Added: Modern turns both scheme step rows into quick-step modifiers. The
                // physical controls keep their row, but a held direction now combines with the
                // modifier instead of stepping on a held key. Classic keeps the original rows.
                bool bQuickStep = bModern
                    && (functionId == SITHCONTROL_STPLEFT || functionId == SITHCONTROL_STPRIGHT);
                SithControlFunction bindFunction = bQuickStep ? SITHCONTROL_QUICKSTEP : functionId;

                if ( !bQuickStep )
                {
                    sithControl_UnbindFunction(functionId);
                    if ( functionId == SITHCONTROL_FORWARD )
                    {
                        sithControl_UnbindFunction(SITHCONTROL_RUNFWD);
                    }
                }

                for ( size_t i = 1; i <= JONESCONTROL_ACTION_GETNUMBINDS(pConfig->aActions[actionId]); ++i )
                {
                    size_t controlId = pConfig->aActions[actionId][i];

                    // Added: in Modern the left trigger locks a target; Run comes from the stick
                    // magnitude, so the scheme's LT-as-Run binding becomes TARGETLOCK at bind time.
                    // Classic keeps the same scheme data and LT stays Run.
                    SithControlFunction bindFunctionId = bindFunction;
                    if ( sithControl_IsModernControlStyle()
                        && bindFunctionId == SITHCONTROL_ACT1
                        && controlId == JONESCONFIG_JOYAXISKID_GROUND )
                    {
                        bindFunctionId = SITHCONTROL_TARGETLOCK;
                    }

                    // Added: the owner reserves LB for a weapon wheel, so in Modern a step-left row's
                    // gamepad controls are not bound; RB (the step-right row) is the quick-step modifier.
                    if ( bQuickStep && functionId == SITHCONTROL_STPLEFT
                        && !JonesControlSchemes_IsKeyboardControl(controlId)
                        && !JonesControlSchemes_IsMouseButton(controlId) )
                    {
                        continue;
                    }

                    if ( JonesControlSchemes_IsKeyboardControl(controlId) )
                    {
                        sithControl_BindControl(bindFunctionId, controlId, (SithControlBindFlag)0);
                    }
                    else if ( JonesControlSchemes_IsMouseButton(controlId) )
                    {
                        if ( bMouse )
                        {
                            sithControl_BindControl(bindFunctionId, controlId, (SithControlBindFlag)0);
                        }
                    }
                    else
                    {
                        jonesConfig_BindJoystickControl(bindFunctionId, controlId);
                    }
                }
            }
        }
    }
}

// --- Listing, creating and deleting schemes ---------------------------------------------------

int J3DAPI jonesConfig_ControlOptions_SetAllDefaultControlSchemes(JonesControlsConfig* pConfig)
{
    if ( !pConfig )
    {
        return 0;
    }

    for ( size_t i = 0; i < JONESCONFIG_NUMDEFAULTCONTROLSCHEMES; ++i )
    {
        if ( (pConfig->numSchemes % 10) == 0 )
        {
            JonesControlsScheme* pNew = (JonesControlsScheme*)STDREALLOC(pConfig->aSchemes,
                sizeof(JonesControlsScheme) * (pConfig->numSchemes + 10));
            if ( !pNew )
            {
                return 0;
            }
            pConfig->aSchemes = pNew;
            memset(&pConfig->aSchemes[pConfig->numSchemes], 0,
                sizeof(JonesControlsScheme) * (pConfig->numSchemes + 10 - pConfig->numSchemes));
        }

        if ( !jonesConfig_SetDefaultControlScheme(&pConfig->aSchemes[pConfig->numSchemes], i) )
        {
            return 0;
        }

        ++pConfig->numSchemes;
    }

    return 1;
}

static void JonesControlSchemes_LoadSchemesFromDirectory(JonesControlsConfig* pConfig,
    const char* pDirectory, int bSkipExisting)
{
    if ( !pDirectory[0] )
    {
        return;
    }

    FindFileData* pFind = stdFileUtil_NewFind(pDirectory, 3, "kfg");
    if ( !pFind )
    {
        return;
    }

    tFoundFileInfo fileInfo;
    while ( stdFileUtil_FindNext(pFind, &fileInfo) )
    {
        if ( fileInfo.bIsDirectory )
        {
            continue;
        }

        size_t nameLen = strlen(fileInfo.aName);
        if ( nameLen < 4 || !strneqi(fileInfo.aName + nameLen - 4, JONESCONFIG_KFG_EXT, 4) )
        {
            continue;
        }

        char aStem[128] = { 0 };
        const size_t stemLen = nameLen - 4u;
        if ( stemLen >= sizeof(aStem) )
        {
            continue;
        }

        memcpy(aStem, fileInfo.aName, stemLen);
        aStem[stemLen] = '\0';

        // Added: a user file wins over the shipped file of the same name.
        int bExists = 0;
        if ( bSkipExisting )
        {
            for ( size_t i = 0; i < pConfig->numSchemes; ++i )
            {
                if ( streqi(pConfig->aSchemes[i].aName, aStem) )
                {
                    bExists = 1;
                    break;
                }
            }
        }

        if ( bExists )
        {
            continue;
        }

        if ( (pConfig->numSchemes % 10) == 0 )
        {
            JonesControlsScheme* pNew = (JonesControlsScheme*)STDREALLOC(pConfig->aSchemes,
                sizeof(JonesControlsScheme) * (pConfig->numSchemes + 10));
            if ( !pNew )
            {
                break;
            }
            pConfig->aSchemes = pNew;
            memset(&pConfig->aSchemes[pConfig->numSchemes], 0,
                sizeof(JonesControlsScheme) * (pConfig->numSchemes + 10 - pConfig->numSchemes));
        }

        char aPath[JONESCONFIG_KEYSDIRPATH_MAX + 160] = { 0 };
        STD_FORMAT(aPath, "%s\\%s", pDirectory, fileInfo.aName);
        if ( jonesConfig_LoadControlScheme(aPath, &pConfig->aSchemes[pConfig->numSchemes]) )
        {
            ++pConfig->numSchemes;
        }
        else
        {
            memset(&pConfig->aSchemes[pConfig->numSchemes], 0, sizeof(JonesControlsScheme));
        }
    }

    stdFileUtil_DisposeFind(pFind);
}

int J3DAPI jonesConfig_ControlOptions_LoadAllControlSchemes(JonesControlsConfig* pConfig)
{
    if ( !pConfig )
    {
        return 0;
    }

    JonesControlSchemes_EnsureTables();

    // Added: user key sets first, then the shipped ones they do not shadow.
    JonesControlSchemes_LoadSchemesFromDirectory(pConfig, jonesConfig_aKeySetsDirPath, 0);
    JonesControlSchemes_LoadSchemesFromDirectory(pConfig, jonesConfig_aShippedKeySetsDirPath, 1);
    return 1;
}

int J3DAPI JonesControlSchemes_LoadAllSchemes(JonesControlsConfig* pConfig)
{
    if ( !pConfig )
    {
        return 0;
    }

    memset(pConfig, 0, sizeof(*pConfig));
    pConfig->selectedShemeIdx = -1;

    if ( !jonesConfig_ControlOptions_SetAllDefaultControlSchemes(pConfig)
        || !jonesConfig_ControlOptions_LoadAllControlSchemes(pConfig) )
    {
        jonesConfig_FreeControlConfigEntry(pConfig);
        return 0;
    }

    return 1;
}

JonesControlsScheme* J3DAPI jonesConfig_GetDefaultControlScheme(JonesControlsConfig* pConfig)
{
    if ( !pConfig )
    {
        return NULL;
    }

    size_t schemeIndex = JonesControlSchemes_DefaultSchemeIndex();
    const char* pSchemeName = JonesControlSchemes_GetDefaultSchemeName(schemeIndex);
    if ( !pSchemeName )
    {
        return NULL;
    }

    for ( size_t i = 0; i < pConfig->numSchemes; ++i )
    {
        if ( streqi(pConfig->aSchemes[i].aName, pSchemeName) )
        {
            return &pConfig->aSchemes[i];
        }
    }

    return NULL;
}

int J3DAPI JonesControlSchemes_CreateScheme(const char* pName, JonesControlsScheme* pOut)
{
    if ( !pName || !pName[0] || !pOut )
    {
        return 0;
    }

    if ( !jonesConfig_SetDefaultControlScheme(pOut, 0) )
    {
        return 0;
    }

    STD_STRCPY(pOut->aName, pName);
    return 1;
}

int J3DAPI JonesControlSchemes_DeleteScheme(const char* pName)
{
    if ( !pName || !pName[0] )
    {
        return 0;
    }

    JonesControlSchemes_EnsureTables();

    char aPath[JONESCONFIG_KEYSDIRPATH_MAX + 160] = { 0 };
    JonesControlSchemes_MakeSchemePath(pName, aPath, STD_ARRAYLEN(aPath));
    stdFileUtil_DelFile(aPath);

    char aActive[128] = { 0 };
    stdConfig_GetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, aActive, STD_ARRAYLEN(aActive), "");
    if ( streqi(aActive, pName) )
    {
        stdConfig_SetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, "");
    }

    return 1;
}

// --- Startup and active binding ---------------------------------------------------------------

int jonesConfig_Startup(void)
{
    if ( jonesConfig_bStartup )
    {
        return 1;
    }

    // The headless runner keeps the engine's default bindings, so it touches neither the scheme
    // files nor sithControl. This is the same game-window test the M7.10 screens use.
    if ( !JonesBabylon_HasWindow() )
    {
        jonesConfig_bStartup = 1;
        return 1;
    }

    if ( !jonesConfig_InitKeySetsPath() )
    {
        return 0;
    }

    if ( !jonesConfig_InitKeyActions() )
    {
        return 0;
    }

    char aConfigured[128] = { 0 };
    stdConfig_GetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, aConfigured, STD_ARRAYLEN(aConfigured), "");

    if ( !aConfigured[0] )
    {
        // Added: on the first run there is no active scheme yet. Choosing the default waits for
        // JonesControlSchemes_BindActive after sithControl and stdControl have started, because the
        // binding calls need the registered control functions.
        jonesConfig_bPendingDefaultScheme = 1;
    }
    else
    {
        JonesControlsScheme* pScheme = jonesConfig_LoadActiveControlScheme();
        if ( pScheme )
        {
            // The original bound here; sithControl has not registered its functions yet, so the
            // calls are recorded into the scheme and applied again by JonesControlSchemes_BindActive.
            jonesConfig_BindControls(pScheme);

            if ( !jonesConfig_pActiveScheme )
            {
                jonesConfig_pActiveScheme = (JonesControlsScheme*)STDMALLOC(sizeof(JonesControlsScheme));
            }

            if ( jonesConfig_pActiveScheme )
            {
                memcpy(jonesConfig_pActiveScheme, pScheme, sizeof(*pScheme));
            }

            jonesConfig_FreeControlScheme(pScheme);
        }
    }

    jonesConfig_bStartup = 1;
    return 1;
}

void jonesConfig_Shutdown(void)
{
    if ( jonesConfig_pActiveScheme )
    {
        memset(jonesConfig_pActiveScheme, 0, sizeof(*jonesConfig_pActiveScheme));
        stdMemory_Free(jonesConfig_pActiveScheme);
        jonesConfig_pActiveScheme = NULL;
    }

    memset(jonesConfig_aKeySetsDirPath, 0, sizeof(jonesConfig_aKeySetsDirPath));
    memset(jonesConfig_aShippedKeySetsDirPath, 0, sizeof(jonesConfig_aShippedKeySetsDirPath));
    jonesConfig_bKeyActionsInited = 0;
    jonesConfig_bStartup = 0;
    jonesConfig_bPendingDefaultScheme = 0;
}

void J3DAPI jonesConfig_BindActiveControlScheme(void)
{
    // Added: the column-neutral entry point JonesControl_Startup calls (jonesConfig.h).
    JonesControlSchemes_BindActive();
}

int J3DAPI jonesConfig_HasActiveControlScheme(void)
{
    // Added: the column-neutral query JonesMain_ProcessMenu uses for its fallback keys.
    return jonesConfig_pActiveScheme != NULL;
}

void J3DAPI JonesControlSchemes_BindActive(void)
{
    if ( !JonesBabylon_HasWindow() )
    {
        return;
    }

    if ( jonesConfig_bPendingDefaultScheme )
    {
        // First run: choose the default now that sithControl has registered its functions.
        size_t defaultIndex = JonesControlSchemes_DefaultSchemeIndex();
        JonesControlsScheme scheme;
        if ( jonesConfig_SetDefaultControlScheme(&scheme, defaultIndex) )
        {
            stdConfig_SetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, scheme.aName);

            if ( !jonesConfig_pActiveScheme )
            {
                jonesConfig_pActiveScheme = (JonesControlsScheme*)STDMALLOC(sizeof(JonesControlsScheme));
            }

            if ( jonesConfig_pActiveScheme )
            {
                memcpy(jonesConfig_pActiveScheme, &scheme, sizeof(scheme));
            }
        }

        jonesConfig_bPendingDefaultScheme = 0;
    }

    if ( jonesConfig_pActiveScheme )
    {
        jonesConfig_BindControls(jonesConfig_pActiveScheme);
    }
}

// --- Controls screen accessors ----------------------------------------------------------------

const char* J3DAPI JonesControlSchemes_GetActionName(JonesControlActions action)
{
    if ( action < 0 || action >= JONESCONTROL_ACTION_NUMACTIONS )
    {
        return NULL;
    }

    return JonesControlSchemes_ResolveKey(jonesConfig_aJonesControlActionNames[action]);
}

const char* J3DAPI JonesControlSchemes_GetCapActionName(JonesControlActions action)
{
    if ( action < 0 || action >= JONESCONTROL_ACTION_NUMACTIONS )
    {
        return NULL;
    }

    return JonesControlSchemes_ResolveKey(jonesConfig_aJonesCapControlActionNames[action]);
}

size_t J3DAPI JonesControlSchemes_GetNumBindings(const JonesControlsScheme* pScheme, JonesControlActions action)
{
    if ( !pScheme || action < 0 || action >= JONESCONTROL_ACTION_NUMACTIONS )
    {
        return 0;
    }

    return JONESCONTROL_ACTION_GETNUMBINDS(pScheme->aActions[action]);
}

int J3DAPI JonesControlSchemes_GetBinding(const JonesControlsScheme* pScheme, JonesControlActions action, size_t index, size_t* pControlId)
{
    if ( !pScheme || !pControlId || action < 0 || action >= JONESCONTROL_ACTION_NUMACTIONS )
    {
        return 0;
    }

    size_t count = JONESCONTROL_ACTION_GETNUMBINDS(pScheme->aActions[action]);
    if ( index >= count )
    {
        return 0;
    }

    *pControlId = pScheme->aActions[action][index + 1];
    return 1;
}

int J3DAPI JonesControlSchemes_AddBinding(JonesControlsScheme* pScheme, JonesControlActions action, size_t controlId)
{
    if ( !pScheme || action < 0 || action >= JONESCONTROL_ACTION_NUMACTIONS )
    {
        return 0;
    }

    if ( !JonesControlSchemes_IsValidControl(controlId) )
    {
        return 0;
    }

    size_t* pAction = pScheme->aActions[action];
    if ( JONESCONTROL_ACTION_HASMAXBINDS(pAction) )
    {
        return 0;
    }

    pAction[JONESCONTROL_ACTION_GETNUMBINDS(pAction) + 1] = controlId;
    if ( JonesControlSchemes_IsKeyboardControl(controlId) )
    {
        JONESCONTROL_ACTION_UPDATENUMBINDINGS_KEYBOARD(pAction, +1);
    }
    else
    {
        JONESCONTROL_ACTION_UPDATENUMBINDINGS_CONTROLLER(pAction, +1);
    }

    pScheme->bModified = 1;
    return 1;
}

int J3DAPI JonesControlSchemes_RemoveBinding(JonesControlsScheme* pScheme, JonesControlActions action, size_t index)
{
    if ( !pScheme || action < 0 || action >= JONESCONTROL_ACTION_NUMACTIONS )
    {
        return 0;
    }

    size_t* pAction = pScheme->aActions[action];
    size_t count = JONESCONTROL_ACTION_GETNUMBINDS(pAction);
    if ( index >= count )
    {
        return 0;
    }

    int bKeyboard = JonesControlSchemes_IsKeyboardControl(pAction[index + 1]);
    for ( size_t i = index + 1; i < count; ++i )
    {
        pAction[i] = pAction[i + 1];
    }
    pAction[count] = 0;

    if ( bKeyboard )
    {
        JONESCONTROL_ACTION_UPDATENUMBINDINGS_KEYBOARD(pAction, -1);
    }
    else
    {
        JONESCONTROL_ACTION_UPDATENUMBINDINGS_CONTROLLER(pAction, -1);
    }

    pScheme->bModified = 1;
    return 1;
}

int J3DAPI JonesControlSchemes_SetBinding(JonesControlsScheme* pScheme, JonesControlActions action, size_t index, size_t controlId)
{
    if ( !pScheme || action < 0 || action >= JONESCONTROL_ACTION_NUMACTIONS
        || !JonesControlSchemes_IsValidControl(controlId) )
    {
        return 0;
    }

    size_t* pAction = pScheme->aActions[action];
    size_t count = JONESCONTROL_ACTION_GETNUMBINDS(pAction);
    if ( index >= count )
    {
        return 0;
    }

    size_t oldControlId = pAction[index + 1];
    int oldKeyboard = JonesControlSchemes_IsKeyboardControl(oldControlId);
    int newKeyboard = JonesControlSchemes_IsKeyboardControl(controlId);
    if ( oldKeyboard != newKeyboard )
    {
        if ( oldKeyboard )
        {
            JONESCONTROL_ACTION_UPDATENUMBINDINGS_KEYBOARD(pAction, -1);
            JONESCONTROL_ACTION_UPDATENUMBINDINGS_CONTROLLER(pAction, +1);
        }
        else
        {
            JONESCONTROL_ACTION_UPDATENUMBINDINGS_CONTROLLER(pAction, -1);
            JONESCONTROL_ACTION_UPDATENUMBINDINGS_KEYBOARD(pAction, +1);
        }
    }

    pAction[index + 1] = controlId;
    pScheme->bModified = 1;
    return 1;
}

void J3DAPI JonesControlSchemes_ResetAction(JonesControlsScheme* pScheme, JonesControlActions action, size_t defaultIndex)
{
    if ( !pScheme || action < 0 || action >= JONESCONTROL_ACTION_NUMACTIONS
        || defaultIndex >= JONESCONFIG_NUMDEFAULTCONTROLSCHEMES )
    {
        return;
    }

    size_t* pAction = pScheme->aActions[action];
    memset(pAction, 0, JONESCONTROL_ACTION_BINDARRAYSIZE * sizeof(size_t));

    const JonesConfigControlAction* pDefault = &jonesConfig_aDfltKeySets[defaultIndex][action];
    for ( size_t j = 1; j <= pDefault->numBinds; ++j )
    {
        size_t controlId = pDefault->aBindings[j - 1];
        pAction[j] = controlId;
        if ( JonesControlSchemes_IsKeyboardControl(controlId) )
        {
            JONESCONTROL_ACTION_UPDATENUMBINDINGS_KEYBOARD(pAction, +1);
        }
        else
        {
            JONESCONTROL_ACTION_UPDATENUMBINDINGS_CONTROLLER(pAction, +1);
        }
    }

    pScheme->bModified = 1;
}

void J3DAPI JonesControlSchemes_ResetToDefault(JonesControlsScheme* pScheme, size_t defaultIndex)
{
    if ( !pScheme || defaultIndex >= JONESCONFIG_NUMDEFAULTCONTROLSCHEMES )
    {
        return;
    }

    char aName[128] = { 0 };
    stdUtil_StringCopy(aName, STD_ARRAYLEN(aName), pScheme->aName);
    jonesConfig_SetDefaultControlScheme(pScheme, defaultIndex);
    STD_STRCPY(pScheme->aName, aName);
    pScheme->bModified = 1;
}
