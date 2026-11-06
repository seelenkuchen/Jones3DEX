// Added: the Babylon column's render frame owner, shared by the std3D adapter (std3DBabylon.cpp) and,
// from M6.7, the stdDisplay adapter. It holds the render device of the adapter context while open,
// the command list of the frame being recorded, and the back buffer: a Bgra8 color target (also
// sampled) and a depth target (D24S8, or D32F where the device lacks D24S8) at the display size.
// Until M6.7 std3D submits the frame at std3D_EndScene; on a device with a swapchain the submit first
// draws the back buffer to the swapchain with a full-screen pass, which presents it.
//
// Adapter code, C++ only: callers are the adapter files and the composition roots. Not thread-safe:
// call from the game thread.
#ifndef STD_STDRENDERBABYLON_H
#define STD_STDRENDERBABYLON_H
#ifndef __cplusplus
#error "stdRenderBabylon.h is C++ only: it names babylon::render types."
#endif

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Texture.h>

#include <cstdint>

// The render device a composition root creates, chosen with --render.
enum class StdRenderBabylonDeviceType
{
    Null, // babylon::render::NullDevice: records commands, draws nothing
    Gpu,  // the SDL GPU device
};

// Takes the --render option out of the command line: every "--render null" or "--render gpu" pair is
// removed from argv, the remaining arguments keep their order, *pArgc is reduced to match and
// argv[*pArgc] is set to nullptr. The last pair wins; *pType is left unchanged when there is none.
// Returns false, changing nothing, when a --render has no value or a value other than null or gpu.
// argv[0] is never looked at.
bool stdRenderBabylon_TakeRenderOption(int* pArgc, char** argv, StdRenderBabylonDeviceType* pType);

// Starts using pDevice, which must stay alive until stdRenderBabylon_Close. On a device with a
// swapchain it creates the present pipeline and sampler. Returns false, logging why, for a null
// pDevice, when already open, or when the present pipeline can't be created.
bool stdRenderBabylon_Open(babylon::render::Device* pDevice);

// Abandons a frame that wasn't submitted, then destroys the back buffer, the present pipeline and
// sampler and every texture whose destruction was deferred. A no-op when not open.
void stdRenderBabylon_Close();

bool stdRenderBabylon_IsOpen();

// The open device, or nullptr.
babylon::render::Device* stdRenderBabylon_GetDevice();

// Makes the back buffer width x height. A back buffer of another size is destroyed first (deferred
// while a frame is being recorded, like stdRenderBabylon_DestroyTexture). *pbRecreated (may be
// nullptr) is set to true when new targets were created: their contents are undefined until a pass
// clears them. Returns false, logging why, when not open, for a size of 0 or above the device's
// limits, and when a target can't be created; there is then no back buffer.
bool stdRenderBabylon_SetBackBufferSize(uint32_t width, uint32_t height, bool* pbRecreated);

// The back buffer targets, or null handles without a back buffer.
babylon::render::TextureHandle stdRenderBabylon_GetColorTarget();
babylon::render::TextureHandle stdRenderBabylon_GetDepthTarget();
babylon::render::TextureFormat stdRenderBabylon_GetDepthFormat();
uint32_t stdRenderBabylon_GetBackBufferWidth();
uint32_t stdRenderBabylon_GetBackBufferHeight();

// The command list of the frame being recorded; begins a frame when none is. Must be open. The
// reference is valid until stdRenderBabylon_SubmitFrame or stdRenderBabylon_Close.
babylon::render::CommandList& stdRenderBabylon_GetFrame();

// True while a frame has been begun and not submitted.
bool stdRenderBabylon_HasFrame();

// Submits the frame being recorded, if any: on a device with a swapchain it first records the
// present pass (the back buffer drawn over the whole swapchain image). Afterwards it destroys the
// textures whose destruction was deferred. Returns false, logging why, when a pass is still open or
// the device refuses the list; the frame is dropped either way.
bool stdRenderBabylon_SubmitFrame();

// Destroys texture now, or after the next submit while a frame is being recorded, since that frame
// may still name it. The null handle is ignored.
void stdRenderBabylon_DestroyTexture(babylon::render::TextureHandle texture);

#endif // STD_STDRENDERBABYLON_H
