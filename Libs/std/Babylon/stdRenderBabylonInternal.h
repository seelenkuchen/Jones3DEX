// Private declarations shared by the Babylon render adapter's stage files.
// Part of the Babylon column's render frame owner; see stdRenderBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#pragma once

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "stdRenderBabylon.h"

namespace stdRenderBabylonInternal
{
    namespace render = babylon::render;

    // Added: SMAA 1x (0 off, 1 on) of the 3D scene, read when the frame owner opens.
    inline constexpr const char* stdRenderBabylon_kAntiAliasingKey = "engine.renderer.antiAliasing";

    // Added: contrast-adaptive sharpening strength (0 off to 1 strongest) of the presented 3D
    // image, read when the frame owner opens.
    inline constexpr const char* stdRenderBabylon_kSharpeningKey = "engine.renderer.sharpening";
    inline constexpr float stdRenderBabylon_kMaxSharpening = 1.0f;

    // Added: the final tone map's operator (0 clamp, 1 Neutral, 2 ACES fitted), exposure and
    // brightness gamma, read when the frame owner opens.
    inline constexpr const char* stdRenderBabylon_kToneMappingKey = "engine.renderer.toneMapping";
    inline constexpr const char* stdRenderBabylon_kExposureKey    = "engine.renderer.exposure";
    inline constexpr float stdRenderBabylon_kMinExposure = 0.5f;
    inline constexpr float stdRenderBabylon_kMaxExposure = 2.0f;
    inline constexpr const char* stdRenderBabylon_kBrightnessKey = "engine.renderer.brightness";
    inline constexpr float stdRenderBabylon_kMinBrightness = 0.6f;
    inline constexpr float stdRenderBabylon_kMaxBrightness = 1.8f;

    // Added: color grading is a preset (0 Neutral .. 5 Muted) applied in the tone-map pass after the
    // operator and before brightness, read when the frame owner opens. The lookup table is 32^3,
    // stored as a 1024 x 32 Rgba8 strip of 32 slices side by side. The parameters live in one table:
    // temperature shifts red up and blue down (positive is warm), saturation multiplies the distance
    // from luma, contrast blends toward a smoothstep S-curve, and the split-tone tints are added
    // weighted toward shadows and highlights. The tints fade to zero at black and white, so both stay
    // where they were. Preset 0 is the identity and the shader skips the lookup.
    inline constexpr const char* stdRenderBabylon_kColorGradingKey = "engine.renderer.colorGrading";
    inline constexpr uint32_t stdRenderBabylon_kColorGradingSize = 32;
    inline constexpr uint32_t stdRenderBabylon_kColorGradingStripWidth =
        stdRenderBabylon_kColorGradingSize * stdRenderBabylon_kColorGradingSize;
    struct StdRenderBabylonGradingPreset
    {
        const char* pName;
        float temperature;
        float saturation;
        float contrast;
        float aShadowTint[3];
        float aHighlightTint[3];
    };
    inline constexpr StdRenderBabylonGradingPreset stdRenderBabylon_aGradingPresets[] = {
        { "Neutral",   0.00f, 1.00f, 0.00f, {  0.000f,  0.000f,  0.000f }, {  0.000f,  0.000f,  0.000f } },
        { "Warm",      0.04f, 1.03f, 0.00f, {  0.000f,  0.000f,  0.000f }, {  0.000f,  0.000f,  0.000f } },
        { "Cool",     -0.04f, 1.00f, 0.00f, {  0.000f,  0.000f,  0.000f }, {  0.000f,  0.000f,  0.000f } },
        { "Adventure", 0.00f, 1.00f, 0.10f, { -0.015f,  0.010f,  0.020f }, {  0.020f,  0.010f, -0.020f } },
        { "Vivid",     0.00f, 1.12f, 0.05f, {  0.000f,  0.000f,  0.000f }, {  0.000f,  0.000f,  0.000f } },
        { "Muted",     0.00f, 0.80f, 0.00f, {  0.000f,  0.000f,  0.000f }, {  0.000f,  0.000f,  0.000f } },
    };
    inline constexpr uint32_t stdRenderBabylon_kMaxColorGrading =
        (uint32_t)(sizeof(stdRenderBabylon_aGradingPresets)
            / sizeof(stdRenderBabylon_aGradingPresets[0])) - 1;

