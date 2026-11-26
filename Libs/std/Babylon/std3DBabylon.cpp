// Added: the Babylon column's std3D backend on the Babylon render device of the adapter context
// (ADR-0025), replacing std/Null/std3DNull.c in that column. It defines the std3DBackend_* functions
// through stdRenderBackend.h, so the capture forwarders of std3DCapture.c stay in front (ADR-0024).
// The behavior reference is the removed DirectX 9 backend (std/Win95/DX9/std3DX9.c before 82b8f607):
// the device list, texture formats, system textures, the texture cache with its frame counts, the
// fog and projection state and the scene clears follow it, with Babylon textures where it held
// Direct3D ones. Frames go through stdRenderBabylon.cpp, which owns the frame's command list and
// the back buffer; the display adapter (stdDisplayBabylon.cpp) opens it with the display, sizes the
// back buffer with the video mode and submits the frame at stdDisplay_Update, as std3DX9.c drew into
// the display's device and stdDisplayDX9.c presented it.
//
// Draws follow std3DX9.c's shader path (std3DX9.c with stdShaderDX9.c and Shaders/*.hlsl), which
// ran on every Direct3D 9 device with shader model 3: the shaders are ports of its default and
// wireframe shaders (Shaders/std3d*.hlsl), and the Direct3D state its SetRenderState and
// InitRenderState set becomes pipelines (blending, depth, primitive), samplers (filter, mip filter,
// anisotropy, clamp) and uniforms (viewport, fog, alpha reference), worked out from the render
// state bits at each draw (std3D_GetPipeline, std3D_GetSampler, std3D_GetFragmentUniforms).
//
// std3D_DrawRenderListLit draws like std3D_DrawRenderList, with the lit shaders (Shaders/std3d_lit*.hlsl):
// they compute the color exactly as the default shaders do and write each fragment's interpolated
// view-space normal into the normal target, where the default shaders write a constant. Lit vertices
// (StdVertexLit, 56 bytes) stream into a vertex buffer of their own, made on the first lit draw, so the
// classic draws' device calls stay what they were.
//
// std3D_SetLights keeps the frame's light list (StdLight, 48 bytes each) for the lit draws. A pass
// with lit draws, or the first pass after the list changed, copies it into the light buffer, a
// storage buffer made on first use (std3D_kLightBufferSize: a 16-byte header with the count, then
// up to STD3D_MAXLIGHTS lights as they are), and the pass binds it to fragment storage slot 0 of
// the lit draws. std3d_lit.frag.hlsl reads it for the vertices per-pixel lighting flags (M10.2);
// a Classic vertex adds nothing, so its pixel is unchanged. Without lit draws and std3D_SetLights
// (the litFaces option off) there is no light buffer and the classic draws' device calls stay what
// they were.
//
// A scene is one or more passes on the back buffer. CommandList::updateBuffer can't be recorded
// inside a pass, so a draw streams its vertices and indices into the frame's vertex and index
// buffer right away, at growing offsets like std3DX9.c's buffer path, and the pass that draws them
// is recorded when it ends: at std3D_EndScene and at a std3D_ClearZBuffer inside the scene.
//
// Adapter code: no Fixed/Added markers inside; divergences from std3DX9.c say so where they are.
#include <std/General/stdRenderBackend.h>
#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>

#include <std/General/std.h>
#include <std/General/stdColor.h>
#include <std/General/stdConfig.h>
#include <std/General/stdEffect.h>
#include <std/General/stdMath.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include "std3DBabylon.h"
#include "stdBabylon.h"
#include "stdRenderBabylon.h"
#include "std3DBabylonInternal.h"

namespace render = babylon::render;
using namespace std3DBabylonInternal;

float std3D_g_fogDensity   = 0.0f;
size_t std3D_g_maxVertices = 0u;

namespace std3DBabylonInternal
{
    bool std3D_bStartup          = false;
    bool std3D_bOpen             = false;
    bool std3D_bFindAllD3Devices = false;
    render::Device* std3D_pRenderDevice = nullptr;

    size_t std3D_numDevices      = 0;
    size_t std3D_curDevice       = 0;
    Device3D* std3D_pCurDevice   = nullptr;
    Device3D std3D_aDevices[4];
}

