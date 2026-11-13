// Added: the Babylon column's Sound settings screen (ADR-0026), the original Sound dialog
// (jonesConfig.c, dialog 113). The wrapper keeps the original return values: 1 applies the
// changed settings, 2 cancels and writes nothing.
//
// The output-device helper is the test surface: it builds the choice the screen shows from the
// sound driver's output list, with the system default first.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUISOUNDSETTINGS_H
#define JONES3D_PLATFORM_BABYLON_JONESUISOUNDSETTINGS_H
#ifndef __cplusplus
#error "JonesUiSoundSettings.h is C++ only: it names babylon::ui and std::string."
#endif

#include <Jones3D/types.h>

#include <babylon/ui/Painter.h>
#include <babylon/ui/Ui.h>

#include <string>
#include <vector>

// One output-device choice. The first entry is always the system default and has an empty id,
// which is what engine.audio.outputDevice stores for it. id and name are the driver's strings.
struct JonesUiSoundOutputDevice
{
    std::string id;
    std::string name;
};

// Fills devices with the system default first, then every output the sound driver reports, in
// driver order. A driver that reports no outputs leaves the default as the only entry.
void JonesUiSound_CollectOutputDevices(std::vector<JonesUiSoundOutputDevice>& devices);

// The values a Sound screen edits and the widget indices its OK handler reads.
struct JonesUiSoundValues
{
    double volume = 1.0;
    bool bReverse = false;
    std::size_t outputDevice = 0;
};

struct JonesUiSoundLayout
{
    std::size_t volume = 0;
    std::size_t reverse = 0;
    std::size_t outputDevice = 0;
    std::size_t ok = 0;
    std::size_t back = 0;
};

// Everything the screen needs from the sound driver, captured when it opens.
struct JonesUiSoundModel
{
    JonesSoundSettings* pData = nullptr;
    std::vector<JonesUiSoundOutputDevice> devices;
    std::size_t curDevice = 0;
};

// Reads the sound state and output list into a model and the initial values.
JonesUiSoundModel JonesUiSound_LoadModel(JonesSoundSettings* pData,
    JonesUiSoundValues& values);

// Builds the rows from values.
babylon::ui::Screen JonesUiSound_BuildScreen(const JonesUiSoundModel& model,
    const JonesUiSoundValues& values, JonesUiSoundLayout& layout);

// Reads the current widget values while screen is open.
void JonesUiSound_ReadValues(const babylon::ui::Ui& ui, babylon::ui::ScreenId screen,
    const JonesUiSoundLayout& layout, JonesUiSoundValues& values);

// Applies values exactly as the original OK handler did. Returns 1.
int JonesUiSound_Apply(const JonesUiSoundModel& model, const JonesUiSoundValues& values);

// Runs the Sound screen through ui. On OK it writes pData->maxSoundVolume and
// pData->bReverseSound and their Jones.cfg keys, switches to the chosen output device with
// SoundDriver_SetOutputDevice and stores its id in engine.audio.outputDevice (empty for the
// default), and returns 1. pData->b3DHWSupport is left unchanged. On back it returns 2 and writes
// nothing. pPainter may be null, which runs the screen without drawing. pData must not be null.
int JonesUiSound_RunSoundScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesSoundSettings* pData);

#ifdef J3D_TEST
// Test support: appends every screen this module builds, for the action-id audit.
void JonesUiSound_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens);
#endif

#endif // JONES3D_PLATFORM_BABYLON_JONESUISOUNDSETTINGS_H
