// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>

#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

namespace babylon::render
{

    // A pipeline the Null device created, with the desc as it was given.
    struct PipelineCreated
    {
        PipelineHandle pipeline;
        PipelineDesc desc;
        bool operator==(const PipelineCreated&) const = default;
    };

    // The end of one submitted frame. presented is true when the frame drew
    // to the swapchain of a device that has one, which a Null device never
    // has.
    struct FrameSubmitted
    {
        std::uint64_t frame = 0;
        bool presented = false;
        bool operator==(const FrameSubmitted&) const = default;
    };

    // One entry of the Null device's record: a pipeline creation, an
    // executed command (the Command alternatives), or the end of a frame.
    using RecordedCommand = std::variant<PipelineCreated, BeginPass, EndPass, SetPipeline,
        SetViewport, SetScissor, BindVertexBuffer, BindIndexBuffer, BindTexture, PushUniforms,
        Draw, DrawIndexed, CopyTexture, FrameSubmitted>;

    // How a Null device is made.
    struct NullDeviceDesc
    {
        // Formats the device reports as unsupported for every usage, to
        // exercise the paths of devices that lack a format.
        std::vector<TextureFormat> unsupportedFormats;
    };

    // Device that touches no GPU, for tests and headless runs. It honors the
    // whole Device contract: the same checks and warnings, handle
    // lifetimes, and texture and buffer contents kept in memory, so uploads,
    // clears of texture targets and copies are applied and readTexture() and
    // readBuffer() return real bytes. It does not rasterize: draws change no
    // contents. It is headless (no swapchain), accepts blobs in every shader
    // format without looking at the code, supports every format for every
    // usage its kind allows except NullDeviceDesc::unsupportedFormats, and
    // reports maxTextureDimension 8192 and maxBufferSize 256 MiB. New
    // textures and buffers are zero-filled.
    //
    // Every pipeline creation and every executed command of a submitted
    // list is appended to recorded(), in order, followed by a FrameSubmitted
    // per submit; dropped commands and refused calls are not recorded.
    class NullDevice final : public Device
    {
    public:
        explicit NullDevice(const NullDeviceDesc& desc = {});
        ~NullDevice() override;
        NullDevice(const NullDevice&) = delete;
        NullDevice& operator=(const NullDevice&) = delete;

        const DeviceInfo& info() const override;
        bool supportsFormat(TextureFormat format, TextureUsage usage) const override;

        // Everything recorded since creation or the last clearRecorded().
        const std::vector<RecordedCommand>& recorded() const;
        void clearRecorded();

    private:
        const TextureDesc* findTexture(TextureHandle texture) const override;
        const BufferDesc* findBuffer(BufferHandle buffer) const override;
        bool hasSampler(SamplerHandle sampler) const override;
        const PipelineDesc* findPipeline(PipelineHandle pipeline) const override;

        TextureHandle doCreateTexture(const TextureDesc& desc) override;
        void doDestroyTexture(TextureHandle texture) override;
        bool doUploadTexture(TextureHandle texture, const TextureRegion& region,
            std::span<const std::byte> data) override;
        std::vector<std::byte> doReadTexture(TextureHandle texture, std::uint32_t mip) override;
        BufferHandle doCreateBuffer(const BufferDesc& desc) override;
        void doDestroyBuffer(BufferHandle buffer) override;
        bool doUploadBuffer(BufferHandle buffer, std::uint32_t offset,
            std::span<const std::byte> data) override;
        std::vector<std::byte> doReadBuffer(BufferHandle buffer, std::uint32_t offset,
            std::uint32_t size) override;
        SamplerHandle doCreateSampler(const SamplerDesc& desc) override;
        void doDestroySampler(SamplerHandle sampler) override;
        PipelineHandle doCreatePipeline(const PipelineDesc& desc) override;
        void doDestroyPipeline(PipelineHandle pipeline) override;
        std::uint64_t doBeginFrame() override;
        std::uint64_t currentFrame() const override;
        void doSubmit(std::vector<Command> commands, bool drawsToSwapchain) override;

        // The resources and the record, private to NullDevice.cpp.
        struct State;
        std::unique_ptr<State> m_state;
    };

    // Creates a Null device; never fails. The caller owns it.
    std::unique_ptr<NullDevice> createNullDevice(const NullDeviceDesc& desc = {});

}
