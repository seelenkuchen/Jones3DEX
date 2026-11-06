// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>
#include <babylon/log/Log.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/SdlPlatform.h>
#include <babylon/platform/Window.h>

#include "GpuRequirement.h"
#include "KeepGpuDriverLoaded.h"

#include <doctest.h>

#include <memory>
#include <string>

namespace
{
    using babylon::log::CaptureSink;
    using babylon::log::Level;
    using namespace babylon::render;

    std::string joined(const CaptureSink& sink)
    {
        std::string text;
        for ( const auto& record : sink.records() )
        {
            text += text.empty() ? "" : "; ";
            text += record.message;
        }
        return text;
    }

    const char* formatName(TextureFormat format)
    {
        switch ( format )
        {
            case TextureFormat::Rgba8: return "Rgba8";
            case TextureFormat::Bgra8: return "Bgra8";
            case TextureFormat::B5G6R5: return "B5G6R5";
            case TextureFormat::B5G5R5A1: return "B5G5R5A1";
            case TextureFormat::B4G4R4A4: return "B4G4R4A4";
            case TextureFormat::D24S8: return "D24S8";
            case TextureFormat::D32F: return "D32F";
        }
        return "?";
    }
}

TEST_CASE("The headless SDL GPU device names its driver and lists its formats")
{
    test::keepGpuDriverLoaded();
    CaptureSink sink;
    const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }
    CHECK(sink.records().empty());
    const DeviceInfo& info = device->info();
    CHECK(info.backend.rfind("sdl-gpu ", 0) == 0);
    CHECK_FALSE(info.hasSwapchain);
    CHECK(info.swapchainFormat == TextureFormat::Bgra8);
    CHECK(info.shaderFormats != ShaderFormat::None);

    // B4G4R4A4 is never a color target on this backend (SdlGpuDevice.h).
    CHECK_FALSE(device->supportsFormat(TextureFormat::B4G4R4A4, TextureUsage::ColorTarget));

    // What this driver offers, for the test log.
    std::string table;
    for ( const TextureFormat format : kAllTextureFormats )
    {
        table += std::string(formatName(format)) + ":";
        if ( isDepthFormat(format) )
        {
            table += device->supportsFormat(format, TextureUsage::DepthTarget) ? " depth" : " -";
        }
        else
        {
            table += device->supportsFormat(format, TextureUsage::Sampled) ? " sampled" : "";
            table += device->supportsFormat(format, TextureUsage::ColorTarget) ? " target" : "";
        }
        table += "; ";
    }
    MESSAGE(info.backend << " formats: " << table);
}

TEST_CASE("An SDL GPU device refuses windows of other platforms")
{
    CaptureSink sink;
    babylon::platform::NullPlatform platform;
    const std::unique_ptr<babylon::platform::Window> window =
        platform.createWindow(babylon::platform::WindowDesc{ "null", { 64, 48 } });
    REQUIRE(window != nullptr);
    CHECK(createSdlGpuDevice(window.get()) == nullptr);
    INFO("captured log: " << joined(sink));
    REQUIRE(sink.records().size() == 1);
    CHECK(sink.records()[0].level == Level::Warning);
}

TEST_CASE("An SDL GPU device on a window clears and presents its swapchain")
{
    const std::unique_ptr<babylon::platform::Platform> platform =
        babylon::platform::createSdlPlatform();
    if ( platform == nullptr )
    {
        FAIL("createSdlPlatform() returned nullptr: run this test with "
            "the offscreen video driver");
    }
    const std::unique_ptr<babylon::platform::Window> window =
        platform->createWindow(babylon::platform::WindowDesc{ "swapchain", { 64, 48 } });
    REQUIRE(window != nullptr);

    test::keepGpuDriverLoaded();
    CaptureSink sink;
    std::unique_ptr<Device> device = createSdlGpuDevice(window.get());
    if ( device == nullptr )
    {
        test::noGpuDevice("no GPU device for the window: " + joined(sink));
        return;
    }
    INFO("captured log: " << joined(sink));
    CHECK(sink.records().empty());
    const DeviceInfo& info = device->info();
    CHECK(info.hasSwapchain);
    CHECK((info.swapchainFormat == TextureFormat::Bgra8
        || info.swapchainFormat == TextureFormat::Rgba8));
    CHECK(device->supportsFormat(info.swapchainFormat, TextureUsage::ColorTarget));

    // Frames to the swapchain, one also with a depth target the window's
    // size, and one that never touches the swapchain and so presents
    // nothing.
    const TextureFormat depthFormat = device->supportsFormat(TextureFormat::D24S8,
        TextureUsage::DepthTarget) ? TextureFormat::D24S8 : TextureFormat::D32F;
    const TextureHandle depth = device->createTexture(TextureDesc{ 64, 48, 1, depthFormat,
        TextureUsage::DepthTarget });
    REQUIRE_FALSE(depth.isNull());
    for ( int i = 0; i < 4; ++i )
    {
        CommandList frame = device->beginFrame();
        if ( i != 3 )
        {
            PassDesc pass;
            pass.colorTargets = { ColorAttachment{ TextureHandle{}, 0, LoadOp::Clear,
                StoreOp::Store, ClearColor{ 0.25f * static_cast<float>(i), 0.0f, 0.0f, 1.0f } } };
            if ( i == 1 )
            {
                pass.depthTarget = DepthAttachment{ depth };
            }
            REQUIRE(frame.beginPass(pass));
            frame.setViewport(Viewport{ 0, 0, 64, 48 });
            frame.setScissor(ScissorRect{ 0, 0, 32, 24 });
            REQUIRE(frame.endPass());
        }
        CHECK(device->submit(frame));
    }
    CHECK(sink.records().empty());
    device->destroyTexture(depth);

    // The device goes before its window, as SdlGpuDevice.h requires.
    device.reset();
    CHECK(sink.records().empty());
}
