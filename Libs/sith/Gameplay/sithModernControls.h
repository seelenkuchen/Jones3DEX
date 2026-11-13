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
    bool bLocked; // Added: target lock is held.
    bool bHasLockTarget; // Added: the lock has a live target.
    float lockTargetYaw; // Added: direction from Indy to the locked target, same convention.
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
    SithModernKey strafeLeft; // Added: locked-sideways input, played as thrust by sithPlayerControls.
    SithModernKey strafeRight; // Added
} SithModernOutput;

// Added: the one classifier shared by movement and the free camera. Relative is the original
// tank-style set (climbing, pushing, vehicles, slides); None leaves the real keys alone; every
// other state on a floor or in water is camera-relative.
SithModernControlMode J3DAPI sithModernControls_GetMode(const SithThing* pPlayer);

// Added: the pure decision. It computes the original movement keys from a move vector, the
// camera and player yaws and a mode, without touching the control system or a world.
void J3DAPI sithModernControls_Compute(const SithModernInput* pIn, SithModernOutput* pOut);

// Added: reads the move input and the player state, then sets this frame's virtual key
// overrides. Called at the start of sithPlayerControls_Process.
void J3DAPI sithModernControls_Update(SithThing* pPlayer, float secDeltaTime);

// Added: the target lock state (ADR-0029). GetLockedTarget stays valid for a held lock even in
// states that ignore movement; IsLockActive is false outside the camera-relative states.
SithThing* J3DAPI sithModernControls_GetLockedTarget(void);
bool J3DAPI sithModernControls_IsLockActive(void);
void J3DAPI sithModernControls_ResetLock(void);

// Added: -1..1 sideways input the locked move produces, consumed by sithPlayerControls.
float J3DAPI sithModernControls_GetStrafeInput(void);

// Added: if the free camera should ease toward a locked target this frame, write its yaw.
bool J3DAPI sithModernControls_GetLockCameraYaw(float* pYaw);

J3D_EXTERN_C_END
#endif // SITH_SITHMODERNCONTROLS_H