    // Added: the built-in bloom stage. engine.renderer.bloom is 0 off, 1 low, 2 medium, 3 high; the
    // strengths live here in one place. The scene is display referred, so the soft-knee prefilter
    // starts just below white and lets bright values below 1.0 glow as well.
    inline constexpr const char* stdRenderBabylon_kBloomKey = "engine.renderer.bloom";
    inline constexpr float stdRenderBabylon_aBloomStrength[] = { 0.0f, 0.15f, 0.3f, 0.5f };
    inline constexpr int stdRenderBabylon_kBloomMaxLevel =
        (int)(sizeof(stdRenderBabylon_aBloomStrength) / sizeof(stdRenderBabylon_aBloomStrength[0])) - 1;
    inline constexpr int stdRenderBabylon_kBloomLevels = 5; // half, quarter, eighth, sixteenth, 1/32

    // Added: the built-in depth-of-field stage (M9.14). engine.renderer.dof.cutscenes and
    // engine.renderer.dof.gameplay are read when the frame owner opens; the strength scales the
    // gather radius and is clamped to 0.5..2.0. The circle of confusion is computed from the
    // linearised depth in the shader; the constants here are its shape and the gather's radius.
    // Gameplay blurs the background only: nothing nearer than the focus blurs, and the ramp starts
    // at three times the focus distance (with an absolute floor for a first-person camera whose
    // focus distance is nearly zero) over a span that keeps the ramp gentle. Cutscene mode ramps
    // from either side of the focus, with a small dead zone so the subject stays sharp.
    inline constexpr const char* stdRenderBabylon_kDofCutscenesKey = "engine.renderer.dof.cutscenes";
    inline constexpr const char* stdRenderBabylon_kDofGameplayKey  = "engine.renderer.dof.gameplay";
    inline constexpr const char* stdRenderBabylon_kDofStrengthKey  = "engine.renderer.dof.strength";
    inline constexpr float stdRenderBabylon_kMinDofStrength        = 0.5f;
    inline constexpr float stdRenderBabylon_kMaxDofStrength        = 2.0f;

    // Added: the built-in light-shaft stage (M9.16), after depth of field and before bloom.
    // engine.renderer.lightShafts is 0 off, 1 Low, 2 Medium, 3 High; the strengths live here in one
    // table. The mask keeps sky pixels above the luminance threshold (soft knee), the centroid is
    // the mean of (w, w * u, w * v) reduced to 1 x 1, and the shafts fade over the fade time when
    // the sky coverage is below the minimum, reaching full at the full-coverage threshold. The
    // radial blur's 40 taps and their decay are in lightshaft_blur.frag.hlsl. Values are
    // presentation only.
    inline constexpr const char* stdRenderBabylon_kLightShaftsKey = "engine.renderer.lightShafts";
    inline constexpr float stdRenderBabylon_aLightShaftStrength[] = { 0.0f, 0.25f, 0.5f, 0.8f };
    inline constexpr int stdRenderBabylon_kLightShaftsMaxLevel =
        (int)(sizeof(stdRenderBabylon_aLightShaftStrength) / sizeof(stdRenderBabylon_aLightShaftStrength[0])) - 1;

