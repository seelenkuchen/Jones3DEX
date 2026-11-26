// Shared state, types and helpers of the Babylon std3D backend.
// Part of the Babylon column's std3D backend; see std3DBabylon.cpp for the overview. Adapter code: no Fixed/Added markers inside.
#pragma once

#include <std/General/std3D.h>
#include <std/General/stdColor.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace std3DBabylonInternal
{
    namespace render = babylon::render;

    // What the backend stores in the opaque tSysPixelFormat of StdTextureFormat::ddPixelFmt and
    // tSystemTexture::format, where std3DX9.c stored the D3DFORMAT.
    struct Std3DBabylonPixelFormat
    {
        uint8_t bValid;
        uint8_t format;  // render::TextureFormat
        uint8_t bOpaque; // the ColorInfo has no alpha, but the texture format has: alpha is set to 255
    };
    static_assert(sizeof(Std3DBabylonPixelFormat) <= sizeof(tSysPixelFormat), "Std3DBabylonPixelFormat must fit tSysPixelFormat");

    // The DX9 texture formats InitTextureFormats checks, in its order, that have a Babylon format with
    // the same bit layout (Texture.h): D3DFMT_X8R8G8B8 and D3DFMT_A8R8G8B8 are bytes B, G, R, A
    // (Bgra8; X8 reads as opaque), and the 16-bit ones match B5G6R5, B5G5R5A1 and B4G4R4A4 bit for
    // bit. D3DFMT_R8G8B8 is left out: no GPU API samples 24-bit texels. The luminance formats were
    // skipped by std3DX9.c too.
    struct Std3DBabylonFormatEntry
    {
        const ColorInfo* pColorInfo;
        render::TextureFormat format;
        bool bOpaque;
        const char* pName;
    };

    // The largest texture side reported when the device names none.
    inline constexpr uint32_t std3D_kDefaultMaxTextureSize = 4096u;

    // The primitives std3DX9.c drew: triangle lists with its default shader, line strips and point
    // lists with its wireframe shader.
    enum class Std3DBabylonPrimitive : uint8_t
    {
        Triangles,
        Lines,
        Points,
    };
    inline constexpr size_t std3D_kNumPrimitives = 3;

    // Fragment uniform block 0 of Shaders/std3d.frag.hlsl and std3d_wireframe.frag.hlsl.
    struct Std3DBabylonFragmentUniforms
    {
        float aFogParams[4]; // DX9's g_fogParams: start, end, 1 / (end - start), enabled
        float aFogColor[4];  // DX9's g_fogColor
        float aAlphaTest[4]; // D3DRS_ALPHAREF / 255, then unused
        float aSurfaceTag[4]; // Added: x is the normal target's alpha tag (M9.16), then unused
    };
    static_assert(sizeof(Std3DBabylonFragmentUniforms) == 64, "Std3DBabylonFragmentUniforms must match the shaders' block");

    // One draw of the scene's current pass, recorded into it when the pass ends. Its vertices and
    // indices are already streamed into vertexBuffer and indexBuffer, at firstVertex and firstIndex.
    struct Std3DBabylonDraw
    {
        Std3DBabylonPrimitive primitive;
        render::PipelineHandle pipeline;
        render::BufferHandle vertexBuffer;
        render::BufferHandle indexBuffer; // triangles only
        render::TextureHandle texture;    // triangles only
        render::SamplerHandle sampler;    // triangles only
        Std3DBabylonFragmentUniforms uniforms;
        bool bLit;                        // a lit draw, which reads the light buffer
        uint32_t firstVertex;
        uint32_t numVertices;
        uint32_t firstIndex;
        uint32_t numIndices;
    };

    extern bool std3D_bStartup;
    extern bool std3D_bOpen;
    extern bool std3D_bFindAllD3Devices;
    extern render::Device* std3D_pRenderDevice;

    extern size_t std3D_numDevices;
    extern size_t std3D_curDevice;
    extern Device3D* std3D_pCurDevice;
    extern Device3D std3D_aDevices[4];

    extern size_t std3D_numTextureFormats;
    extern StdTextureFormat std3D_aTextureFormats[8];
    extern bool std3D_bHasRGBTextureFormat;
    extern size_t std3D_RGBTextureFormat;
    extern size_t std3D_RGBATextureFormat;
    extern size_t std3D_RGBAKeyTextureFormat;

    extern size_t std3D_frameCount;
    extern size_t std3D_numCachedTextures;
    extern tSystemTexture* std3D_pFirstTexCache;
    extern tSystemTexture* std3D_pLastTexCache;

    extern Std3DRenderState std3D_renderState;
#ifdef J3D_TEST
    // Added: M11.2c. Vertices recorded this frame by draws carrying STD3D_RS_SHADOW_CASTER; reset at
    // std3D_StartScene.
    extern uint32_t std3D_casterVertexCount;
#endif
    extern Std3DMipmapFilterType std3D_mipmapFilter;
    extern int std3D_anisotropicFilter;
    extern bool std3D_bTexturePixelated;
    extern tSysTexture* std3D_pCurTex;
    extern float std3D_zDepth;
    extern float std3D_nearPlane;
    extern float std3D_farPlane;
    extern bool std3D_bRenderFog;
    extern float std3D_fogStartDepth;
    extern float std3D_fogEndDepth;
    extern float std3D_fogDepthFactor;
    extern float std3D_aFogColor[4];

    extern bool std3D_bInScene;
    extern bool std3D_bClearColorPending;
    extern bool std3D_bClearDepthPending;
    extern render::TextureHandle std3D_sceneColorTarget;

    extern render::PipelineHandle std3D_aPipelines[std3D_kNumPrimitives][2][2];
    extern render::PipelineHandle std3D_aLitPipelines[2][2];
    extern render::PipelineHandle std3D_aWaterPipelines[std3D_kNumPrimitives][2];
    extern render::PipelineHandle std3D_aLitWaterPipelines[2];
    extern render::SamplerHandle std3D_aSamplers[256];
    extern render::TextureHandle std3D_whiteTexture;
    extern render::BufferHandle std3D_vertexBuffer;
    extern render::BufferHandle std3D_indexBuffer;
    extern uint32_t std3D_vertexBufferSize;
    extern uint32_t std3D_indexBufferSize;
    extern uint32_t std3D_vertexBufferOffset;
    extern uint32_t std3D_indexBufferOffset;
    extern render::BufferHandle std3D_litVertexBuffer;
    extern uint32_t std3D_litVertexBufferSize;
    extern uint32_t std3D_litVertexBufferOffset;

    extern StdLight std3D_aLights[STD3D_MAXLIGHTS];
    extern size_t std3D_numLights;
    extern bool std3D_bLightsDirty;
    extern render::BufferHandle std3D_lightBuffer;

    extern render::LoadOp std3D_sceneColorLoad;
    extern render::LoadOp std3D_sceneDepthLoad;
    extern std::vector<Std3DBabylonDraw> std3D_aSceneDraws;

    extern bool std3D_bHudLayer;
    extern bool std3D_bScenePassBegun;
    extern bool std3D_bHudPassBegun;
    extern render::LoadOp std3D_hudColorLoad;
    extern render::LoadOp std3D_hudDepthLoad;

    extern float std3D_aShaderFogParams[4];
    extern float std3D_aShaderFogColor[4];

    Std3DBabylonPixelFormat std3D_GetPixelFormat(const tSysPixelFormat& sysFormat);
    bool std3D_SameLayout(const ColorInfo& a, const ColorInfo& b);
    uint32_t std3D_ReadPixel(const uint8_t* pSrc, size_t numBytes);
    void std3D_WritePixel(uint8_t* pDst, size_t numBytes, uint32_t value);
    const Std3DBabylonFormatEntry* std3D_FindFormatEntry(render::TextureFormat format, bool bOpaque);
    void std3D_InitTextureFormats();
    int std3D_NormalizeAnisotropicFilter(int level);
    render::SamplerHandle std3D_GetSampler(Std3DRenderState state);
    bool std3D_UpdateLightBuffer(render::CommandList& frame);
    void std3D_ResetTextureCacheInternal();
    bool std3D_InitDrawResources();
    void std3D_ReleaseDrawResources();

    // Added: M11.2e. The frame's shadow casters, in the postfx view convention (x right, y up,
    // z forward), three floats per triangle vertex. rdModel3 appends every face of the models of
    // SITH_TF_SHADOW things through std3D_AddShadowCasterPolygon, including the faces turned away
    // from the camera; std3D_StartScene clears the list. A polygon is added whole or not at all and
    // the list is capped, so one bad polygon cannot shift the ones after it.
    void std3DBabylon_ResetShadowCasters();
    const std::vector<float>& std3DBabylon_GetShadowCasterPositions();
}
