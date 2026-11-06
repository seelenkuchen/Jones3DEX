// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/Device.h>

#include <memory>

namespace babylon::platform
{
    class Window;
}

namespace babylon::render
{

    // Device on SDL3 GPU (ADR-0009, ADR-0025): Vulkan, Direct3D 12 or Metal,
    // whichever SDL finds first, accepting shader blobs in SPIR-V, DXIL or
    // MSL as the driver takes them (DeviceInfo::shaderFormats).
    //
    // With a window, which must come from the SDL platform backend, the
    // device claims it for its swapchain: DeviceInfo::hasSwapchain is true,
    // swapchainFormat is the window's (Bgra8 or Rgba8), and submit() acquires
    // the window's image for passes to the swapchain and presents it. The
    // window must outlive the device. With nullptr the device is headless:
    // no swapchain, and passes to the swapchain draw nothing.
    //
    // SDL GPU needs SDL's video subsystem, so the device initializes it
    // (SDL_InitSubSystem, counted) and quits it again when it is destroyed;
    // without a display, SDL_VIDEO_DRIVER=offscreen gives a headless one.
    //
    // Formats: B4G4R4A4 is never a color target here, because Vulkan stores
    // it with the components in other bits and only fixes that up when
    // sampling; every other format and usage is what the driver reports.
    // Limits: maxTextureDimension 8192 and maxBufferSize 128 MiB on every
    // driver. Uploads and readbacks each run in their own command buffer,
    // in order with submits; readbacks wait for the GPU.
    //
    // Returns nullptr and logs one warning when no GPU device can be created
    // (no Vulkan, Direct3D 12 or Metal driver), when SDL video can't start,
    // or when the window can't be claimed. The caller owns the device.
    std::unique_ptr<Device> createSdlGpuDevice(platform::Window* window);

}
