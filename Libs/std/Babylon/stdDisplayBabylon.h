// Added: the Babylon display adapter's runtime controls that are not part of
// the recovered stdDisplay API (M9.23 frame pacing). The game's Display options
// call this after writing the config key, so a change applies without closing
// the display.
#ifndef STD_STDDISPLAYBABYLON_H
#define STD_STDDISPLAYBABYLON_H
#ifndef __cplusplus
#error "stdDisplayBabylon.h is C++ only: it is part of the Babylon adapter."
#endif

// Sets the presentation cap at once. frameLimit follows
// STDDISPLAY_CFG_FRAMELIMIT: 0 presents with V-Sync, a positive value caps the
// frame rate with V-Sync off, and -1 presents unlimited with V-Sync off. Other
// values are treated as 0. Safe before the display starts (the startup then
// reads the config) and with no render device.
void stdDisplayBabylon_SetFrameLimit(int frameLimit);

#endif // STD_STDDISPLAYBABYLON_H
