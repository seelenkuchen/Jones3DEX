// Added: the Babylon column's render frame owner, shared by the std3D adapter (std3DBabylon.cpp) and
// the stdDisplay adapter (stdDisplayBabylon.cpp). It holds the render device of the adapter context
// while open, the command list of the frame being recorded, and the scene targets at the display
// size times the render scale: an Rgba16F color target (also sampled; Bgra8 where the device can't
// render to Rgba16F), a depth target (D24S8, or D32F where the device lacks D24S8; also sampled
// where the device can) and a normal target (R10G10B10A2, or Rgba8 where the device can't render to
// it; also sampled) that std3D's scene passes write beside the color. stdDisplay opens it
// (stdDisplay_Open), sizes the back buffer (stdDisplay_SetMode) and ends each frame at
// stdDisplay_Update with stdRenderBabylon_PresentFrame. That submit first runs the post-processing
// chain, an ordered list of full-screen passes that is empty by default, then the built-in
// depth-of-field stage when its cutscene or gameplay option is on and the game set a focus, then
// the built-in light-shaft stage when it is on, the built-in lens-flare stage when it is on, then the
// built-in bloom stage when it is on, and then, on a device with a swapchain, runs the built-in final
// tone-map pass
// over that output (the back buffer color when the chain is empty and the later stages are off) and writes it
// to the swapchain at the presented size, sampling with linear filtering when the render scale is
// not 1 so supersampled scenes resolve cleanly. When the
// anti-aliasing option is on (SMAA 1x, engine.renderer.antiAliasing) the tone map writes an LDR
// image at the presented size instead, three SMAA passes smooth the scene's edges, and the last one
// writes the swapchain; with it off the image is byte-identical to the plain tone-map present.
// Added: the sharpening option (AMD FidelityFX CAS, engine.renderer.sharpening) then runs one
// contrast-adaptive sharpening pass on that presented-size LDR image, after SMAA or after the tone
// map when anti-aliasing is off; at 0 it is skipped and the image is byte-identical. The vignette and
// film-grain option then runs the Babylon postfx vignette and film-grain effects on a presented-size
// image after it when either is on; both off skip them and leave the image byte-identical. It then composites the gameplay HUD layer (see std3D_BeginHudLayer and the getters below) over that
// image, so no anti-aliasing, sharpening or other scene effect touches the HUD, before the prompt
// and menu present overlays. std3D records its scenes and HUD layer into the same frame. stdRenderBabylon_ReadBackBuffer and _WriteBackBuffer
// translate between the scene color target and the Bgra8 bytes the game and stdDisplay work with,
// resampling between the scaled target and the display-sized image there.
//
// The game keeps its video mode: the canvas and every screen coordinate are in display pixels, and
// GetBackBufferWidth/Height answer those. Only the GPU scene targets are larger or smaller than the
// display when the render scale is not 1; the scene still fills them because its screen vertices are
// mapped over the same display-sized viewport. ReadBackBuffer, and with it stdDisplay's lock and the
// replay's frame output, always return the display-sized image.
//
// The chain and the final pass never write the back buffer: stdRenderBabylon_ReadBackBuffer, and
// with it stdDisplay's lock and the replay's frame output, always read what the game drew.
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
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>

#include <cstddef>
#include <cstdint>

// Added: the surface tags in the normal target's alpha (M9.16). The normal target is R10G10B10A2
// (2 alpha bits) or Rgba8 (8), so the values are 0, 1/3, 2/3 and 1, which both formats represent
// exactly (2/3 = 170/255, 1/3 = 85/255): none (cleared, nothing drawn), water, sky (horizon or
// ceiling sky) and ordinary. Only the scene's sectors and their sky and water surfaces carry a tag;
// things, sprites, particles, the HUD and every other draw are ordinary.
struct StdRenderBabylonSurfaceTags
{
    float none;     // 0.0
    float water;    // 1/3
    float sky;      // 2/3
    float ordinary; // 1.0
};
inline constexpr StdRenderBabylonSurfaceTags stdRenderBabylon_kSurfaceTags = {
    0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };

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