namespace
{
    // The DX9 BuildDeviceList with the render device in place of Direct3D: one device, its limits
    // from the render device, its memory from the display device as std3DX9.c took it.
    int std3D_BuildDeviceList()
    {
        std3D_numDevices = 0;

        StdDisplayDevice displayDevice;
        STD_ZEROMEM(&displayDevice, sizeof(displayDevice));
        if ( !stdDisplay_IsOpen() || stdDisplay_GetCurrentDevice(&displayDevice) )
        {
            return 0;
        }

        if ( !std3D_bFindAllD3Devices && !displayDevice.bHAL )
        {
            return 0;
        }

        const render::DeviceInfo& info = std3D_pRenderDevice->info();
        const bool bHasZBuffer = std3D_pRenderDevice->supportsFormat(render::TextureFormat::D24S8, render::TextureUsage::DepthTarget)
            || std3D_pRenderDevice->supportsFormat(render::TextureFormat::D32F, render::TextureUsage::DepthTarget);
        const uint32_t maxTexSize = info.limits.maxTextureDimension ? info.limits.maxTextureDimension : std3D_kDefaultMaxTextureSize;

        Device3D* pDriver = &std3D_aDevices[0];
        STD_ZEROMEM(pDriver, sizeof(*pDriver));

        stdUtil_Format(pDriver->deviceDescription, STD_ARRAYLEN(pDriver->deviceDescription), "%s (Babylon render, %s)",
            displayDevice.aDeviceName, info.backend.c_str());
        stdUtil_Format(pDriver->deviceName, STD_ARRAYLEN(pDriver->deviceName), "%s", displayDevice.aDriverName);

        pDriver->bHAL                           = displayDevice.bHAL;
        pDriver->bTexturePerspectiveSupported   = 1;
        pDriver->hasZBuffer                     = bHasZBuffer;
        pDriver->bSqareOnlyTexture              = 0;
        pDriver->bAlphaTextureSupported         = 1;
        pDriver->bColorkeyTextureSupported      = 1;
        pDriver->bStippledShadeSupported        = 0;
        pDriver->minTexWidth                    = 1;
        pDriver->minTexHeight                   = 1;
        pDriver->maxTexWidth                    = maxTexSize;
        pDriver->maxTexHeight                   = maxTexSize;
        pDriver->bAnisotropicFilteringSupported = 1; // Added: render::SamplerDesc::maxAnisotropy
        pDriver->bMipmapAutoGenSupported        = 1; // Added: single-level materials get a mip chain on upload
        pDriver->bMSAASupported                 = false; // nor multisampled targets
        pDriver->bAlphaBlendSupported           = 1;
        pDriver->maxVertexCount                 = 65535u; // 16-bit indices

        pDriver->totalMemory     = displayDevice.totalVideoMemory;
        pDriver->availableMemory = displayDevice.freeVideoMemory;

        std3D_numDevices = 1u;

        STDLOG_STATUS("Found |%s|%s|%s|%s| Babylon render device\n",
            pDriver->hasZBuffer ? "Z" : "Non-Z",
            pDriver->bAlphaTextureSupported ? "Alpha" : "No Alpha",
            pDriver->bStippledShadeSupported ? "Stippled" : "Blend",
            pDriver->bColorkeyTextureSupported ? "Colorkey" : "No Colorkey"
        );
        STDLOG_STATUS("Description: %s [%s]\n", pDriver->deviceName, pDriver->deviceDescription);
        return 1;
    }

    void std3D_ReleaseSystemResources()
    {
        std3D_ResetTextureCacheInternal();
        std3D_ReleaseDrawResources();
        std3D_sceneColorTarget   = {};
        std3D_bInScene           = false;
        std3D_bClearColorPending = false;
        std3D_bClearDepthPending = false;
        std3D_bHudLayer          = false;
        std3D_bScenePassBegun    = false;
        std3D_bHudPassBegun      = false;
        std3D_aSceneDraws.clear();
    }

