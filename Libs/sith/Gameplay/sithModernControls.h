#ifndef SITH_SITHMODERNCONTROLS_H
#define SITH_SITHMODERNCONTROLS_H
#include <j3dcore/j3d.h>
#include <sith/types.h>

J3D_EXTERN_C_START

// Added: the Modern control style's camera-relative movement layer (ADR-0029). It turns the
// move input into the original movement keys once per frame, before sithPlayerControls_Process
// reads them.
typedef enum eSithModernControlMode
{
    SITHMODERNCONTROL_MODE_CAMERA_RELATIVE = 0,
    SITHMODERNCONTROL_MODE_RELATIVE        = 1,
    SITHMODERNCONTROL_MODE_NONE            = 2,
} SithModernControlMode;

// Added: one frame of already-gathered input. x is right and y is forward on screen, each -1..1.
typedef struct sSithModernInput
{
    float x;
    float y;
    float cameraYaw; // Degrees, 0 faces +Y and positive turns left.
    float playerYaw; // Degrees, in the same convention as cameraYaw.
    SithModernControlMode mode;
    bool bRunKey; // The real RUN binding, which keeps its Classic meaning.
} SithModernInput;

typedef struct sSithModernKey
{
    bool bPressed;
    float amount; // 0..1
} SithModernKey;

typedef struct sSithModernOutput
{
    SithModernKey forward;
    SithModernKey back;
    SithModernKey turnLeft;
    SithModernKey turnRight;
    SithModernKey run;
} SithModernOutput;

// Added: the pure decision. It computes the original movement keys from a move vector, the
// camera and player yaws and a mode, without touching the control system or a world.
void J3DAPI sithModernControls_Compute(const SithModernInput* pIn, SithModernOutput* pOut);

// Added: reads the move input and the player state, then sets this frame's virtual key
// overrides. Called at the start of sithPlayerControls_Process.
void J3DAPI sithModernControls_Update(SithThing* pPlayer, float secDeltaTime);

J3D_EXTERN_C_END
#endif // SITH_SITHMODERNCONTROLS_H