    // Added: the built-in screen-space ambient occlusion stage (M9.17b), after the water ripples and
    // before depth of field. engine.renderer.ssao is 0 off, 1 Low, 2 Medium, 3 High. Each level's
    // sample count, world-space radius, self-occlusion bias and intensity live here in one table;
    // the pass runs at half the scene size, is blurred by a 4 x 4 depth-aware bilateral filter and
    // composited at the scene size. Values are presentation only.
    //
    // The radii are on the game's own scale, not a metre-scale scene: Indy is about 0.2 world units
    // tall (model radius 0.092, collision height 0.08) and the gameplay camera sits about 0.21 units
    // behind him, so a metre-scale radius would darken several of his own heights of the scene around
    // him. These values keep one level's kernel within a hand-sized crease and let the range check
    // fade an occluder out long before another character's depth.
    inline constexpr const char* stdRenderBabylon_kSsaoKey = "engine.renderer.ssao";
    struct StdRenderBabylonSsaoLevel
    {
        int samples;
        float radius;    // world units
        float bias;      // world units
        float intensity; // 0 = no darkening, 1 = fully dark where occluded
    };
    inline constexpr StdRenderBabylonSsaoLevel stdRenderBabylon_aSsaoLevels[] = {
        {  0, 0.000f, 0.0000f, 0.00f },
        {  8, 0.020f, 0.0012f, 0.65f },
        { 12, 0.035f, 0.0018f, 0.80f },
        { 16, 0.050f, 0.0025f, 1.00f },
    };
    inline constexpr int stdRenderBabylon_kSsaoMaxLevel =
        (int)(sizeof(stdRenderBabylon_aSsaoLevels) / sizeof(stdRenderBabylon_aSsaoLevels[0])) - 1;

    // Added: the built-in camera motion-blur stage (M9.19), after the ambient-occlusion stage and
    // before depth of field. engine.renderer.motionBlur is 0 off, 1 Low, 2 Medium, 3 High. Each
    // level's tap count, shutter fraction (how much of the frame's motion the blur covers) and
    // maximum blur length as a fraction of the output height live here in one table, with the depth
    // falloff that downweights a much nearer tap. The previous camera is presentation state only; it
    // is reset at a cut, a pause, a teleport or a large snap, and the first frame after open has none.
    inline constexpr const char* stdRenderBabylon_kMotionBlurKey = "engine.renderer.motionBlur";
    struct StdRenderBabylonMotionBlurLevel
    {
        int taps;
        float shutter;      // fraction of the frame's motion the blur covers
        float maxLength;    // largest blur length as a fraction of the output height
        float depthFalloff; // how quickly a much nearer tap loses weight
    };
    inline constexpr StdRenderBabylonMotionBlurLevel stdRenderBabylon_aMotionBlurLevels[] = {
        {  0, 0.00f, 0.000f, 0.0f },
        {  8, 0.50f, 0.020f, 2.0f },
        { 12, 0.65f, 0.030f, 2.0f },
        { 16, 0.80f, 0.040f, 2.0f },
    };
    inline constexpr int stdRenderBabylon_kMotionBlurMaxLevel =
        (int)(sizeof(stdRenderBabylon_aMotionBlurLevels) / sizeof(stdRenderBabylon_aMotionBlurLevels[0])) - 1;
    inline constexpr float stdRenderBabylon_kMotionBlurTeleportDistance = 16.0f; // world units in one frame
    inline constexpr float stdRenderBabylon_kMotionBlurMaxRotation      = 45.0f; // degrees in one frame

    // Added: the built-in water-ripple stage (M9.17), after the post chain and before depth of field.
    // engine.renderer.water.ripples is a toggle. On water-tagged pixels the scene is sampled at a small
    // animated offset (a two-layer scrolling sine slope, phase from accumulated game time) and a faint
    // view-dependent highlight is added. The offset is a fraction of the output height, so its pixel
    // size follows the resolution; a sample that lands on a non-water pixel falls back to the
    // unoffset sample, so edges never smear geometry. The wave shape, speed, highlight and amplitude
    // all live here. Values are presentation only.
    inline constexpr const char* stdRenderBabylon_kWaterRipplesKey       = "engine.renderer.water.ripples";

