// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/Texture.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace babylon::render
{

    // Fixed limits of the command and pipeline model, the same on every
    // backend so callers never branch on the device for them.
    inline constexpr std::uint32_t kMaxColorTargets = 4;     // color attachments of a pass
    inline constexpr std::uint32_t kMaxVertexBuffers = 4;    // vertex buffer slots
    inline constexpr std::uint32_t kMaxVertexAttributes = 16;
    inline constexpr std::uint32_t kMaxTextureSlots = 8;     // per shader stage
    inline constexpr std::uint32_t kMaxUniformSlots = 4;     // per shader stage
    inline constexpr std::uint32_t kMaxUniformBytes = 4096;  // per pushUniforms() call

    // Compiled shader code formats (ADR-0022). A device accepts some of
    // them (DeviceInfo::shaderFormats); a pipeline carries one blob per format
    // it was built for and the device picks the one it accepts.
    enum class ShaderFormat : std::uint8_t
    {
        None = 0,
        SpirV = 1 << 0,
        Dxil = 1 << 1,
        Msl = 1 << 2,
    };

    constexpr ShaderFormat operator|(ShaderFormat a, ShaderFormat b)
    {
        return static_cast<ShaderFormat>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    // True when formats contains every flag in format.
    constexpr bool hasFormat(ShaderFormat formats, ShaderFormat format)
    {
        return (static_cast<std::uint8_t>(formats) & static_cast<std::uint8_t>(format))
            == static_cast<std::uint8_t>(format);
    }

    enum class ShaderStage : std::uint8_t
    {
        Vertex,
        Fragment,
    };

    // One compiled form of a shader: exactly one format flag, the code, and
    // the entry point the code names.
    struct ShaderBlob
    {
        ShaderFormat format = ShaderFormat::None;
        std::vector<std::byte> code;
        std::string entryPoint = "main";

        bool operator==(const ShaderBlob&) const = default;
    };

    // One shader stage of a pipeline: its blobs and the resources it reads.
    // The counts are what the code declares; the device needs them to build
    // the pipeline, and bindTexture() and pushUniforms() slots must be below
    // them at draw time.
    struct ShaderDesc
    {
        std::vector<ShaderBlob> blobs;
        std::uint32_t textureCount = 0;   // at most kMaxTextureSlots
        std::uint32_t uniformCount = 0;   // at most kMaxUniformSlots

        bool operator==(const ShaderDesc&) const = default;
    };

    enum class VertexFormat : std::uint8_t
    {
        Float1,
        Float2,
        Float3,
        Float4,
        UByte4Norm,   // four bytes, each read as byte / 255
    };

    // Size in bytes of one attribute of a format.
    constexpr std::uint32_t vertexFormatSize(VertexFormat format)
    {
        switch ( format )
        {
            case VertexFormat::Float1: return 4;
            case VertexFormat::Float2: return 8;
            case VertexFormat::Float3: return 12;
            case VertexFormat::Float4: return 16;
            case VertexFormat::UByte4Norm: return 4;
        }
        return 0;
    }

    // One attribute: the shader input location, the vertex buffer slot it
    // comes from, and its offset inside one vertex of that slot.
    struct VertexAttribute
    {
        std::uint32_t location = 0;
        std::uint32_t bufferSlot = 0;
        VertexFormat format = VertexFormat::Float4;
        std::uint32_t offset = 0;

        bool operator==(const VertexAttribute&) const = default;
    };

    // The vertex input of a pipeline. bufferStrides[slot] is the distance
    // in bytes between two vertices in that slot, at most kMaxVertexBuffers
    // slots; attributes must fit inside their slot's stride and use distinct
    // locations. An empty layout means the shader makes its own vertices.
    struct VertexLayout
    {
        std::vector<std::uint32_t> bufferStrides;
        std::vector<VertexAttribute> attributes;

        bool operator==(const VertexLayout&) const = default;
    };

    enum class PrimitiveType : std::uint8_t
    {
        TriangleList,
        TriangleStrip,
        LineList,
        LineStrip,
        PointList,
    };

    enum class BlendFactor : std::uint8_t
    {
        Zero,
        One,
        SrcColor,
        OneMinusSrcColor,
        DstColor,
        OneMinusDstColor,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstAlpha,
        OneMinusDstAlpha,
    };

    enum class BlendOp : std::uint8_t
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    // Blending of one color target. Disabled means the fragment replaces the
    // target.
    struct BlendState
    {
        bool enabled = false;
        BlendFactor srcColor = BlendFactor::One;
        BlendFactor dstColor = BlendFactor::Zero;
        BlendOp colorOp = BlendOp::Add;
        BlendFactor srcAlpha = BlendFactor::One;
        BlendFactor dstAlpha = BlendFactor::Zero;
        BlendOp alphaOp = BlendOp::Add;

        bool operator==(const BlendState&) const = default;
    };

    // One color target of a pipeline: the format it renders to, which must
    // match the attachment of the pass it is used in, and its blending.
    struct ColorTargetDesc
    {
        TextureFormat format = TextureFormat::Bgra8;
        BlendState blend;

        bool operator==(const ColorTargetDesc&) const = default;
    };

    enum class CompareOp : std::uint8_t
    {
        Never,
        Less,
        Equal,
        LessOrEqual,
        Greater,
        NotEqual,
        GreaterOrEqual,
        Always,
    };

    // Depth test and write. Either needs PipelineDesc::depthFormat.
    struct DepthState
    {
        bool testEnabled = false;
        bool writeEnabled = false;
        CompareOp compare = CompareOp::LessOrEqual;

        bool operator==(const DepthState&) const = default;
    };

    enum class CullMode : std::uint8_t
    {
        None,
        Front,
        Back,
    };

    // Which winding is the front of a triangle, in framebuffer coordinates.
    enum class FrontFace : std::uint8_t
    {
        CounterClockwise,
        Clockwise,
    };

    // Everything fixed about a draw. Device::createPipeline() refuses a stage
    // without a nonempty blob in a format the device accepts, more than
    // kMaxColorTargets color targets, no color target and no depth format, a
    // color target in a depth format or one the device can't render to, a
    // depth format that isn't one, a depth test or write without a depth
    // format, and a vertex layout that breaks the rules of VertexLayout.
    struct PipelineDesc
    {
        ShaderDesc vertexShader;
        ShaderDesc fragmentShader;
        VertexLayout vertexLayout;
        PrimitiveType primitive = PrimitiveType::TriangleList;
        std::vector<ColorTargetDesc> colorTargets;
        std::optional<TextureFormat> depthFormat;
        DepthState depth;
        CullMode cull = CullMode::None;
        FrontFace frontFace = FrontFace::CounterClockwise;

        bool operator==(const PipelineDesc&) const = default;
    };

}