// Makes the back buffer width x height in display pixels. The scene targets are made at
// width * render scale by height * render scale (rounded, at least 1 * 1), clamped to the device's
// max texture size with one logged line when the clamp applies. A back buffer of another size or
// scale is destroyed first (deferred while a frame is being recorded, like
// stdRenderBabylon_DestroyTexture). *pbRecreated (may be nullptr) is set to true when new targets
// were created: their contents are undefined until a pass clears them. Returns false, logging why,
// when not open, for a size of 0 or above the device's limits, and when a target can't be created;
// there is then no back buffer.
bool stdRenderBabylon_SetBackBufferSize(uint32_t width, uint32_t height, bool* pbRecreated);

// Destroys the back buffer (deferred while a frame is being recorded); there is none afterwards. A
// no-op without one.
void stdRenderBabylon_FreeBackBuffer();

// Copies the back buffer into pOut, which must have room for the display-sized width * height * 4
// bytes of Bgra8, rows top first, and sets *pWidth and *pHeight to that display size (both may be
// nullptr). At render scale 1 this is the scene color target's mip 0: on an Rgba16F target the half
// floats are clamped to 0..1 and rounded to 8-bit unorm, a Bgra8 target is copied as it is. At
// another scale the scaled target is read on the CPU and box-filtered down (above 1) or bilinearly
// sampled up (below 1) into the display size. The frame being recorded is submitted first, without
// the present pass (stdRenderBabylon_FlushFrame), so the copy holds everything drawn so far; it
// waits for the GPU. Returns false, logging why, when not open, without a back buffer, when the
// flush fails or when the device returns another size.
bool stdRenderBabylon_ReadBackBuffer(void* pOut, uint32_t* pWidth, uint32_t* pHeight);

// Replaces the back buffer with pData: display-sized width * height * 4 bytes of Bgra8, rows top
// first. At render scale 1 the bytes go straight into the scene color target (half floats in 0..1
// on an Rgba16F target). At another scale they are resampled on the CPU to the scaled target first
// (box down, bilinear up). The frame being recorded is submitted first, without the present pass,
// so what it drew comes before the new contents and what is recorded afterwards comes after them.
// Returns false, logging why, for a null pData, when not open, without a back buffer, when the
// flush fails or when the device refuses the upload.
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

// The scene color target's format: Rgba16F on a device that can render and sample it, else Bgra8.
// Pipelines that draw the scene name it as their first color target. Chosen when the frame owner
// opens and constant while it is open.
babylon::render::TextureFormat stdRenderBabylon_GetColorFormat();

// The normal target: per pixel the view-space normal of the geometry drawn there, its components
// stored as n * 0.5 + 0.5 in red, green and blue. Since M9.16 alpha is the surface tag of
// stdRenderBabylon_kSurfaceTags: 1 ordinary, 2/3 sky (horizon or ceiling sky), 1/3 water; it is
// cleared to 0 (nothing drawn) whenever the scene clears depth. Draws that write depth write it,
// the others leave it, so it matches the depth target. A null handle without a back buffer.
babylon::render::TextureHandle stdRenderBabylon_GetNormalTarget();
babylon::render::TextureFormat stdRenderBabylon_GetNormalFormat();

// Added: the gameplay HUD layer's targets, at the display size (not the render scale). Its color
// target is the scene color format and also sampled; the normal target is a same-format dummy the
// HUD draws write because their shaders output a normal, and the depth target uses the scene depth
// format. std3D draws the HUD into them through std3D_BeginHudLayer/EndHudLayer, and a presenting
// submit composites the color over the tone-mapped scene with alpha blending. Null without a back
// buffer.
babylon::render::TextureHandle stdRenderBabylon_GetHudColorTarget();
babylon::render::TextureHandle stdRenderBabylon_GetHudNormalTarget();
babylon::render::TextureHandle stdRenderBabylon_GetHudDepthTarget();

// The back buffer's size in display pixels: the game's video mode, the canvas and every screen
// coordinate. The scene targets are this size times the render scale (GetRenderWidth/Height).
uint32_t stdRenderBabylon_GetBackBufferWidth();
uint32_t stdRenderBabylon_GetBackBufferHeight();

// The scene targets' size in pixels: the display size times the render scale, rounded and clamped.
// std3D's screen vertices stay in display pixels and map over this target unchanged, so a larger
// target supersamples the scene. Zero without a back buffer.
uint32_t stdRenderBabylon_GetRenderWidth();
uint32_t stdRenderBabylon_GetRenderHeight();

