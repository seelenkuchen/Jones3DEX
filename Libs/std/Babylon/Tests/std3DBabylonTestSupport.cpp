// Shared fixtures and helpers of the std3D Babylon adapter tests.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

namespace std3DBabylonTest
{
    StdBabylonContext std3DBabylonTest_context{};
    std::unique_ptr<render::Device> std3DBabylonTest_pDevice;
    render::NullDevice* std3DBabylonTest_pNullDevice = nullptr;

    const char* const kCapturePath = "std3DBabylonTest_Input.rcap";
    const char* const kReplayPath  = "std3DBabylonTest_Output.rcap";

    void UseNullDevice(const render::NullDeviceDesc& desc)
    {
        std::unique_ptr<render::NullDevice> pDevice = render::createNullDevice(desc);
        std3DBabylonTest_pNullDevice = pDevice.get();
        std3DBabylonTest_pDevice     = std::move(pDevice);
        std3DBabylonTest_context.pRenderDevice = std3DBabylonTest_pDevice.get();
    }

    void UseDevice(std::unique_ptr<render::Device> pDevice)
    {
        std3DBabylonTest_pNullDevice = nullptr;
        std3DBabylonTest_pDevice     = std::move(pDevice);
        std3DBabylonTest_context.pRenderDevice = std3DBabylonTest_pDevice.get();
    }

    void DropDevice()
    {
        std3DBabylonTest_context.pRenderDevice = nullptr;
        std3DBabylonTest_pNullDevice = nullptr;
        std3DBabylonTest_pDevice.reset();
    }

    render::NullDevice& Null()
    {
        TEST_ASSERT_NOT_NULL(std3DBabylonTest_pNullDevice);
        return *std3DBabylonTest_pNullDevice;
    }

    // The display at width x height (0 keeps the display's default 640 x 480), then std3D. What the
    // mode set recorded on a Null device (its clear and present) is dropped, so a test's stream
    // starts with its own calls.
    void OpenStd3D(uint32_t width, uint32_t height)
    {
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
        if ( width && height )
        {
            stdDisplay_SetDefaultResolution(width, height);
        }
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
        TEST_ASSERT_EQUAL_INT(1, std3D_Startup());
        TEST_ASSERT_EQUAL_INT(1, std3D_Open(0u));
        if ( std3DBabylonTest_pNullDevice )
        {
            std3DBabylonTest_pNullDevice->clearRecorded();
        }
    }

    // The end of a game frame: the scene, then the present, which submits the frame (M6.7).
    void EndFrame()
    {
        std3D_EndScene();
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    }

    void CloseStd3D()
    {
        std3D_Shutdown();
        stdDisplay_Shutdown();
        std3D_ResetGlobals();
        stdDisplay_ResetGlobals();
    }