    // Added: the M9.18 camera effects. Lens flare is a toggle; it reuses the light-shaft stage's sky
    // centroid and smoothed strength, so its constants sit with that stage's. The ghosts lie along
    // the line from the light through the screen centre: t is the fraction of that line (1 is the
    // centre), radius is in screen-height units and tint is the rgb the ghost adds. The halo is a
    // soft disc around the light. Vignette and film grain are toggles on the presented LDR image;
    // their tuning lives here too. All values are presentation only.
    inline constexpr const char* stdRenderBabylon_kLensFlareKey = "engine.renderer.lensFlare";
    inline constexpr const char* stdRenderBabylon_kVignetteKey  = "engine.renderer.vignette";
    inline constexpr const char* stdRenderBabylon_kFilmGrainKey = "engine.renderer.filmGrain";

    struct StdRenderBabylonState
    {
        render::Device* pDevice = nullptr;
        std::optional<render::CommandList> frame;

        render::TextureHandle colorTarget;
        render::TextureHandle depthTarget;
        render::TextureHandle normalTarget;

        // Added: the display-sized HUD layer std3D draws between BeginHudLayer and EndHudLayer. It is
        // composited over the tone-mapped scene before the menu and prompt overlays.
        render::TextureHandle hudColorTarget;
        render::TextureHandle hudNormalTarget;
        render::TextureHandle hudDepthTarget;
        bool bHudLayerDrawn = false; // the last submitted frame recorded a HUD pass

        render::TextureFormat colorFormat  = render::TextureFormat::Bgra8;
        render::TextureFormat depthFormat  = render::TextureFormat::D24S8;
        render::TextureFormat normalFormat = render::TextureFormat::R10G10B10A2;
        bool bDepthSampled = false; // the depth target is also Sampled, so post passes can read it
        uint32_t modeWidth  = 0;    // the display/video-mode size the game sees
        uint32_t modeHeight = 0;
        uint32_t width      = 0;    // the scene targets' size: mode size * render scale
        uint32_t height     = 0;
        float renderScale   = 1.0f;
        bool bScaleClampLogged = false; // one line per requested scale when the device limit clamps it

        // Added: SMAA 1x on the tone-mapped LDR image, before the HUD composite. Its three targets
        // are at the presented size (the window's drawable size, or the display size without one)
        // and are recreated when that size changes. bSmaaSetupFailed stops repeated log lines when
        // a pipeline or the precomputed textures can't be made.
        bool bAntiAliasing = false;
        bool bSmaaSetupFailed = false;
        render::TextureHandle smaaColorTarget;
        render::TextureHandle smaaEdgesTarget;
        render::TextureHandle smaaWeightsTarget;
        uint32_t smaaWidth  = 0;
        uint32_t smaaHeight = 0;
        render::PipelineHandle smaaToneMapPipeline;
        render::PipelineHandle smaaEdgePipeline;
        render::PipelineHandle smaaWeightsPipeline;
        render::PipelineHandle smaaBlendPipeline;        // to the swapchain
        render::PipelineHandle smaaBlendCapturePipeline; // to a Bgra8 capture target
        render::TextureHandle smaaAreaTexture;
        render::TextureHandle smaaSearchTexture;

        // Added: contrast-adaptive sharpening on the presented-size LDR image, after SMAA or the
        // tone map and before the HUD composite. casSourceTarget is that Rgba8 image; the CAS pass
        // reads it and writes the presented frame. bCasSetupFailed stops repeated log lines.
        float sharpening = 0.0f;
        bool bCasSetupFailed = false;
        render::TextureHandle casSourceTarget;
        uint32_t casWidth  = 0;
        uint32_t casHeight = 0;
        render::PipelineHandle casToneMapPipeline;    // the tone map into casSourceTarget
        render::PipelineHandle smaaBlendCasPipeline;  // SMAA's blend into casSourceTarget
        render::PipelineHandle casPipeline;           // to the swapchain
        render::PipelineHandle casCapturePipeline;    // to a Bgra8 capture target