// The internal 3D resolution as a multiple of the display size: 1.0 by default. Accepted values
// are clamped to 0.5..2.0; a non-finite value is refused, logging why and changing nothing. Returns
// false only for such a value. The scene targets are made at the new size now when a back buffer
// exists and no frame is being recorded; otherwise the next SetBackBufferSize applies it. Closing
// the frame owner resets it, and opening reads the config key again. Changes are presentation only:
// the display size, coordinates and read-back image stay as they are.
bool stdRenderBabylon_SetRenderScale(float scale);
float stdRenderBabylon_GetRenderScale();

// Added: SMAA 1x on the tone-mapped 3D scene, before the HUD layer composite. On: the final
// tone-map pass writes an LDR image at the presented size, then luma edge detection, blending
// weights (with SMAA's precomputed area and search textures) and neighbourhood blending write the
// presented frame. Off: the plain tone-map present, byte-identical to the pre-M9.8 output. The HUD
// layer, menus and prompts are never anti-aliased. Opening reads engine.renderer.antiAliasing
// (0 off, 1 SMAA; default 1 in Enhanced builds, 0 in Retail/QOL-off ones); this setter changes it
// until the next open. Closing frees the stage's targets, pipelines and textures.
bool stdRenderBabylon_SetAntiAliasing(bool bEnabled);
bool stdRenderBabylon_GetAntiAliasing();

// Added: contrast-adaptive sharpening (AMD FidelityFX CAS) on the presented-size 3D image, after
// SMAA when anti-aliasing is on and after the tone map when it is off, before the HUD layer
// composite. sharpening is 0 (off) to 1 (strongest); the Options > Display row offers 0, 0.3, 0.5
// and 0.8. At 0 the pass is skipped and the image is byte-identical to the path without it. The
// HUD layer, menus and prompts are never sharpened. A finite value outside 0..1 is clamped; a
// non-finite one is refused, logging why and changing nothing. Opening reads
// engine.renderer.sharpening (default 0.3 in Enhanced builds, 0 in Retail/QOL-off ones); this
// setter changes it until the next open. Closing frees the stage's target and pipelines.
bool stdRenderBabylon_SetSharpening(float strength);
float stdRenderBabylon_GetSharpening();

// Added: the built-in bloom stage on the 3D scene, before the tone map. strength is 0 (off) to 0.5
// (the High choice); the prefilter's soft-knee threshold (0.6) and knee (0.3) and the option levels'
// strengths live in stdRenderBabylon.cpp. On: a half-size soft-knee luminance prefilter, a
// downsample chain to 1/32 of the scene, additive tent upsamples back to 1/2 and a scene-sized
// composite that adds the result times the strength to the scene; its targets are in the scene
// format, at the scene size times the render scale, and are recreated when that size changes. The
// tone map then reads the composite. Off: no bloom pass is recorded and the image is byte-identical
// to the path without the option. The HUD layer, menus and prompts come after and are untouched.
// A finite value outside 0..0.5 is clamped; a non-finite one is refused, logging why and changing
// nothing. Opening reads engine.renderer.bloom (0 off, 1 Low, 2 Medium, 3 High = strength 0, 0.15,
// 0.3, 0.5; default 1 in Enhanced builds, 0 in Retail/QOL-off ones); this setter changes it until
// the next open. Closing frees the stage's targets and pipelines.
bool stdRenderBabylon_SetBloom(float strength);
float stdRenderBabylon_GetBloom();

// Added: the built-in light-shaft stage over the HDR scene, after depth of field and before bloom.
// level is 0 (Off), 1 (Low), 2 (Medium) or 3 (High); the strengths are 0, 0.25, 0.5 and 0.8 and
// live with the stage's other constants in stdRenderBabylon.cpp. On: a half-resolution mask keeps
// the bright sky pixels (the normal target's alpha is the sky surface tag), the mask's mean
// weighted position is reduced to 1 x 1 on the GPU as the light position, a 40-tap radial blur of
// the mask toward it is scaled by a strength smoothed over about 0.3 s of game time, and a
// scene-sized composite adds the result to the HDR scene. No sky visible fades the strength to zero.
// Off, or a failure, skips every pass and the image is byte-identical to the path without the
// option. The HUD layer, menus and prompts come after and are untouched. A level outside 0..3 is
// clamped. Opening reads engine.renderer.lightShafts (default 1 Low in Enhanced builds, 0 in
// Retail/QOL-off ones); this setter changes it until the next open. Closing frees the stage's
// targets and pipelines.
bool stdRenderBabylon_SetLightShafts(int level);
int stdRenderBabylon_GetLightShafts();

