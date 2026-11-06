// Added: the Babylon column's render frame owner, shared by the std3D adapter (std3DBabylon.cpp) and
// the stdDisplay adapter (stdDisplayBabylon.cpp). It holds the render device of the adapter context
// while open, the command list of the frame being recorded, and the back buffer: a Bgra8 color target
// (also sampled) and a depth target (D24S8, or D32F where the device lacks D24S8) at the display size.
// stdDisplay opens it (stdDisplay_Open), sizes the back buffer (stdDisplay_SetMode) and ends each frame
// at stdDisplay_Update with stdRenderBabylon_PresentFrame; on a device with a swapchain that submit
// first draws the back buffer to the swapchain with a full-screen pass, which presents it. std3D
// records its scenes into the same frame.
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
// sampler and every texture and buffer whose destruction was deferred. A no-op when not open.
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

// Destroys the back buffer (deferred while a frame is being recorded); there is none afterwards. A
// no-op without one.
void stdRenderBabylon_FreeBackBuffer();

// Copies the back buffer's mip 0 into pOut, which must have room for width * height * 4 bytes of
// Bgra8, rows top first, and sets *pWidth and *pHeight (both may be nullptr). The frame being
// recorded is submitted first, without the present pass (stdRenderBabylon_FlushFrame), so the copy
// holds everything drawn so far; it waits for the GPU. Returns false, logging why, when not open,
// without a back buffer, when the flush fails or when the device returns another size.
bool stdRenderBabylon_ReadBackBuffer(void* pOut, uint32_t* pWidth, uint32_t* pHeight);

// Replaces the back buffer's mip 0 with pData: width * height * 4 bytes of Bgra8, rows top first.
// The frame being recorded is submitted first, without the present pass, so what it drew comes
// before the new contents and what is recorded afterwards comes after them. Returns false, logging
// why, for a null pData, when not open, without a back buffer, when the flush fails or when the
// device refuses the upload.
bool stdRenderBabylon_WriteBackBuffer(const void* pData);

// A number that changes whenever the back buffer's contents may change: when it is created or
// destroyed, when a submitted list has a pass with its color target as an attachment or a copy into
// it, and at each stdRenderBabylon_WriteBackBuffer. While the frame being recorded has such a pass
// the number is one more than without it, which is what the submit of that frame makes it. Equal
// numbers mean equal contents, so a CPU copy of the back buffer can skip the readback while only the
// CPU changed it.
uint64_t stdRenderBabylon_GetBackBufferVersion();

// Records a pass into the frame (begun when none is) that clears the back buffer's color target to
// color and leaves depth alone. Returns false, logging why, when not open, without a back buffer, or
// when the command list refuses the pass (one is open).
bool stdRenderBabylon_ClearBackBuffer(const babylon::render::ClearColor& color);

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
// textures and buffers whose destruction was deferred. Returns false, logging why, when a pass is still open or
// the device refuses the list; the frame is dropped either way.
bool stdRenderBabylon_SubmitFrame();

// Submits the frame being recorded, if any, without the present pass: what it drew reaches the
// back buffer and nothing reaches the window. Afterwards it destroys what was deferred, like
// stdRenderBabylon_SubmitFrame. True without a frame; false, logging why, when a pass is still open
// or the device refuses the list; the frame is dropped either way.
bool stdRenderBabylon_FlushFrame();

// Ends the frame on the screen: begins one when none is being recorded (so a frame that only changed
// the back buffer through stdRenderBabylon_WriteBackBuffer is presented too) and submits it like
// stdRenderBabylon_SubmitFrame. Must be open.
bool stdRenderBabylon_PresentFrame();

// Destroys texture now, or after the next submit while a frame is being recorded, since that frame
// may still name it. The null handle is ignored.
void stdRenderBabylon_DestroyTexture(babylon::render::TextureHandle texture);

// The same for a buffer.
void stdRenderBabylon_DestroyBuffer(babylon::render::BufferHandle buffer);

#endif // STD_STDRENDERBABYLON_H