        // Added: the M9.18 vignette and film grain on the presented-size LDR image, after SMAA and
        // sharpening and before the HUD composite. fxSourceTarget is the image the existing chain
        // leaves for the effect pass (Bgra8); the effect pass then writes the swapchain or a Bgra8
        // capture target. bFxSetupFailed stops repeated log lines. grainFrame advances with the
        // game time received through stdRenderBabylon_SetFrameTime, never with wall-clock time.
        bool bVignette      = false;
        bool bFilmGrain     = false;
        bool bFxSetupFailed = false;
        uint32_t grainFrame = 0;
        render::TextureHandle fxSourceTarget;
        uint32_t fxWidth  = 0;
        uint32_t fxHeight = 0;
        render::PipelineHandle fxPipeline;        // to the swapchain
        render::PipelineHandle fxCapturePipeline; // to a Bgra8 capture target
        render::PipelineHandle fxToneMapPipeline; // the tone map into fxSourceTarget (Bgra8)

        // Added: the built-in bloom stage before the tone map. The prefilter target is half the
        // scene size; downsamples go to 1/4, 1/8, 1/16 and 1/32, then additive tent upsamples run
        // back to 1/2, and a scene-sized composite adds the result to the scene source. All targets
        // are in the scene format and are recreated when the scene size or render scale changes.
        // bBloomRan tells PresentSource to read the composite after a presenting submit.
        float bloom = 0.0f;
        bool bBloomSetupFailed = false;
        bool bBloomRan = false;
        bool bBloomLogged = false; // one status line per open (or strength change) proves the stage runs
        render::TextureHandle bloomCompositeTarget;
        render::TextureHandle aBloomTargets[stdRenderBabylon_kBloomLevels];
        uint32_t bloomWidth  = 0; // the scene size the targets were made for
        uint32_t bloomHeight = 0;
        render::PipelineHandle bloomPrefilterPipeline;
        render::PipelineHandle bloomDownsamplePipeline;
        render::PipelineHandle bloomUpsamplePipeline;
        render::PipelineHandle bloomCompositePipeline;

        // Added: the built-in depth-of-field stage after the post chain and before bloom. The
        // half-res target holds the gather blur, the scene-sized target the composited result.
        // focusDistance is the smoothed input; focusTarget is what the game last asked for.
        bool bDofCutscenes = true;
        bool bDofGameplay  = true;
        float dofStrength  = 1.0f;
        bool bDofSetupFailed = false;
        bool bDofRan         = false;
        bool bDofLogged      = false;
        bool bHasFocus       = false;
        bool bFocusCutscene  = false;
        float focusTarget    = 0.0f;
        float focusDistance  = 0.0f;
        float focusNearPlane = 0.1f;
        float focusFarPlane  = 1000.0f;
        render::TextureHandle dofBlurTarget;
        render::TextureHandle dofColorTarget;
        uint32_t dofWidth  = 0;
        uint32_t dofHeight = 0;
        render::PipelineHandle dofGatherPipeline;
        render::PipelineHandle dofCompositePipeline;

