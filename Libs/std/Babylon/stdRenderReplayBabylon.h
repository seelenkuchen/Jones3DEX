// Added: the Babylon column's image output for the render capture replay (Jones3D-replay). It takes
// the --frames-out and --every options out of the command line, reads the replayed back buffer back
// after each stdDisplay_Update record and writes the frames as 24-bit BMP files.
#ifndef STD_STDRENDERREPLAYBABYLON_H
#define STD_STDRENDERREPLAYBABYLON_H
#ifndef __cplusplus
#error "stdRenderReplayBabylon.h is C++ only."
#endif

#include <cstddef>
#include <cstdint>

struct StdRenderReplayBabylonFrameOutput
{
    const char* pDirectory = nullptr; // nullptr: write no images
    std::uint32_t every    = 1;       // write every n-th frame, counting from frame 0
    std::size_t frames     = 0;       // stdDisplay_Update records seen
    std::size_t images     = 0;       // BMP files written
};

// Takes "--frames-out <dir>" and "--every <n>" out of the command line, like
// stdRenderBabylon_TakeRenderOption. The last pair wins; *pOutput is left unchanged on failure.
// A directory is required for --frames-out, n must be a positive decimal integer, --every needs
// --frames-out, and both need --render gpu (bRenderGpu). Returns false, changing nothing, with
// *ppError (may be nullptr) at a static message when the options are missing a value, malformed,
// or don't fit together. argv[0] is never looked at.
bool stdRenderReplayBabylon_TakeFrameOptions(
    int* pArgc,
    char** argv,
    bool bRenderGpu,
    StdRenderReplayBabylonFrameOutput* pOutput,
    const char** ppError
);

// Creates pOutput->pDirectory when it is missing. Returns false, logging why, when that fails.
bool stdRenderReplayBabylon_BeginFrames(StdRenderReplayBabylonFrameOutput* pOutput);

// A stdRenderReplay_RunWithFrameCallback observer: counts the frame and, on every every-th one,
// reads the back buffer and writes <dir>/frame_<NNNNN>.bmp. Write failures are logged and leave
// pOutput->images unchanged.
void stdRenderReplayBabylon_CaptureFrame(std::size_t frameIndex, void* pUserData);

#endif // STD_STDRENDERREPLAYBABYLON_H
