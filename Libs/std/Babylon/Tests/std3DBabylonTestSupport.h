// Shared fixtures and helpers of the std3D Babylon adapter tests.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#ifndef STD_STD3DBABYLONTESTSUPPORT_H
#define STD_STD3DBABYLONTESTSUPPORT_H

#include <unity_fixture.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/log/CaptureSink.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/SdlPlatform.h>
#include <babylon/platform/Window.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/std3DBabylonTests.h>
#include <babylon/shaders/stdBabylonShaders.h>
#include <babylon/time/Clock.h>
#include <babylon/time/NullClock.h>
#include <babylon/time/SdlClock.h>

#include <SDL3/SDL_stdinc.h>

#include <std/General/std3D.h>
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdDisplayCapture.h>
#include <std/General/stdRenderCapture.h>
#include <std/General/stdRenderReplay.h>

#include <std/Babylon/std3DBabylon.h>
#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonCvars.h>
#include <std/Babylon/stdRenderBabylon.h>
#include <std/Babylon/stdRenderReplayBabylon.h>

#include "stdGeneralTest.h"

namespace render = babylon::render;

namespace std3DBabylonTest
{
    extern StdBabylonContext std3DBabylonTest_context;
    extern std::unique_ptr<render::Device> std3DBabylonTest_pDevice;
    extern render::NullDevice* std3DBabylonTest_pNullDevice;
    extern const char* const kCapturePath;
    extern const char* const kReplayPath;
    extern std::vector<std::byte> std3DBabylonGpuTest_pixels;
    extern std::unique_ptr<babylon::platform::Platform> std3DBabylonGpuTest_pPlatform;
    extern std::unique_ptr<babylon::platform::Window> std3DBabylonGpuTest_pWindow;

    void UseNullDevice(const render::NullDeviceDesc& desc = {})
    ;
    void UseDevice(std::unique_ptr<render::Device> pDevice);
    void DropDevice();
    render::NullDevice& Null();
    void OpenStd3D(uint32_t width = 0, uint32_t height = 0)
    ;
    void EndFrame();
    void CloseStd3D();
    tVBuffer* NewVBuffer(const ColorInfo& ci, uint32_t width, uint32_t height, uint8_t seed);
    std::vector<std::byte> PackedPixels(const tVBuffer* pVBuffer);
    void AssertBytes(const std::vector<std::byte>& expected, const std::vector<std::byte>& actual);
    std::vector<std::byte> ReadTexture(render::TextureHandle texture, uint32_t mip, size_t* pNumWarnings = nullptr)
    ;
    render::TextureHandle CachedHandle(const tSystemTexture& texture);
    void MakeCachedTexture(tSystemTexture* pTexture, const ColorInfo& ci, uint8_t seed,
        std::vector<std::byte>* pMip0 = nullptr, std::vector<std::byte>* pMip1 = nullptr)
    ;
    std::vector<std::byte> Opaque(std::vector<std::byte> bytes);
    render::PassDesc ScenePass(render::LoadOp colorLoad, render::LoadOp depthLoad);
    void AssertScenePass(size_t index, render::LoadOp colorLoad, render::LoadOp depthLoad);
    void AssertSubmitted(size_t index);
    std::vector<char> ReadFile(const char* pPath);
    bool GpuRequired();
    std::unique_ptr<render::Device> CreateGpuDevice(babylon::platform::Window* pWindow);
    StdVertexTL Vertex(float x, float y, float z, float rhw, uint32_t color, float u = 0.0f, float v = 0.0f)
    ;
    StdVertexLit LitVertex(const StdVertexTL& tl, float vx, float vy, float vz, float nx, float ny, float nz);
    bool OpenGpuStd3D(uint32_t width, uint32_t height);
    void AssertPixel(uint32_t x, uint32_t y, int r, int g, int b, int a, int tolerance = 1)
    ;
    std::vector<std::byte> CapturePresented(uint32_t width, uint32_t height);
    void DrawQuad(tSysTexture* pTex, Std3DRenderState state, float x0, float y0, float x1, float y1, float z, uint32_t color,
        float rhwLeft = 1.0f, float rhwRight = 1.0f)
    ;
    void MakeTexture(tSystemTexture* pTexture, const ColorInfo& ci, uint32_t width, uint32_t height, const std::vector<uint32_t>& aTexels);
    uint32_t Texel(const ColorInfo& ci, uint32_t r, uint32_t g, uint32_t b, uint32_t a);
    uint32_t MaxOf(uint32_t bits);
    float SceneDepthForEyeDepth(float eyeDepth, float nearPlane, float farPlane);
    std::array<int, 4> NormalTexel(const std::vector<std::byte>& aBytes, uint32_t x, uint32_t y);
    float SsaoMeanBrightness(const std::vector<std::byte>& pixels, uint32_t width,
        uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1);
    void DrawSsaoCreaseSceneWithProjection(float nearPlane, float farPlane);
    void DrawSsaoNearObjectScene();
    void DrawSsaoCreaseScene();

    template <class T>
    const T& RecordedAt(size_t index)
    {
        const std::vector<render::RecordedCommand>& recorded = Null().recorded();
        TEST_ASSERT_TRUE(index < recorded.size());
        TEST_ASSERT_TRUE(std::holds_alternative<T>(recorded[index]));
        return std::get<T>(recorded[index]);
    }

    // Runs draws in one scene, then reads the back buffer back. Every render warning fails the test:
    // a draw the device dropped draws nothing.
    template <class F>
    void RenderScene(F draws)
    {
        std::vector<std::string> aWarnings;
        int startResult = 1;
        bool bRead = false;
        {
            babylon::log::CaptureSink sink;
            startResult = std3D_StartScene();
            if ( startResult == 0 )
            {
                draws();
                std3D_EndScene();
                (void)stdDisplay_Update(); // no assert while the sink is attached
            }
            // The game-visible back buffer: the scene color target translated to Bgra8.
            std::vector<std::byte> aPixels((size_t)stdRenderBabylon_GetBackBufferWidth()
                * stdRenderBabylon_GetBackBufferHeight() * 4u);
            bRead = stdRenderBabylon_ReadBackBuffer(aPixels.data(), nullptr, nullptr);
            std3DBabylonGpuTest_pixels = std::move(aPixels);
            for ( const babylon::log::CapturedRecord& record : sink.records() )
            {
                aWarnings.push_back(record.message);
            }
        }

        TEST_ASSERT_EQUAL_INT(0, startResult);
        TEST_ASSERT_TRUE(bRead);
        for ( const std::string& warning : aWarnings )
        {
            TEST_FAIL_MESSAGE(warning.c_str());
        }
        TEST_ASSERT_EQUAL_size_t((size_t)stdRenderBabylon_GetBackBufferWidth() * stdRenderBabylon_GetBackBufferHeight() * 4u,
            std3DBabylonGpuTest_pixels.size());
    }
}

#endif