        // Added: the built-in light-shaft stage after depth of field and before bloom. The mask and
        // the radial blur are half the scene size, the centroid is the mask reduced to 1 x 1, the
        // smoothed strength ping-pongs between two 1 x 1 targets, and the composite is scene sized.
        // shaftDeltaTime is the frame's game time, set by stdRenderBabylon_SetFrameTime; the stage
        // is presentation only.
        int lightShafts = 0; // 0 off, 1 Low, 2 Medium, 3 High
        float lightShaftStrength = 0.0f;
        bool bShaftsSetupFailed = false;
        bool bShaftsRan         = false;
        bool bShaftsLogged      = false;
        bool bShaftStrengthValid = false;
        int shaftStrengthIndex  = 0;
        float shaftDeltaTime    = 0.0f;
        render::TextureHandle shaftMaskTarget;
        render::TextureHandle shaftBlurTarget;
        render::TextureHandle shaftColorTarget;
        std::vector<render::TextureHandle> aShaftReduceTargets;
        render::TextureHandle aShaftStrengthTargets[2];
        render::TextureFormat shaftFormat = render::TextureFormat::Rgba16F;
        uint32_t shaftWidth  = 0; // the scene size the targets were made for
        uint32_t shaftHeight = 0;
        render::PipelineHandle shaftMaskPipeline;
        render::PipelineHandle shaftReducePipeline;
        render::PipelineHandle shaftStrengthPipeline;
        render::PipelineHandle shaftBlurPipeline;
        render::PipelineHandle shaftCompositePipeline;
        // The mask reduced to 1 x 1 and the smoothed strength most recently written. The lens-flare
        // stage reads both; light shafts write them too, so the flare needs no readback.
        render::TextureHandle shaftCentroidTarget;
        render::TextureHandle shaftStrengthTarget;

        // Added: the built-in lens-flare stage after light shafts and before bloom. bLensFlare is
        // the option; flareColorTarget is the scene-sized HDR result that bloom then reads.
        // bFlareRan tells SceneSource to return it.
        bool bLensFlare        = false;
        bool bFlareSetupFailed = false;
        bool bFlareRan         = false;
        bool bFlareLogged      = false;
        render::TextureHandle flareColorTarget;
        uint32_t flareWidth  = 0;
        uint32_t flareHeight = 0;
        render::PipelineHandle flarePipeline;

        // Added: the built-in screen-space ambient occlusion stage after the water ripples and
        // before depth of field. The raw and blurred occlusion terms are half the scene size, the
        // composite is scene sized. The stage reads the depth and normal targets; it needs a
        // sampled depth target. ssao is 0 off, 1 Low, 2 Medium, 3 High.
        int ssao = 0;
        bool bSsaoSetupFailed = false;
        bool bSsaoRan         = false;
        bool bSsaoLogged      = false;
        render::TextureHandle ssaoRawTarget;
        render::TextureHandle ssaoBlurTarget;
        render::TextureHandle ssaoColorTarget;
        render::TextureFormat ssaoFormat = render::TextureFormat::Rgba16F;
        uint32_t ssaoWidth  = 0; // the scene size the targets were made for
        uint32_t ssaoHeight = 0;
        render::PipelineHandle ssaoPipeline;
        render::PipelineHandle ssaoBlurPipeline;
        render::PipelineHandle ssaoCompositePipeline;

        // Added: the built-in camera motion-blur stage after ambient occlusion and before depth of
        // field. motionBlur is 0 off, 1 Low, 2 Medium, 3 High. The camera transforms are in the
        // shader's view convention (x right, y up, z forward), with the position in the w of each
        // basis float4; aLastCameraMatrix is the previous frame's for the jump and snap checks.
        int motionBlur = 0;
        bool bMotionBlurSetupFailed = false;
        bool bMotionBlurRan         = false;
        bool bMotionBlurLogged      = false;
        bool bMotionBlurActive      = false; // this frame may blur (no cut, pause, teleport or snap)
        bool bHasCameraTransform    = false;
        float aLastCameraMatrix[12] = {};
        float aMotionBlurCurrent[12] = {};
        float aMotionBlurPrevious[12] = {};
        render::TextureHandle motionBlurColorTarget;
        uint32_t motionBlurWidth  = 0; // the scene size the target was made for
        uint32_t motionBlurHeight = 0;
        render::PipelineHandle motionBlurPipeline;

        // Added: the frame's projection for the ambient-occlusion reconstruction, set by
        // stdRenderBabylon_SetProjection from std3D_SetProjection. The fov is in degrees and is the
        // horizontal fov at the 4:3 reference aspect, as the rdCamera's is.
        float projectionFovDegrees = 90.0f;
        float projectionNearPlane  = 1.0f / 64.0f;
        float projectionFarPlane   = 64.0f;