    // The DX9 InitSystem minus the Direct3D state: formats, the back buffer for the viewport, the
    // cache, the render state and the texture memory.
    bool std3D_InitSystem()
    {
        std3D_InitTextureFormats();
        if ( !std3D_numTextureFormats || !std3D_bHasRGBTextureFormat )
        {
            STDLOG_ERROR("Error no texture formats found.\n");
            return false;
        }

        // std3DX9.c's CreateViewport: the viewport is the display's back buffer, cleared to black
        // with Z 1. The clear happens in the first pass on it.
        uint32_t width  = 0;
        uint32_t height = 0;
        stdDisplay_GetBackBufferSize(&width, &height);
        if ( !stdRenderBabylon_SetBackBufferSize(width, height, nullptr) )
        {
            STDLOG_ERROR("Error creating viewport.\n");
            return false;
        }

        std3D_bClearColorPending = true;
        std3D_bClearDepthPending = true;
        std3D_sceneColorTarget   = stdRenderBabylon_GetColorTarget();

        std3D_g_maxVertices = std3D_pCurDevice->maxVertexCount;
        if ( std3D_g_maxVertices == 0 )
        {
            std3D_g_maxVertices = STD3D_MAXVERTICES;
        }
        STDLOG_STATUS("Max vertices: %zu.\n", std3D_g_maxVertices);

        std3D_frameCount        = 1;
        std3D_numCachedTextures = 0;
        std3D_pFirstTexCache    = nullptr;
        std3D_pLastTexCache     = nullptr;

        std3D_RGBTextureFormat     = std3D_FindClosestFormat(&stdColor_cfBGR8888);
        std3D_RGBAKeyTextureFormat = std3D_FindClosestFormat(&stdColor_cfRGBA8888);
        std3D_RGBATextureFormat    = std3D_FindClosestFormat(&stdColor_cfRGBA8888);

        if ( !std3D_InitDrawResources() )
        {
            return false;
        }

        // std3DX9.c's InitRenderState: the trilinear mip filter, linear filtering, wrapping,
        // shader fog off. The anisotropic level and the crisp-pixels toggle are presentation state
        // read from Jones.cfg here, like the render scale is read when the frame owner opens.
        std3D_mipmapFilter = STD3D_MIPMAPFILTER_TRILINEAR;
        std3D_anisotropicFilter  = std3DBabylon_ReadAnisotropicFilterLevel();
        std3D_bTexturePixelated  = std3DBabylon_ReadTexturePixelated();
        std3D_renderState  = (Std3DRenderState)(STD3D_RS_UNKNOWN_1 | STD3D_RS_TEXFILTER_BILINEAR | STD3D_RS_SUBPIXEL_CORRECTION | STD3D_RS_UNKNOWN_2);
        std3D_pCurTex      = nullptr;
        STD_ZEROMEM(std3D_aShaderFogParams, sizeof(std3D_aShaderFogParams));

        if ( stdDisplay_GetTextureMemory(&std3D_pCurDevice->totalMemory, &std3D_pCurDevice->availableMemory) )
        {
            return false;
        }

        return true;
    }
}

int std3DBabylon_NormalizeAnisotropicFilterLevel(int level)
{
    return std3D_NormalizeAnisotropicFilter(level);
}

int std3DBabylon_ReadAnisotropicFilterLevel(void)
{
    const int defaultLevel = J3D_QOL_VALUE(8, 1);
    if ( !stdConfig_HasStarted() )
    {
        return defaultLevel;
    }

    // An integer value wins. stdConfig_GetInt returns its default for a value of another JSON
    // type, so an old boolean Jones.cfg is found by asking for both booleans of it: a real
    // boolean answers both with itself, while a missing or integer value answers each with its
    // own default and the two differ.
    const int missing = std::numeric_limits<int>::min();
    const int level   = stdConfig_GetInt(STD3D_CFG_ANISOTROPICFILTER, missing);
    if ( level != missing )
    {
        return std3D_NormalizeAnisotropicFilter(level);
    }

    const bool bTrue  = stdConfig_GetBool(STD3D_CFG_ANISOTROPICFILTER, true);
    const bool bFalse = stdConfig_GetBool(STD3D_CFG_ANISOTROPICFILTER, false);
    if ( bTrue == bFalse )
    {
        return bTrue ? 8 : 1;
    }

    return defaultLevel;
}

bool std3DBabylon_ReadTexturePixelated(void)
{
    return stdConfig_HasStarted() ? stdConfig_GetBool(STD3D_CFG_TEXTUREPIXELATED, false) : false;
}

#ifdef J3D_TEST
void std3DBabylon_SetAnisotropicFilterForTest(int level)
{
    std3D_anisotropicFilter = std3D_NormalizeAnisotropicFilter(level);
}

void std3DBabylon_SetTexturePixelatedForTest(bool bEnabled)
{
    std3D_bTexturePixelated = bEnabled;
}
#endif