// Added: the built-in lens-flare stage over the HDR scene, after the light-shaft stage and before
// bloom, so the flare blooms with the rest of the bright image. It reuses the light-shaft stage's
// GPU-reduced 1 x 1 sky centroid and its smoothed visible-sky strength (no readback): a few tinted
// ghosts sit along the line from the light through the screen centre and a soft halo surrounds the
// light, both scaled by the strength. With light shafts off, only the mask and centroid reduction
// needed for the centroid run, not the shaft blur. No sky visible, or a centroid off screen, adds
// nothing. Off, or a failure, records no pass and the image is byte-identical to the path without
// it. The HUD layer, menus and prompts come after and are untouched. Opening reads
// engine.renderer.lensFlare (default on in Enhanced builds, off without QOL); this setter changes
// it until the next open. Closing frees the stage's target and pipeline.
bool stdRenderBabylon_SetLensFlare(bool bEnabled);
bool stdRenderBabylon_GetLensFlare();

// Added: the vignette and film-grain stage on the presented-size LDR image, after SMAA and
// sharpening and before the HUD layer composite, so the HUD, menus and prompts are untouched. The
// vignette and the film grain are drawn by the Babylon postfx effects (babylon::postfx,
// ADR-0043). The vignette smoothly darkens the corners, aspect-corrected, with the centre
// unchanged. The film grain is deterministic luminance noise from a hash of the pixel and a
// game-time frame counter, weaker in the highlights, with zero mean. Each on its own is
// byte-identical to the path without it; both off skip the passes and the image is byte-identical
// to today. Opening reads engine.renderer.vignette and engine.renderer.filmGrain (both off in every
// build by default); these setters change them until the next open. Closing frees the effects,
// their targets and their pipelines.
bool stdRenderBabylon_SetVignette(bool bEnabled);
bool stdRenderBabylon_GetVignette();
bool stdRenderBabylon_SetFilmGrain(bool bEnabled);
bool stdRenderBabylon_GetFilmGrain();

// Added: the built-in water-ripple stage, after the post-processing chain and before depth of field.
// On pixels whose normal target's alpha carries the water surface tag (1/3), the scene is sampled at
// a small animated offset from two scrolling sine layers whose phase follows the frame's game time,
// and a faint view-dependent highlight is added. The offset is a constant fraction of the output
// height, so it scales with the resolution; a sample that lands on a non-water pixel falls back to
// the unoffset sample, so geometry next to water never smears. Every other pixel is copied
// unchanged. The HUD layer, menus and prompts come after and are untouched. Off, or a failure,
// skips the pass and the image is byte-identical to the path without the option. Opening reads
// engine.renderer.water.ripples (on in Enhanced builds, off in Retail/QOL-off ones); this setter
// changes it until the next open. Reflections are not built: translucent water does not write depth,
// so the depth target at a water pixel holds the scene behind it rather than the water plane, and a
// screen-space ray march there would use the wrong depth.
bool stdRenderBabylon_SetWaterRipples(bool bEnabled);
bool stdRenderBabylon_GetWaterRipples();

// Added: the built-in screen-space ambient occlusion stage over the HDR scene, after the contact
// shadows and before depth of field. level is 0 (Off), 1 (Low), 2 (Medium) or 3 (High); the sample
// count, world-space radius, bias and intensity of each level live with the stage's other constants
// in stdRenderBabylon.cpp. On: view-space positions and normals are reconstructed from the depth
// target (the classic draws write a constant normal), an 8, 12 or 16 sample normal-oriented
// hemisphere kernel is gathered at half the scene size in a fixed 4 x 4 interleaved rotation
// pattern, a depth-aware 4 x 4 blur removes the pattern, and a scene-sized composite multiplies the
// HDR colour by the result. Pixels tagged sky or water, and pixels with nothing drawn, are left
// unchanged. Off, or a failure, skips every pass and the image is byte-identical to the path
// without the option. The HUD layer, menus and prompts come after and are untouched. A level
// outside 0..3 is clamped. Opening reads engine.renderer.ssao (default 1 Low in Enhanced builds, 0
// in Retail/QOL-off ones); this setter changes it until the next open. Closing frees the stage's
// targets and pipelines.
bool stdRenderBabylon_SetSsao(int level);
int stdRenderBabylon_GetSsao();