        // Added: the built-in water-ripple stage after the post chain and before depth of field. Its
        // one scene-sized target holds the offset result; waterTime is the accumulated game time the
        // wave phase follows, set by stdRenderBabylon_SetFrameTime. bWaterRan tells the depth-of-field
        // stage and the later scene stages to read the rippled image.
        bool bWaterRipples   = false; // set from engine.renderer.water.ripples when the config has started
        bool bWaterSetupFailed = false;
        bool bWaterRan       = false;
        bool bWaterLogged    = false;
        float waterTime      = 0.0f;
        render::TextureHandle waterColorTarget;
        uint32_t waterWidth  = 0; // the scene size the target was made for
        uint32_t waterHeight = 0;
        render::PipelineHandle waterRipplesPipeline;

        // The final pass's tone map, and whether it runs the tone-map shader (false only when a test
        // forced the pre-M9.3 path). Opening reads the operator and exposure from the config.
        StdRenderBabylonToneMap toneMap;
        bool bToneMap = true;

        // Added: the 1024 x 32 Rgba8 color-grading strip the tone-map pass samples at texture slot 1,
        // and the preset it was generated for. Re-uploaded when the preset changes.
        render::TextureHandle colorGradingLut;
        uint32_t colorGradingLutPreset = ~0u;
        bool bColorGradingLutFailed = false;

        // Changes whenever the back buffer's contents may change (stdRenderBabylon_GetBackBufferVersion).
        uint64_t backBufferVersion = 0;

        render::PipelineHandle presentPipeline;
        render::PipelineHandle capturePipeline; // Bgra8, for RenderPresentToTexture
        render::PipelineHandle hudPipeline;     // Added: HUD composite to the swapchain
        render::PipelineHandle hudCapturePipeline; // Added: HUD composite to a Bgra8 capture target
        render::SamplerHandle presentSampler; // also the post passes' sampler; nearest, clamping
        render::SamplerHandle finalSampler;   // linear, clamping: the final pass at a render scale != 1

        // The pass a caller records on top of the presented frame, and the
        // offscreen target of stdRenderBabylon_RenderPresentToTexture.
        StdRenderBabylonPresentOverlay presentOverlay = nullptr;
        void* presentOverlayUser = nullptr;

        // Added: the gameplay HUD overlay, drawn before the present overlay so a menu can cover it.
        StdRenderBabylonPresentOverlay hudOverlay = nullptr;
        void* hudOverlayUser = nullptr;

        // The post-processing chain: one pipeline per pass, in order, with the number of inputs its
        // shader reads, and the two targets the passes write in turn (made at the first presenting
        // submit that needs them, freed with the back buffer).
        struct PostPass
        {
            render::PipelineHandle pipeline;
            uint32_t numInputs = 0;
        };
        std::vector<PostPass> aPostPasses;
        render::TextureHandle aPostTargets[2];

        std::vector<render::TextureHandle> aDeferredTextures;
        std::vector<render::BufferHandle> aDeferredBuffers;
    };

    extern StdRenderBabylonState stdRenderBabylon_state;

    // The frame owner and the shared conversion, sampler and scene-source helpers.
    float stdRenderBabylon_HalfToFloat(uint16_t half);
    uint16_t stdRenderBabylon_FloatToHalf(float value);
    uint8_t stdRenderBabylon_ToUnorm8(float value);
    const render::ShaderDesc& stdRenderBabylon_FinalFrag();
    void stdRenderBabylon_DestroyDeferred();
    bool stdRenderBabylon_EnsurePresentSampler();
    bool stdRenderBabylon_EnsureFinalSampler();
    bool stdRenderBabylon_DrawsBackBuffer(const render::CommandList& list);
    void stdRenderBabylon_ResampleBgra(const uint8_t* pSrc, uint32_t srcWidth, uint32_t srcHeight,
        uint8_t* pDst, uint32_t dstWidth, uint32_t dstHeight);