    // A VBuffer of width x height pixels of ci, filled with seed + i at byte i of each row's pixels.
    tVBuffer* NewVBuffer(const ColorInfo& ci, uint32_t width, uint32_t height, uint8_t seed)
    {
        tRasterInfo raster;
        std::memset(&raster, 0, sizeof(raster));
        raster.width     = width;
        raster.height    = height;
        raster.colorInfo = ci;

        tVBuffer* pVBuffer = stdDisplay_VBufferNew(&raster, 0, 0);
        TEST_ASSERT_NOT_NULL(pVBuffer);
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(pVBuffer));
        for ( size_t i = 0; i < pVBuffer->rasterInfo.size; ++i )
        {
            pVBuffer->pPixels[i] = (uint8_t)(seed + 7u * i);
        }
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pVBuffer));
        return pVBuffer;
    }

    // The pixel bytes of a VBuffer in Babylon's upload layout: rows packed, top row first.
    std::vector<std::byte> PackedPixels(const tVBuffer* pVBuffer)
    {
        const size_t rowBytes = pVBuffer->rasterInfo.width * (pVBuffer->rasterInfo.colorInfo.bpp / 8u);
        std::vector<std::byte> bytes;
        for ( uint32_t y = 0; y < pVBuffer->rasterInfo.height; ++y )
        {
            const std::byte* pRow = reinterpret_cast<const std::byte*>(pVBuffer->pPixels + y * pVBuffer->rasterInfo.rowSize);
            bytes.insert(bytes.end(), pRow, pRow + rowBytes);
        }
        return bytes;
    }

    void AssertBytes(const std::vector<std::byte>& expected, const std::vector<std::byte>& actual)
    {
        TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
        if ( !expected.empty() )
        {
            TEST_ASSERT_EQUAL_MEMORY(expected.data(), actual.data(), expected.size());
        }
    }

    // readTexture with its warnings caught, so a failing assert never leaves a sink attached (Unity
    // leaves a test by longjmp, past destructors). *pNumWarnings gets the number of warnings.
    std::vector<std::byte> ReadTexture(render::TextureHandle texture, uint32_t mip, size_t* pNumWarnings)
    {
        babylon::log::CaptureSink sink;
        std::vector<std::byte> bytes = std3DBabylonTest_pDevice->readTexture(texture, mip);
        if ( pNumWarnings )
        {
            *pNumWarnings = sink.records().size();
        }
        return bytes;
    }

    render::TextureHandle CachedHandle(const tSystemTexture& texture)
    {
        TEST_ASSERT_NOT_NULL(texture.pCachedTexture);
        return texture.pCachedTexture->texture;
    }

    // A 4x2 texture with one 2x1 mip of ci, allocated and cached; the VBuffers are freed again.
    void MakeCachedTexture(tSystemTexture* pTexture, const ColorInfo& ci, uint8_t seed,
        std::vector<std::byte>* pMip0, std::vector<std::byte>* pMip1)
    {
        tVBuffer* apMips[2] = { NewVBuffer(ci, 4u, 2u, seed), NewVBuffer(ci, 2u, 1u, (uint8_t)(seed + 100u)) };
        if ( pMip0 )
        {
            *pMip0 = PackedPixels(apMips[0]);
        }
        if ( pMip1 )
        {
            *pMip1 = PackedPixels(apMips[1]);
        }

        std::memset(pTexture, 0, sizeof(*pTexture));
        std3D_AllocSystemTexture(pTexture, apMips, 2u, std3D_GetColorFormat(&ci));
        TEST_ASSERT_EQUAL_size_t(2u, pTexture->numMipLevels);
        std3D_AddToTextureCache(pTexture, std3D_GetColorFormat(&ci));
        TEST_ASSERT_NOT_NULL(pTexture->pCachedTexture);

        stdDisplay_VBufferFree(apMips[0]);
        stdDisplay_VBufferFree(apMips[1]);
    }

    // X8R8G8B8 reads alpha as 1: every fourth byte of a 32-bit RGB texture is 255.
    std::vector<std::byte> Opaque(std::vector<std::byte> bytes)
    {
        for ( size_t i = 3; i < bytes.size(); i += 4 )
        {
            bytes[i] = std::byte{ 0xFF };
        }
        return bytes;
    }

    // A scene pass: the back buffer color, then the normal target, cleared to 0 with depth (M6.9a).
    render::PassDesc ScenePass(render::LoadOp colorLoad, render::LoadOp depthLoad)
    {
        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_GetColorTarget(), 0, colorLoad,
            render::StoreOp::Store, render::ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f } });
        pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_GetNormalTarget(), 0, depthLoad,
            render::StoreOp::Store, render::ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f } });
        pass.depthTarget = render::DepthAttachment{ stdRenderBabylon_GetDepthTarget(), depthLoad,
            render::StoreOp::Store, 1.0f, 0 };
        return pass;
    }

    void AssertScenePass(size_t index, render::LoadOp colorLoad, render::LoadOp depthLoad)
    {
        const render::BeginPass& begin = RecordedAt<render::BeginPass>(index);
        TEST_ASSERT_TRUE(begin.desc == ScenePass(colorLoad, depthLoad));
    }

    void AssertSubmitted(size_t index)
    {
        const render::FrameSubmitted& submitted = RecordedAt<render::FrameSubmitted>(index);
        TEST_ASSERT_TRUE(submitted.frame > 0);
        TEST_ASSERT_FALSE(submitted.presented);
    }

    std::vector<char> ReadFile(const char* pPath)
    {
        std::ifstream file(pPath, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    bool GpuRequired()
    {
        // SDL_getenv: MSVC rejects getenv under /sdl.
        const char* pValue = SDL_getenv("BABYLON_REQUIRE_GPU");
        return pValue && std::strcmp(pValue, "1") == 0;
    }

    // Lavapipe keeps memory under pthread_once that LeakSanitizer reports each time the Vulkan loader
    // unloads it; keeping one more reference loaded avoids that (see Babylon's KeepGpuDriverLoaded.h).
    std::unique_ptr<render::Device> CreateGpuDevice(babylon::platform::Window* pWindow)
    {
        std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(pWindow);
        if ( !pDevice && GpuRequired() )
        {
            TEST_FAIL_MESSAGE("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }
        return pDevice;
    }

    // The windowed case's SDL objects, released in the tear-down, which also runs after a failed
    // assert: the device first, then the window, then the platform.
    std::unique_ptr<babylon::platform::Platform> std3DBabylonGpuTest_pPlatform;
    std::unique_ptr<babylon::platform::Window> std3DBabylonGpuTest_pWindow;

    // The back buffer as the last RenderScene read it back: Bgra8, rows top first.
    std::vector<std::byte> std3DBabylonGpuTest_pixels;

    // std3D on the SDL GPU device at width x height; false when there is no GPU (the test is then
    // ignored, or failed with BABYLON_REQUIRE_GPU=1).
    bool OpenGpuStd3D(uint32_t width, uint32_t height)
    {
        UseDevice(CreateGpuDevice(nullptr));
        if ( !std3DBabylonTest_pDevice )
        {
            return false;
        }
        OpenStd3D(width, height);
        return true;
    }

    // The pixel at (x, y) of the last RenderScene is r, g, b, a, each within tolerance.
    void AssertPixel(uint32_t x, uint32_t y, int r, int g, int b, int a, int tolerance)
    {
        const size_t at = ((size_t)y * stdRenderBabylon_GetBackBufferWidth() + x) * 4u;
        TEST_ASSERT_TRUE(at + 4u <= std3DBabylonGpuTest_pixels.size());
        const int actual[4] = { (int)std3DBabylonGpuTest_pixels[at + 2], (int)std3DBabylonGpuTest_pixels[at + 1],
            (int)std3DBabylonGpuTest_pixels[at], (int)std3DBabylonGpuTest_pixels[at + 3] };
        const int expected[4] = { r, g, b, a };
        char aMessage[128];
        std::snprintf(aMessage, sizeof(aMessage), "pixel (%u, %u) is (%d, %d, %d, %d), expected (%d, %d, %d, %d)", x, y,
            actual[0], actual[1], actual[2], actual[3], r, g, b, a);
        for ( int i = 0; i < 4; ++i )
        {
            TEST_ASSERT_INT_WITHIN_MESSAGE(tolerance, expected[i], actual[i], aMessage);
        }
    }

    // Runs the final pass (with its tone map at the current parameters) over the last presented
    // source into an offscreen Bgra8 target and returns its pixels, rows top first.
    std::vector<std::byte> CapturePresented(uint32_t width, uint32_t height)
    {
        const render::TextureHandle target = std3DBabylonTest_pDevice->createTexture(
            render::TextureDesc{ width, height, 1, render::TextureFormat::Bgra8,
                render::TextureUsage::ColorTarget });
        TEST_ASSERT_FALSE(target.isNull());
        TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(target, width, height));
        std::vector<std::byte> bytes = std3DBabylonTest_pDevice->readTexture(target, 0);
        std3DBabylonTest_pDevice->destroyTexture(target);
        return bytes;
    }

    // A quad from (x0, y0) to (x1, y1) in screen pixels, texture coordinates 0 to 1, rhwLeft on the
    // left edge and rhwRight on the right one; two triangles split along (x0, y0) to (x1, y1).
    void DrawQuad(tSysTexture* pTex, Std3DRenderState state, float x0, float y0, float x1, float y1, float z, uint32_t color,
        float rhwLeft, float rhwRight)
    {
        StdVertexTL aVerts[4] = { Vertex(x0, y0, z, rhwLeft, color, 0.0f, 0.0f), Vertex(x1, y0, z, rhwRight, color, 1.0f, 0.0f),
            Vertex(x1, y1, z, rhwRight, color, 1.0f, 1.0f), Vertex(x0, y1, z, rhwLeft, color, 0.0f, 1.0f) };
        uint16_t aIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
        std3D_DrawRenderList(pTex, state, aVerts, 4u, aIndices, 6u);
    }

    // A width x height texture of ci with one texel value each (its bytes little endian), cached.
    void MakeTexture(tSystemTexture* pTexture, const ColorInfo& ci, uint32_t width, uint32_t height, const std::vector<uint32_t>& aTexels)
    {
        tVBuffer* pVBuffer = NewVBuffer(ci, width, height, 0);
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(pVBuffer));
        const size_t texelBytes = ci.bpp / 8u;
        for ( uint32_t y = 0; y < height; ++y )
        {
            for ( uint32_t x = 0; x < width; ++x )
            {
                uint8_t* pTexel = pVBuffer->pPixels + y * pVBuffer->rasterInfo.rowSize + x * texelBytes;
                for ( size_t i = 0; i < texelBytes; ++i )
                {
                    pTexel[i] = (uint8_t)(aTexels[y * width + x] >> (8u * i));
                }
            }
        }
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pVBuffer));

        std::memset(pTexture, 0, sizeof(*pTexture));
        std3D_AllocSystemTexture(pTexture, &pVBuffer, 1u, std3D_GetColorFormat(&ci));
        std3D_AddToTextureCache(pTexture, std3D_GetColorFormat(&ci));
        stdDisplay_VBufferFree(pVBuffer);
        TEST_ASSERT_NOT_NULL(pTexture->pCachedTexture);
    }

    // A texel of ci from channel values in the format's own bits.
    uint32_t Texel(const ColorInfo& ci, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
    {
        return (r << ci.redPosShift) | (g << ci.greenPosShift) | (b << ci.bluePosShift) | (ci.alphaBPP ? a << ci.alphaPosShift : 0u);
    }

    uint32_t MaxOf(uint32_t bits)
    {
        return (1u << bits) - 1u;
    }

    // rdCamera's scene depth: 0.1 at the near plane, 1.0 at the far plane, 1/z-linear in between.
    float SceneDepthForEyeDepth(float eyeDepth, float nearPlane, float farPlane)
    {
        const float linear = (farPlane - nearPlane * farPlane / eyeDepth)
            / (farPlane - nearPlane);
        return 0.1f + 0.9f * linear;
    }

    StdVertexTL Vertex(float x, float y, float z, float rhw, uint32_t color, float u, float v)
    {
        StdVertexTL vertex;
        vertex.sx       = x;
        vertex.sy       = y;
        vertex.sz       = z;
        vertex.rhw      = rhw;
        vertex.color    = color;
        vertex.specular = 0xFF000000u;
        vertex.tu       = u;
        vertex.tv       = v;
        return vertex;
    }

    StdVertexLit LitVertex(const StdVertexTL& tl, float vx, float vy, float vz, float nx, float ny, float nz)
    {
        StdVertexLit vertex;
        vertex.tl = tl;
        vertex.vx = vx;
        vertex.vy = vy;
        vertex.vz = vz;
        vertex.nx = nx;
        vertex.ny = ny;
        vertex.nz = nz;
        return vertex;
    }

    // The normal target texel at (x, y) of a texture read back, as its four components in their
    // own bits (R10G10B10A2: 0..1023 and alpha 0..3; Rgba8: 0..255).
    std::array<int, 4> NormalTexel(const std::vector<std::byte>& aBytes, uint32_t x, uint32_t y)
    {
        const size_t at = ((size_t)y * stdRenderBabylon_GetBackBufferWidth() + x) * 4u;
        TEST_ASSERT_TRUE(at + 4u <= aBytes.size());
        const uint32_t word = (uint32_t)aBytes[at] | ((uint32_t)aBytes[at + 1u] << 8) | ((uint32_t)aBytes[at + 2u] << 16)
            | ((uint32_t)aBytes[at + 3u] << 24);
        if ( stdRenderBabylon_GetNormalFormat() == render::TextureFormat::R10G10B10A2 )
        {
            return { (int)(word & 0x3FFu), (int)((word >> 10) & 0x3FFu), (int)((word >> 20) & 0x3FFu), (int)(word >> 30) };
        }
        return { (int)(word & 0xFFu), (int)((word >> 8) & 0xFFu), (int)((word >> 16) & 0xFFu), (int)(word >> 24) };
    }

    // The mean of the red, green and blue bytes of a rectangle of a Bgra8 capture, rows top first.
    float SsaoMeanBrightness(const std::vector<std::byte>& pixels, uint32_t width,
        uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
    {
        double sum = 0.0;
        size_t count = 0;
        for ( uint32_t y = y0; y < y1; ++y )
        {
            for ( uint32_t x = x0; x < x1; ++x )
            {
                const size_t at = ((size_t)y * width + x) * 4u;
                sum += (double)pixels[at + 0u] + (double)pixels[at + 1u] + (double)pixels[at + 2u];
                count += 3u;
            }
        }
        return count > 0u ? (float)(sum / (double)count) : 0.0f;
    }

    // The ambient-occlusion test scene: a floor below a box corner at the game's own scale, so the
    // world-space radii (0.02, 0.035, 0.05) cover a few half-res pixels. Indy is about 0.2 units tall
    // and the gameplay camera sits about 0.21 behind him, so the near left wall is at eye depth 0.20,
    // the far right wall at 0.22 and the floor at 0.24, converted to rdCamera's scene depth. The
    // creases at x = 32 and y = 32 are one radius deep, which the range falloff still darkens.
    void DrawSsaoCreaseSceneWithProjection(float nearPlane, float farPlane)
    {
        const uint32_t grey = 0xFF808080u;
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 32.0f,
            SceneDepthForEyeDepth(0.20f, nearPlane, farPlane), grey);
        DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 0.0f, 64.0f, 32.0f,
            SceneDepthForEyeDepth(0.22f, nearPlane, farPlane), grey);
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 32.0f, 64.0f, 64.0f,
            SceneDepthForEyeDepth(0.24f, nearPlane, farPlane), grey);
    }

    // An Indy-sized near object (at the camera-to-Indy distance) against a far flat wall. On High the
    // background ring around it must stay within a small tolerance of Off: the range falloff has to
    // reach zero well before the 1.8-unit depth step, so the near object does not push a dark box
    // into the wall behind it.
    void DrawSsaoNearObjectScene()
    {
        const uint32_t grey = 0xFF808080u;
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f,
            SceneDepthForEyeDepth(2.0f, 0.1f, 100.0f), grey);
        DrawQuad(nullptr, (Std3DRenderState)0, 27.0f, 11.0f, 37.0f, 53.0f,
            SceneDepthForEyeDepth(0.20f, 0.1f, 100.0f), 0xFFB0B0B0u);
    }

    void DrawSsaoCreaseScene()
    {
        DrawSsaoCreaseSceneWithProjection(0.1f, 100.0f);
    }
}