// Added: the built-in screen-space contact-shadow stage over the HDR scene, after the water
// ripples and before the ambient-occlusion stage, so ambient occlusion and every later stage read
// its result. Babylon's postfx `ContactShadowEffect` (ADR-0043) draws it. One fixed overhead light,
// 35 degrees from vertical and 30 degrees in azimuth in the Z-up game world, is converted to the
// shader camera's view space from the transform given to std3D_SetCameraTransform; without a
// camera transform the stage is skipped. A short ray is marched through the depth target toward
// the light, and only ordinary surfaces (the normal target's alpha is the ordinary surface tag,
// class 3) are darkened; sky-tagged, water-tagged, empty and the 0..0.1 first-person overlay pixels
// are returned unchanged, as are the HUD, 2D layer, menus and prompts. Off, or a failure, records
// no pass and the image is byte-identical to the path without the option. A setter before the
// frame owner is open fails, logging why; closing frees the stage's target and the effect's
// resources. Opening reads engine.renderer.contactShadows (default on in Enhanced builds, off in
// Retail/QOL-off ones); this setter changes it until the next open.
bool stdRenderBabylon_SetContactShadows(bool bEnabled);
bool stdRenderBabylon_GetContactShadows();

// Added: the built-in character-shadow stage over the HDR scene, after the contact shadows and
// before the ambient-occlusion stage, so ambient occlusion and every later stage read its result.
// The std3D draw path collects every scene-pass draw whose render state carries
// STD3D_RS_SHADOW_CASTER as view-space triangles; Babylon's shadow module renders them into a 2048
// overhead shadow map fitted around them, and Babylon's postfx `ShadowReceiveEffect` (ADR-0041,
// ADR-0043) darkens the ordinary surfaces (the normal target's alpha is the ordinary surface tag,
// class 3) whose path to the same fixed overhead light is blocked. The light is the M11.1 light, 35
// degrees from vertical and 30 degrees in azimuth in the Z-up world, converted to the shader
// camera's view space from the transform given to std3D_SetCameraTransform; without a camera
// transform, without casters, or when the map fails, the stage is skipped for that frame and its
// image is byte-identical. `std3D_DrawsThingShadows` reports it so the game can skip the original
// blob shadow while it is on; when it is off or could not be set up the blob stays. Sky-tagged,
// water-tagged, empty and the 0..0.1 first-person overlay pixels are returned unchanged, as are the
// HUD, 2D layer, menus and prompts. A setter before the frame owner is open fails, logging why;
// closing frees the stage's target, the map and the effect's resources. Opening reads
// engine.renderer.shadows (default on in Enhanced builds, off in Retail/QOL-off ones); this setter
// changes it until the next open.
bool stdRenderBabylon_SetShadows(bool bEnabled);
bool stdRenderBabylon_GetShadows();

// Added: the built-in camera motion-blur stage over the HDR scene, after the ambient-occlusion
// stage and before depth of field. level is 0 (Off), 1 (Low), 2 (Medium) or 3 (High); each level's
// tap count, shutter fraction and maximum blur length (a fraction of the output height) live with
// the stage's other constants in stdRenderBabylon.cpp. The game feeds the camera each frame through
// std3D_SetCameraTransform; the pass reconstructs each pixel's view position from the depth target
// exactly as the ambient-occlusion pass does, takes it to the world with the current camera and into
// the previous frame's camera, projects it with the same tangents and gathers the HDR scene along
// the resulting screen-space velocity. Sky pixels take part, so a camera rotation blurs the sky;
// pixels with nothing drawn and the first-person overlay (sz < 0.1) do not move. The blur is
// skipped after a camera cut, when the frame's game-time step is zero, after a teleport or a large
// snap, and on the first frame after open. Off, or a failure, skips the pass and the image is
// byte-identical to the path without the option. The HUD layer, menus and prompts come after and
// are untouched. A level outside 0..3 is clamped. Opening reads engine.renderer.motionBlur (default
// 1 Low in Enhanced builds, 0 in Retail/QOL-off ones); this setter changes it until the next open.
// Closing frees the stage's target and pipeline.
bool stdRenderBabylon_SetMotionBlur(int level);
int stdRenderBabylon_GetMotionBlur();