void std3D_ResetGlobals(void)
{
    float fogDensity = 1.0f;
    std::memcpy(&std3D_g_fogDensity, &fogDensity, sizeof(std3D_g_fogDensity));
    STD_ZEROMEM(&std3D_g_maxVertices, sizeof(std3D_g_maxVertices));
}

int std3D_Startup(void)
{
    // std3DX9.c asserted that it wasn't started; a second startup is answered like std3DNull.c does.
    if ( std3D_bStartup )
    {
        return 1;
    }

    STD_ZEROMEM(std3D_aTextureFormats, sizeof(std3D_aTextureFormats));
    STD_ZEROMEM(std3D_aDevices, sizeof(std3D_aDevices));

    StdBabylonContext* pContext = stdBabylon_GetContext();
    std3D_pRenderDevice = pContext ? pContext->pRenderDevice : nullptr;
    if ( !std3D_pRenderDevice )
    {
        STDLOG_ERROR("No render device in the Babylon context.\n");
        return 0;
    }

    if ( !std3D_BuildDeviceList() )
    {
        STDLOG_ERROR("Error building device list.\n");
        std3D_pRenderDevice = nullptr;
        return 0;
    }

    if ( std3D_numDevices == 0 )
    {
        std3D_pRenderDevice = nullptr;
        return 0;
    }

    std3D_bStartup = true;
    return 1;
}

void std3D_Shutdown(void)
{
    if ( !std3D_bStartup )
    {
        STDLOG_WARNING("Warning: System already shutdown!\n");
        return;
    }

    if ( std3D_bOpen )
    {
        std3D_Close();
    }

    STD_ZEROMEM(std3D_aTextureFormats, sizeof(std3D_aTextureFormats));
    STD_ZEROMEM(std3D_aDevices, sizeof(std3D_aDevices));

    std3D_pRenderDevice = nullptr;
    std3D_numDevices    = 0;
    std3D_bStartup      = false;
}

const Device3D* std3D_GetCurrentDevice(void)
{
    return std3D_pCurDevice;
}

size_t std3D_GetNumDevices(void)
{
    return std3D_numDevices;
}

const Device3D* std3D_GetAllDevices(void)
{
    return std3D_aDevices;
}

void J3DAPI std3D_SetFindAllDevices(int bFindAll)
{
    std3D_bFindAllD3Devices = bFindAll != 0;
}

int J3DAPI std3D_Open(size_t deviceNum)
{
    STD_ASSERTREL(std3D_bStartup);
    if ( std3D_bOpen )
    {
        STDLOG_ERROR("Warning: System already open!\n");
        return 0;
    }

    if ( !stdDisplay_IsOpen() )
    {
        STDLOG_ERROR("std3D_Open: Display system is not opened!\n");
        return 0;
    }

    if ( deviceNum >= std3D_numDevices )
    {
        return 0;
    }

    std3D_curDevice  = deviceNum;
    std3D_pCurDevice = &std3D_aDevices[deviceNum];

    // The display opened the frame owner on the same device (stdDisplay_Open).
    if ( stdRenderBabylon_GetDevice() != std3D_pRenderDevice )
    {
        STDLOG_ERROR("std3D_Open: the display does not draw with the render device of the context.\n");
        std3D_pCurDevice = nullptr;
        std3D_curDevice  = 0;
        return 0;
    }

    if ( !std3D_InitSystem() )
    {
        STDLOG_ERROR("Failed to initialize system, closing...\n");
        std3D_ReleaseSystemResources();
        std3D_numTextureFormats    = 0;
        std3D_bHasRGBTextureFormat = false;
        std3D_pCurDevice           = nullptr;
        std3D_curDevice            = 0;
        return 0;
    }

    size_t memFree  = 0;
    size_t memTotal = 0;
    stdDisplay_GetTotalMemory(&memTotal, &memFree);

    STDLOG_STATUS("Texture Ram  Total: %zu bytes  Free: %zu bytes.\n", std3D_pCurDevice->totalMemory, std3D_pCurDevice->availableMemory);
    STDLOG_STATUS("Video Ram Total: %zu bytes  Free: %zu bytes.\n", memTotal, memFree);

    std3D_bOpen = true;
    return 1;
}

void std3D_Close(void)
{
    if ( !std3D_bOpen )
    {
        STDLOG_WARNING("Warning: System already closed!\n");
        return;
    }

    std3D_ReleaseSystemResources();

    std3D_mipmapFilter         = (Std3DMipmapFilterType)-1;
    std3D_numTextureFormats    = 0;
    std3D_curDevice            = 0;
    std3D_pCurDevice           = nullptr;
    std3D_bHasRGBTextureFormat = false;
    std3D_bOpen                = false;
}

