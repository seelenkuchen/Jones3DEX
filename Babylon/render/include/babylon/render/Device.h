// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace babylon::render
{

    // Limits that differ between devices.
    struct DeviceLimits
    {
        std::uint32_t maxTextureDimension = 0;   // width and height, in texels
        std::uint32_t maxBufferSize = 0;         // in bytes

        bool operator==(const DeviceLimits&) const = default;
    };

    // What a device is and accepts. Constant for the device's lifetime.
    struct DeviceInfo
    {
        // A short lowercase name of the backend, such as "null"; for logs and
        // test output, never for branching.
        std::string backend;

        // The shader formats createPipeline() accepts blobs in.
        ShaderFormat shaderFormats = ShaderFormat::None;

        // False for a headless device: passes to the swapchain are then
        // checked and recorded like any other but draw nothing, and nothing
        // is presented.
        bool hasSwapchain = false;

        // The format of the swapchain, which pipelines drawing to it name as
        // their color target format. A headless device reports Bgra8.
        TextureFormat swapchainFormat = TextureFormat::Bgra8;

        DeviceLimits limits;
    };

    // The GPU: owns every texture, buffer, sampler and pipeline, records
    // frames into CommandLists and executes them. Backends are chosen at
    // startup (ADR-0016) by their factories; callers only see this
    // interface and typed handles, never a backend object.
    //
    // Contract shared by every backend, checked here once:
    // - Every failure logs exactly one babylon::log::warning line and
    //   changes nothing; nothing throws, success paths log nothing.
    // - A create that fails returns the null handle.
    // - A stale handle (its resource was destroyed) is detected: the call
    //   logs one warning and does nothing. Destroying the null handle is a
    //   silent no-op.
    // - Uploads take effect in submit order: a list submitted after an
    //   upload sees the new contents, one submitted before it the old ones.
    //   CommandList::updateBuffer() takes effect at its place in its list.
    // - A resource may be destroyed right after the submit of a list that
    //   uses it; the backend keeps what the GPU still needs.
    //
    // Destroy every handle's resource or let the device go: destroying the
    // device frees all of them. Not thread-safe: call from one thread.
    class Device
    {
    public:
        virtual ~Device() = default;

        virtual const DeviceInfo& info() const = 0;

        // True when textures of format can be created with every flag in
        // usage. False for a depth format with ColorTarget usage, a color
        // format with DepthTarget usage, and anything the device lacks.
        virtual bool supportsFormat(TextureFormat format, TextureUsage usage) const = 0;

        // Creates a texture; its contents are undefined until uploaded,
        // cleared or copied to. Refused as described at TextureDesc.
        [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc);
        void destroyTexture(TextureHandle texture);

        // Replaces region of the texture with data: rows of region.width
        // texels in bytesPerTexel() bytes each, top row first, no padding,
        // so exactly width * height * bytesPerTexel() bytes. Refused for a
        // depth format, a mip out of range, a region of size 0 or outside
        // the mip, and a data size that doesn't match.
        [[nodiscard]] bool uploadTexture(TextureHandle texture, const TextureRegion& region,
            std::span<const std::byte> data);

        // Uploads one whole mip level.
        [[nodiscard]] bool uploadTexture(TextureHandle texture, std::uint32_t mip,
            std::span<const std::byte> data);

        // The contents of one mip level as uploadTexture() takes them, after
        // every submitted list has finished (it waits for the GPU; for tests
        // and screenshots, not per frame). Empty and one warning for a stale
        // handle, a depth format or a mip out of range.
        [[nodiscard]] std::vector<std::byte> readTexture(TextureHandle texture, std::uint32_t mip);

        // Creates a buffer; its contents are undefined until uploaded.
        [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc);
        void destroyBuffer(BufferHandle buffer);

        // Replaces data.size() bytes at offset. Refused for empty data or a
        // range past the end.
        [[nodiscard]] bool uploadBuffer(BufferHandle buffer, std::uint32_t offset,
            std::span<const std::byte> data);

        // size bytes at offset, after every submitted list has finished.
        // Empty and one warning for size 0 or a range past the end.
        [[nodiscard]] std::vector<std::byte> readBuffer(BufferHandle buffer, std::uint32_t offset,
            std::uint32_t size);

        [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& desc);
        void destroySampler(SamplerHandle sampler);

        // Creates a pipeline. Refused as described at PipelineDesc.
        [[nodiscard]] PipelineHandle createPipeline(const PipelineDesc& desc);
        void destroyPipeline(PipelineHandle pipeline);

        // Starts the next frame and returns its empty command list. A list
        // of an earlier frame that wasn't submitted is abandoned: submitting
        // it later is refused.
        [[nodiscard]] CommandList beginFrame();

        // Executes the list of the current frame and, when a pass drew to
        // the swapchain of a device that has one, presents the frame. Refused
        // (false, one warning, nothing executed) for a list of another
        // device or frame, one already submitted, or one with a pass still
        // open; the list is then unchanged. Otherwise each command is checked
        // against the live resources: a command naming a stale handle, a
        // texture or buffer without the usage it needs, a pipeline whose
        // target formats differ from the pass's, or a copy or draw out of
        // bounds is dropped with one warning. A dropped beginPass drops its
        // whole pass; after any other command of a pass was dropped, the
        // pass's later draws are dropped too, without further warnings. An
        // updateBuffer of a stale buffer or past its end is dropped the same
        // way. The rest executes in order, and true is returned.
        [[nodiscard]] bool submit(CommandList& list);

    private:
        // Lookups of live resources: nullptr (false) for null and stale
        // handles.
        virtual const TextureDesc* findTexture(TextureHandle texture) const = 0;
        virtual const BufferDesc* findBuffer(BufferHandle buffer) const = 0;
        virtual bool hasSampler(SamplerHandle sampler) const = 0;
        virtual const PipelineDesc* findPipeline(PipelineHandle pipeline) const = 0;

        // Called only with arguments that passed the shared checks and with
        // live handles. A create or upload the backend refuses logs its own
        // one warning and returns the null handle or false.
        virtual TextureHandle doCreateTexture(const TextureDesc& desc) = 0;
        virtual void doDestroyTexture(TextureHandle texture) = 0;
        virtual bool doUploadTexture(TextureHandle texture, const TextureRegion& region,
            std::span<const std::byte> data) = 0;
        virtual std::vector<std::byte> doReadTexture(TextureHandle texture, std::uint32_t mip) = 0;
        virtual BufferHandle doCreateBuffer(const BufferDesc& desc) = 0;
        virtual void doDestroyBuffer(BufferHandle buffer) = 0;
        virtual bool doUploadBuffer(BufferHandle buffer, std::uint32_t offset,
            std::span<const std::byte> data) = 0;
        virtual std::vector<std::byte> doReadBuffer(BufferHandle buffer, std::uint32_t offset,
            std::uint32_t size) = 0;
        virtual SamplerHandle doCreateSampler(const SamplerDesc& desc) = 0;
        virtual void doDestroySampler(SamplerHandle sampler) = 0;
        virtual PipelineHandle doCreatePipeline(const PipelineDesc& desc) = 0;
        virtual void doDestroyPipeline(PipelineHandle pipeline) = 0;

        // Advances to the next frame and returns its number, never 0 and
        // greater than every earlier one; currentFrame() returns it until
        // the next call.
        virtual std::uint64_t doBeginFrame() = 0;
        virtual std::uint64_t currentFrame() const = 0;

        // Executes commands that passed every check: balanced passes, live
        // handles, matching formats, in-bounds ranges. drawsToSwapchain is
        // true when a pass targets the swapchain. Cannot fail in a way the
        // caller can act on; a lost device logs its own warning.
        virtual void doSubmit(std::vector<Command> commands, bool drawsToSwapchain) = 0;
    };

}