// Added: the current camera's world transform for the motion-blur stage, called by
// std3D_SetCameraTransform. aMatrix is 12 floats in rdCamera's model-matrix order: right, forward
// and up basis vectors, then the world position. bCut marks a camera cut. Presentation state only.
void stdRenderBabylon_SetCameraTransform(const float aMatrix[12], bool bCut);

// Added: the frame's projection for the ambient-occlusion reconstruction. fovDegrees is the
// horizontal fov at the 4:3 reference aspect, in degrees, as rdCamera's fov is; the stage derives
// the vertical tangent from it and the horizontal tangent from the real aspect (Hor+). nearPlane
// and farPlane are the frame's planes. The values are presentation state only; invalid ones are
// ignored.
void stdRenderBabylon_SetProjection(float fovDegrees, float nearPlane, float farPlane);

// Added: the frame's game time in seconds, used by the light-shaft fade and the water ripple phase.
// Presentation state only; a non-finite value is ignored and the step is clamped to 0.25 s.
void stdRenderBabylon_SetFrameTime(float deltaTime);

// Added: the built-in depth-of-field stage on the HDR scene, after the post-processing chain and
// before bloom. The game feeds it each frame with std3D_SetFocus: the view-space distance from the
// camera to its focus thing, whether the camera is a cutscene camera, and the frame's near and far
// planes. The renderer smooths the distance over about 0.2 s of game time (it is presentation
// state, never saved or simulated). The circle of confusion comes from the depth target
// (linearised with the near and far planes): gameplay mode blurs only the background, with the
// clear blur starting at three times the focus distance and strengthening over the next several
// times that, so Indy, the foreground and the HUD-less near geometry stay sharp; cutscene mode
// blurs both nearer and farther than the focus. A half-resolution gather blur (20 taps in a disc)
// weighted by the circle of confusion is composited back over the scene with the same weight. The
// constants live in stdRenderBabylon.cpp. Both options off, or no focus set this run, skip the
// stage and the image is byte-identical to the path without it. The HUD layer, menus and prompts
// come after and are untouched. Opening reads engine.renderer.dof.cutscenes and
// engine.renderer.dof.gameplay (default 1 in Enhanced builds, 0 without QOL) and
// engine.renderer.dof.strength (0.5..2.0, default 1.0); this setter changes them until the next
// open. Closing frees the stage's targets and pipelines.
bool stdRenderBabylon_SetDepthOfField(bool bCutscenes, bool bGameplay, float strength);
bool stdRenderBabylon_GetDepthOfFieldCutscenes();
bool stdRenderBabylon_GetDepthOfFieldGameplay();
float stdRenderBabylon_GetDepthOfFieldStrength();

// The smoothed focus distance in view-space units, or 0 before the first std3D_SetFocus. Tests
// read it to prove the smoothing reaches its target.
float stdRenderBabylon_GetFocusDistance();

// Added: the per-frame focus input of the depth-of-field stage, called by std3D_SetFocus. distance
// is in view-space units, bCutscene selects the cutscene ramp, nearPlane and farPlane linearise the
// depth target, and deltaTime is the frame's game time in seconds. The distance is smoothed here,
// in presentation state only. A non-finite distance is ignored; a non-positive deltaTime snaps.
void stdRenderBabylon_SetFocus(float distance, bool bCutscene, float nearPlane, float farPlane,
    float deltaTime);

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

// Where a present overlay draws. A null texture names the swapchain; format is then the device's
// swapchain format and is ignored. A texture names an offscreen color target of that format and
// width x height. width and height are in pixels.
struct StdRenderBabylonOverlayTarget
{
    babylon::render::TextureHandle texture;
    babylon::render::TextureFormat format = babylon::render::TextureFormat::Bgra8;
    std::uint32_t width  = 0;
    std::uint32_t height = 0;
};

// An extra pass recorded on top of the presented frame: the callback runs after the post-processing
// chain and the present pass of every presenting submit and before the single Device::submit(), on
// the same command list. It must open and close its own passes and must not submit; the target is
// the swapchain or the offscreen target of stdRenderBabylon_RenderPresentToTexture. For a swapchain
// target, width and height are the game window's drawable size, or the back buffer's without a
// window. Returns true when it recorded commands. pfn is called on the render thread while the
// frame is open. Registering nullptr removes it. Open and Close clear it.
using StdRenderBabylonPresentOverlay = bool (*)(void* pUser,
    babylon::render::CommandList& list, const StdRenderBabylonOverlayTarget& target);