StdDisplayEnvironment* J3DAPI std3D_BuildDisplayEnvironment()
{
    StdDisplayEnvironment* pDeviceList = (StdDisplayEnvironment*)STDMALLOC(sizeof(StdDisplayEnvironment));
    STD_ZEROMEM(pDeviceList, sizeof(StdDisplayEnvironment));

    if ( !stdDisplay_Startup() )
    {
        STDLOG_ERROR("Error starting stdDisplay system.\n");
        std3D_FreeDisplayEnvironment(pDeviceList);
        return nullptr;
    }

    pDeviceList->numInfos      = stdDisplay_GetNumDevices();
    pDeviceList->aDisplayInfos = nullptr;

    if ( pDeviceList->numInfos )
    {
        pDeviceList->aDisplayInfos = (StdDisplayInfo*)STDMALLOC(sizeof(StdDisplayInfo) * pDeviceList->numInfos);

        StdDisplayInfo* pCurInfo = pDeviceList->aDisplayInfos;
        for ( size_t deviceNum = 0; deviceNum < pDeviceList->numInfos; ++deviceNum )
        {
            STD_ZEROMEM(pCurInfo, sizeof(StdDisplayInfo));

            if ( stdDisplay_GetDevice(deviceNum, &pCurInfo->displayDevice) )
            {
                STDLOG_ERROR("Error getting stdDisplay device.\n");
                std3D_FreeDisplayEnvironment(pDeviceList);
                return nullptr;
            }

            if ( !stdDisplay_Open(deviceNum) )
            {
                STDLOG_ERROR("Error opening stdDisplay device.\n");
                std3D_FreeDisplayEnvironment(pDeviceList);
                return nullptr;
            }

            pCurInfo->numModes = stdDisplay_GetNumVideoModes();
            pCurInfo->aModes   = nullptr;

            if ( pCurInfo->numModes )
            {
                pCurInfo->aModes = (StdVideoMode*)STDMALLOC(sizeof(StdVideoMode) * pCurInfo->numModes);
                StdVideoMode* pCurMode = pCurInfo->aModes;
                for ( size_t modeNum = 0; modeNum < pCurInfo->numModes; ++modeNum )
                {
                    if ( !stdDisplay_GetVideoMode(modeNum, pCurMode) )
                    {
                        ++pCurMode;
                    }
                }

                pCurInfo->numDevices = 0;
                pCurInfo->aDevices   = nullptr;
                if ( pCurInfo->displayDevice.bHAL )
                {
                    if ( !std3D_Startup() )
                    {
                        STDLOG_ERROR("Error starting std3D system.\n");
                        std3D_FreeDisplayEnvironment(pDeviceList);
                        return nullptr;
                    }

                    pCurInfo->numDevices = std3D_GetNumDevices();
                    if ( pCurInfo->numDevices )
                    {
                        size_t listSize = sizeof(Device3D) * pCurInfo->numDevices;
                        pCurInfo->aDevices = (Device3D*)STDMALLOC(listSize);
                        std::memcpy(pCurInfo->aDevices, std3D_GetAllDevices(), listSize);
                    }

                    std3D_Shutdown();
                }
            }

            stdDisplay_Close();
            ++pCurInfo;
        }
    }

    stdDisplay_Shutdown();
    return pDeviceList;
}

void J3DAPI std3D_FreeDisplayEnvironment(StdDisplayEnvironment* pEnv)
{
    if ( pEnv->aDisplayInfos )
    {
        StdDisplayInfo* pCurDevice = pEnv->aDisplayInfos;
        for ( size_t i = 0; i < pEnv->numInfos; ++i )
        {
            if ( pCurDevice->aDevices )
            {
                stdMemory_Free(pCurDevice->aDevices);
                pCurDevice->aDevices = nullptr;
            }

            if ( pCurDevice->aModes )
            {
                stdMemory_Free(pCurDevice->aModes);
                pCurDevice->aModes = nullptr;
            }

            ++pCurDevice;
        }

        stdMemory_Free(pEnv->aDisplayInfos);
        pEnv->aDisplayInfos = nullptr;
    }

    stdMemory_Free(pEnv);
}
