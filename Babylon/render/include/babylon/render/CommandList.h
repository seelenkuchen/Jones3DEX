// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/Handle.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Texture.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace babylon::render
{

    // What a pass does with an attachment's contents when it begins.
    enum class LoadOp : std::uint8_t
    {
        Load,       // keep what is there
        Clear,      // fill with the attachment's clear value
        DontCare,   // contents are undefined; the cheapest choice
    };

    // What a pass does with an attachment's contents when it ends.
    enum class StoreOp : std::uint8_t
    {
        Store,      // keep what the pass rendered
        DontCare,   // contents are undefined afterwards
    };

    // A clear color, each component in 0..1. A target stores it rounded to
    // the nearest value its format can hold; alpha in B5G5R5A1 is 1 from
    // 0.5 up.
    struct ClearColor
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        bool operator==(const ClearColor&) const = default;
    };

    // One color attachment. The null texture handle names the swapchain: the
    // window's image of this frame, in DeviceInfo::swapchainFormat. A
    // swapchain attachment must be the only color attachment of its pass.
    struct ColorAttachment
    {
        TextureHandle target;
        std::uint32_t mip = 0;
        LoadOp load = LoadOp::Clear;
        StoreOp store = StoreOp::Store;
        ClearColor clear;

        bool operator==(const ColorAttachment&) const = default;
    };

    struct DepthAttachment
    {
        TextureHandle target;
        LoadOp load = LoadOp::Clear;
        StoreOp store = StoreOp::DontCare;
        float clearDepth = 1.0f;
        std::uint8_t clearStencil = 0;

        bool operator==(const DepthAttachment&) const = default;
    };

    // The attachments of one render pass: up to kMaxColorTargets color
    // attachments and an optional depth attachment, at least one of them.
    // Texture attachments must all have the same size at the mips used.
    struct PassDesc
    {
        std::vector<ColorAttachment> colorTargets;
        std::optional<DepthAttachment> depthTarget;

        bool operator==(const PassDesc&) const = default;
    };

    // In pixels of the pass's targets, origin top left. setViewport()
    // refuses a width or height that isn't positive and a depth range other
    // than 0 <= minDepth <= maxDepth <= 1.
    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;

        bool operator==(const Viewport&) const = default;
    };

    // In pixels of the pass's targets, origin top left.
    struct ScissorRect
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool operator==(const ScissorRect&) const = default;
    };

    enum class IndexType : std::uint8_t
    {
        Uint16,
        Uint32,
    };

    // The commands a CommandList records, one struct per call, holding the
    // call's arguments as given. Backends translate them at submit; tests
    // and the Null backend compare them.
    struct BeginPass
    {
        PassDesc desc;
        bool operator==(const BeginPass&) const = default;
    };

    struct EndPass
    {
        bool operator==(const EndPass&) const = default;
    };

    struct SetPipeline
    {
        PipelineHandle pipeline;
        bool operator==(const SetPipeline&) const = default;
    };

    struct SetViewport
    {
        Viewport viewport;
        bool operator==(const SetViewport&) const = default;
    };

    struct SetScissor
    {
        ScissorRect rect;
        bool operator==(const SetScissor&) const = default;
    };

    struct BindVertexBuffer
    {
        std::uint32_t slot = 0;
        BufferHandle buffer;
        std::uint32_t offset = 0;
        bool operator==(const BindVertexBuffer&) const = default;
    };

    struct BindIndexBuffer
    {
        BufferHandle buffer;
        IndexType type = IndexType::Uint16;
        std::uint32_t offset = 0;
        bool operator==(const BindIndexBuffer&) const = default;
    };

    struct BindTexture
    {
        ShaderStage stage = ShaderStage::Fragment;
        std::uint32_t slot = 0;
        TextureHandle texture;
        SamplerHandle sampler;
        bool operator==(const BindTexture&) const = default;
    };

    struct BindStorageBuffer
    {
        ShaderStage stage = ShaderStage::Fragment;
        std::uint32_t slot = 0;
        BufferHandle buffer;
        bool operator==(const BindStorageBuffer&) const = default;
    };

    struct PushUniforms
    {
        ShaderStage stage = ShaderStage::Vertex;
        std::uint32_t slot = 0;
        std::vector<std::byte> data;
        bool operator==(const PushUniforms&) const = default;
    };

    struct Draw
    {
        std::uint32_t vertexCount = 0;
        std::uint32_t firstVertex = 0;
        bool operator==(const Draw&) const = default;
    };

    struct DrawIndexed
    {
        std::uint32_t indexCount = 0;
        std::uint32_t firstIndex = 0;
        std::int32_t vertexOffset = 0;
        bool operator==(const DrawIndexed&) const = default;
    };

    struct CopyTexture
    {
        TextureHandle source;
        TextureRegion sourceRegion;
        TextureHandle destination;
        std::uint32_t destinationMip = 0;
        std::uint32_t destinationX = 0;
        std::uint32_t destinationY = 0;
        bool operator==(const CopyTexture&) const = default;
    };

    struct UpdateBuffer
    {
        BufferHandle buffer;
        std::uint32_t offset = 0;
        std::vector<std::byte> data;
        bool operator==(const UpdateBuffer&) const = default;
    };

    using Command = std::variant<BeginPass, EndPass, SetPipeline, SetViewport, SetScissor,
        BindVertexBuffer, BindIndexBuffer, BindTexture, PushUniforms, Draw, DrawIndexed,
        CopyTexture, UpdateBuffer, BindStorageBuffer>;

    // The commands of one frame, recorded on the CPU and handed to the GPU
    // in one Device::submit(). Made only by Device::beginFrame(); valid for
    // that device and that frame only. Recording calls no backend: it checks
    // the structure of the frame here, the same for every backend, and
    // leaves everything that needs the device's resources to submit().
    //
    // Structure: render commands (setPipeline to drawIndexed) are recorded
    // only inside a pass, between beginPass() and endPass(); passes don't
    // nest; copyTexture() and updateBuffer() only outside a pass. A draw needs a setPipeline()
    // earlier in the same pass, drawIndexed() also a bindIndexBuffer(). Each
    // pass starts with the viewport covering its targets, no scissor and
    // nothing bound. A call that breaks a rule, has a slot out of range or
    // comes after the list was submitted logs one babylon::log::warning
    // line and records nothing; nothing throws. A draw of 0 vertices or
    // indices is no error and records nothing either.
    //
    // Not thread-safe: record from one thread at a time.
    class CommandList
    {
    public:
        CommandList(CommandList&&) noexcept = default;
        CommandList& operator=(CommandList&&) noexcept = default;
        CommandList(const CommandList&) = delete;
        CommandList& operator=(const CommandList&) = delete;

        // Opens a pass. Refused (false, one warning, nothing recorded) inside
        // a pass, after submit, with no attachment at all, more than
        // kMaxColorTargets color attachments, or a swapchain attachment
        // beside another color attachment.
        [[nodiscard]] bool beginPass(const PassDesc& desc);

        // Closes the open pass. Refused (false, one warning) without one.
        [[nodiscard]] bool endPass();

        void setPipeline(PipelineHandle pipeline);
        void setViewport(const Viewport& viewport);
        void setScissor(const ScissorRect& rect);

        // slot below kMaxVertexBuffers; offset in bytes.
        void bindVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint32_t offset = 0);

        // offset in bytes, a multiple of the index size (checked at submit).
        void bindIndexBuffer(BufferHandle buffer, IndexType type, std::uint32_t offset = 0);

        // slot below kMaxTextureSlots.
        void bindTexture(ShaderStage stage, std::uint32_t slot, TextureHandle texture,
            SamplerHandle sampler);

        // Binds the whole buffer, which needs Storage usage (checked at
        // submit), to storage buffer slot (below kMaxStorageBufferSlots) of
        // the stage for the draws that follow. Shaders read it; a write to
        // the buffer (updateBuffer) between passes is seen by the passes
        // after it. Binding a slot the pipeline's shader doesn't declare is
        // allowed and has no effect on its draws.
        void bindStorageBuffer(ShaderStage stage, std::uint32_t slot, BufferHandle buffer);

        // Copies the bytes into the list; they reach the shader's uniform
        // block in slot (below kMaxUniformSlots) for the draws that follow.
        // 1 to kMaxUniformBytes bytes.
        void pushUniforms(ShaderStage stage, std::uint32_t slot, std::span<const std::byte> data);

        void draw(std::uint32_t vertexCount, std::uint32_t firstVertex = 0);
        void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex = 0,
            std::int32_t vertexOffset = 0);

        // Copies sourceRegion of source to the same-sized region at
        // (destinationX, destinationY) of destinationMip. Bounds and formats
        // are checked at submit.
        void copyTexture(TextureHandle source, const TextureRegion& sourceRegion,
            TextureHandle destination, std::uint32_t destinationMip,
            std::uint32_t destinationX, std::uint32_t destinationY);

        // Copies the bytes into the list; at submit they replace data.size()
        // bytes of the buffer at offset, at this point of the list: passes
        // recorded before it see the old contents, passes after it the new
        // ones. So a frame can stream the vertices of many draws through one
        // buffer, at different offsets or the same one. Refused with one
        // warning inside a pass and for empty data; a stale buffer or a range
        // past the end is dropped at submit.
        void updateBuffer(BufferHandle buffer, std::uint32_t offset,
            std::span<const std::byte> data);

        // The commands recorded so far, in call order. A successful submit
        // hands them to the device and leaves this empty.
        const std::vector<Command>& commands() const { return m_commands; }

        bool inPass() const { return m_inPass; }

        // True once a Device::submit() accepted the list.
        bool submitted() const { return m_submitted; }

    private:
        friend class Device;

        CommandList(const void* device, std::uint64_t frame) : m_device(device), m_frame(frame) {}

        // Logs the one warning of a refused call and returns false when the
        // list can't record anymore or the call needs a pass it isn't in.
        bool checkRecording(const char* call, bool needsPass);

        const void* m_device;
        std::uint64_t m_frame;
        std::vector<Command> m_commands;
        bool m_inPass = false;
        bool m_pipelineSet = false;
        bool m_indexBufferSet = false;
        bool m_submitted = false;
    };

}