void stdRenderBabylon_SetPresentOverlay(StdRenderBabylonPresentOverlay pfn, void* pUser);
bool stdRenderBabylon_HasPresentOverlay();

// Added: a second overlay for the gameplay HUD, such as an interaction prompt drawn while no menu
// is open. It uses the same callback contract and target as the present overlay and runs first, so
// a menu overlay (and its dim layer) covers it. The game installs at most one of each. Open and
// Close clear both.
void stdRenderBabylon_SetHudOverlay(StdRenderBabylonPresentOverlay pfn, void* pUser);
bool stdRenderBabylon_HasHudOverlay();

// Draws what the last presenting submit presents (the post-processing chain's output, or the back
// buffer) and the present overlay into target with one submit, without presenting. target must be
// a Bgra8 color target of width x height; the caller reads it back and destroys it. Use this for
// screenshots of the window, including the overlay a menu records on the swapchain. Returns false,
// logging why, when not open, without a back buffer, while a frame is being recorded, or when the
// device refuses the frame.
bool stdRenderBabylon_RenderPresentToTexture(babylon::render::TextureHandle target,
    std::uint32_t width, std::uint32_t height);

// The built-in final tone-map pass's parameters. It runs after the post-processing chain (reading
// the back buffer when the chain is empty) and writes the presented frame: the swapchain, or the
// target of stdRenderBabylon_RenderPresentToTexture. It samples the source with a 1:1
// nearest-neighbour sampler at render scale 1 (exactly the pre-M9.4 present) and with a linear
// sampler otherwise, so the scaled scene resolves to the presented size. exposure is multiplied
// into the sampled color before the operator. Operator 0 adds no curve and clamps each color
// channel to 0..1; 1 is Khronos PBR Neutral (highlight compression with desaturation, midtones
// unchanged); 2 is the Narkowicz 2015 ACES fit. colorGrading is the preset applied after the
// operator and before brightness: 0 Neutral is the identity and skips the lookup, so the output is
// byte-identical; 1 Warm, 2 Cool, 3 Adventure, 4 Vivid and 5 Muted sample a 32^3 lookup table
// generated in code. brightness is the gamma applied after grading as
// pow(saturate(x), 1 / brightness); 1.0 is the identity and skips the pow. There is no transfer
// function in the pass, exactly as the plain present pass had none: the scene writes
// display-referred color today, so the output gamma of the defaults is today's. With exposure 1.0,
// operator 0, color grading 0 and brightness 1.0 it is a clamp, so the presented image matches the
// pre-M9.3 present for scene colors in 0..1.
struct StdRenderBabylonToneMap
{
    float exposure = 1.0f;
    std::uint32_t oper = 0;
    float brightness = 1.0f;
    std::uint32_t colorGrading = 0; // 0 Neutral, 1 Warm, 2 Cool, 3 Adventure, 4 Vivid, 5 Muted
};

// Sets the tone-map parameters. Refused, logging why and changing nothing, for a negative or
// non-finite exposure, a non-finite brightness at or below 0, an operator other than 0, 1 or 2, or
// a color-grading preset above 5. The defaults are exposure 1.0, operator 0, brightness 1.0 and
// color grading 0 Neutral. Opening also reads engine.renderer.toneMapping (defaults: 1 in Enhanced
// builds, 0 in Retail/QOL-off ones), engine.renderer.exposure (0.5..2.0, default 1.0),
// engine.renderer.brightness (0.6..1.8, default 1.0) and engine.renderer.colorGrading (0..5,
// default 0 in every build) from the config. Closing and reopening the frame owner restores those
// config values.
void stdRenderBabylon_SetToneMap(const StdRenderBabylonToneMap& params);
StdRenderBabylonToneMap stdRenderBabylon_GetToneMap();

