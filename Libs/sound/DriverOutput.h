#ifndef SOUND_DRIVER_OUTPUT_H
#define SOUND_DRIVER_OUTPUT_H
#include <j3dcore/j3d.h>
#include <stddef.h>

J3D_EXTERN_C_START

// Added: the output-device selection shared by both sound driver columns. The Babylon driver
// answers with the Babylon audio devices and can switch between them; the Null driver reports no
// devices and refuses every switch. The configuration key is written by whoever offers the
// choice (the settings screen); the driver only reads the id it is handed.
//
// The longest output-device id a caller has to keep for the configuration file. Ids are opaque
// strings; a longer id is still listed, but this is the size the startup read reserves.
#define SOUNDDRIVER_OUTPUTDEVICE_ID_MAX 512

// The number of output devices the driver can open right now, and the index of the last call to
// this function is what SoundDriver_GetOutputDevice() reads. The list is cached here so that
// index-based reads stay consistent even when devices are plugged in or removed between calls.
// Zero when there is no audio device or the backend cannot enumerate outputs.
size_t SoundDriver_GetNumOutputDevices(void);

// Copies the id and display name of the output device at index into the caller's buffers, both
// truncated to fit and always NUL-terminated when the buffer is non-null and its size is not zero.
// A null buffer or a zero size leaves that string alone. Returns 1 on success and 0 when index is
// out of range, in which case nothing is written.
int SoundDriver_GetOutputDevice(size_t index, char* pId, size_t idSize, char* pName, size_t nameSize);

// Opens the output device pId, closing the one in use first. NULL or an empty id selects the
// system default. Sounds and voices, their settings and their positions are kept. Returns 1 on
// success and 0 when the id is unknown or the output cannot be opened; in that case the previous
// output is opened again, or the default when the previous one is gone. On success pId also
// becomes the configured device, so a later loss prefers it.
int SoundDriver_SetOutputDevice(const char* pId);

// The id of the output in use, borrowed until the next call that changes the output, or an empty
// string for the system default and when no output is open.
const char* SoundDriver_GetCurrentOutputDevice(void);

// Records the device SoundDriver_Open() opens and a later loss reopens: NULL or an empty id means
// the system default. This does not open anything, so the startup config read sets it before
// SoundDriver_Open(). It is kept even when the device is missing, so the device is used again when
// it returns.
void SoundDriver_SetConfiguredOutputDevice(const char* pId);

J3D_EXTERN_C_END
#endif // SOUND_DRIVER_OUTPUT_H