    // The stage targets' free functions, called when the back buffer is freed.
    void stdRenderBabylon_FreeSmaaTargets();
    void stdRenderBabylon_FreeCasTarget();
    void stdRenderBabylon_FreeFxTarget();
    void stdRenderBabylon_FreeBloomTargets();
    void stdRenderBabylon_FreeDoFTargets();
    void stdRenderBabylon_FreeWaterTargets();
    void stdRenderBabylon_FreeSsaoTargets();
    void stdRenderBabylon_FreeMotionBlurTarget();
    void stdRenderBabylon_FreeShaftTargets();
    void stdRenderBabylon_FreeFlareTargets();
    void stdRenderBabylon_FreePostTargets();

    // The sharpening and camera-effect resources the presenting submit makes.
    bool stdRenderBabylon_EnsureCasTarget(uint32_t width, uint32_t height);
    bool stdRenderBabylon_EnsureCasToneMapPipeline();
    bool stdRenderBabylon_EnsureFxTarget(uint32_t width, uint32_t height);
    bool stdRenderBabylon_EnsureFxToneMapPipeline();
    bool stdRenderBabylon_EnsureFxPipeline(bool bSwapchain, render::TextureFormat format);

    // The stage record functions the presenting submit and the capture call.
    bool stdRenderBabylon_RecordSmaa(render::CommandList& list, render::TextureHandle target,
        render::TextureFormat format, uint32_t width, uint32_t height, bool bCasSource);
    bool stdRenderBabylon_RecordSharpening(render::CommandList& list, render::TextureHandle source,
        render::TextureHandle target, render::TextureFormat format, uint32_t width, uint32_t height);
    bool stdRenderBabylon_RecordCameraFx(render::CommandList& list, render::TextureHandle source,
        render::TextureHandle target, render::TextureFormat format, uint32_t width, uint32_t height);
    bool stdRenderBabylon_RecordBloom(render::CommandList& list);
    bool stdRenderBabylon_RecordDoF(render::CommandList& list);
    bool stdRenderBabylon_RecordWaterRipples(render::CommandList& list);
    bool stdRenderBabylon_RecordSsao(render::CommandList& list);
    bool stdRenderBabylon_RecordMotionBlur(render::CommandList& list);
    bool stdRenderBabylon_RecordShaftMaskCentroid(render::CommandList& list);
    bool stdRenderBabylon_RecordShaftComposite(render::CommandList& list);
    bool stdRenderBabylon_RecordLensFlare(render::CommandList& list);
    void stdRenderBabylon_RecordPostChain(render::CommandList& list);
    bool stdRenderBabylon_RecordFinalPass(render::CommandList& list, render::PipelineHandle pipeline,
        render::TextureHandle target);
    bool stdRenderBabylon_RecordHudComposite(render::CommandList& list, render::TextureHandle target,
        render::TextureFormat format);
    bool stdRenderBabylon_DrawsHudLayer(const render::CommandList& list);

    // The source helpers and the SSAO projection the stages share.
    render::TextureHandle stdRenderBabylon_ChainSource();
    render::TextureHandle stdRenderBabylon_SceneSource();
    render::TextureHandle stdRenderBabylon_PresentSource();
    void stdRenderBabylon_SsaoProjection(uint32_t width, uint32_t height, float* pTanHalfX,
        float* pTanHalfY);
    bool stdRenderBabylon_EnsureColorGradingLut();
    render::PipelineHandle stdRenderBabylon_CreateHudPipeline(render::TextureFormat format);

#ifdef J3D_TEST
    extern bool stdRenderBabylon_bLegacyPresentationForTest;
    float stdRenderBabylon_ReadShaftTexel(const std::vector<std::byte>& texels,
        render::TextureFormat format, size_t index, size_t channel = 0);
#endif
}