// Appends a pass to the post-processing chain. The pass draws one full-screen triangle (the vertex
// shader of the present pass, Shaders/present.vert.hlsl, gives the fragment shader the texture
// coordinate in TEXCOORD0, (0, 0) at the top left) into a target of the back buffer's size and the
// scene color format (Rgba16F when the scene is), with no blending, depth or uniforms. Its fragment
// shader reads fragment texture slots 0 to textureCount - 1 with a nearest, clamping sampler: slot 0
// the previous pass's output (the back buffer color for the first pass), slot 1 the normal target,
// slot 2 the depth target. Passes run in
// the order added, at every presenting submit (stdRenderBabylon_SubmitFrame and
// stdRenderBabylon_PresentFrame, with or without a swapchain), never at stdRenderBabylon_FlushFrame.
// Returns false, logging why and changing nothing, when not open, for a shader that reads no texture,
// more than three or any uniform block, for one that reads depth on a device that can't sample it,
// and when the pipeline can't be created.
bool stdRenderBabylon_AddPostPass(const babylon::render::ShaderDesc& fragmentShader);

// Removes every pass of the chain and frees its targets; the present pass then reads the back buffer
// again. A no-op when not open. stdRenderBabylon_Close does it too.
void stdRenderBabylon_ClearPostPasses();

size_t stdRenderBabylon_GetNumPostPasses();

// What the final tone-map pass samples: the bloom composite after a presenting submit ran the bloom
// stage, else the light-shaft composite, the depth-of-field output, the output of the chain's last
// pass, else the back buffer color target (null without a back buffer). Its size is the scaled scene size (GetRenderWidth/Height). Its contents are those of
// the last presenting submit; tests read it back. With the default tone map it is the presented
// image up to the 0..1 clamp and the final pass's resample to the presented size.
babylon::render::TextureHandle stdRenderBabylon_GetPresentSource();

#ifdef J3D_TEST
// Test-only: makes the next stdRenderBabylon_Open use the pre-M9.3 presentation so a test can
// compare both paths in one run: a Bgra8 scene color target and the plain present pass, without the
// tone map. It resets after that Open, and production builds don't compile the declaration.
void stdRenderBabylon_ForceLegacyPresentationForTest(bool bLegacy);

// Added: test-only. Reads the 1 x 1 light-shaft centroid after a presenting submit and writes its
// mask weight in 0..1 and the sky position in image uv (0, 0 top left; 1, 1 bottom right). Returns
// false without a presented light-shaft or lens-flare frame. Production builds don't compile it.
bool stdRenderBabylon_GetLightShaftCentroidForTest(float* pU, float* pV, float* pWeight);

// Added: test-only. Reads the half-res light-shaft blur at a full-res pixel centre (the linear
// sample the composite uses) and writes it in red. Returns false without a presented shaft frame.
bool stdRenderBabylon_GetLightShaftValueForTest(std::uint32_t x, std::uint32_t y, float* pValue);

// Added: test-only. Writes the ambient-occlusion reconstruction's horizontal and vertical tangent
// scales for the current scene size and projection. Returns false when not open.
bool stdRenderBabylon_GetSsaoProjectionForTest(float* pTanHalfX, float* pTanHalfY);

// Added: test-only. Converts the fixed overhead contact-shadow light from the shader camera basis
// (right, up and forward in elements 0-2, 4-6 and 8-10) and writes the view-space direction toward
// the light. Returns false for a null matrix. Production builds don't compile it.
bool stdRenderBabylon_GetContactLightDirectionForTest(const float aCamera[12], float* pX,
    float* pY, float* pZ);
// Added: test-only. Reads the 1 x 1 light position and smoothed strength a presenting submit left,
// whether they came from the light-shaft stage or from the lens-flare stage's mask reduction.
// Returns false without such a submit. Production builds don't compile it.
bool stdRenderBabylon_GetLensFlareStateForTest(float* pU, float* pV, float* pStrength);

// Added: test-only. The number of textures the camera effects' scratch pool holds, idle or in use.
// Returns 0 when the postfx objects are unavailable. Production builds don't compile it.
std::size_t stdRenderBabylon_GetPostfxTargetCountForTest();
#endif

// Destroys texture now, or after the next submit while a frame is being recorded, since that frame
// may still name it. The null handle is ignored.
void stdRenderBabylon_DestroyTexture(babylon::render::TextureHandle texture);

// The same for a buffer.
void stdRenderBabylon_DestroyBuffer(babylon::render::BufferHandle buffer);

#endif // STD_STDRENDERBABYLON_H
