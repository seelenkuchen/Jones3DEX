// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/render/CommandList.h>

#include <babylon/log/Log.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace babylon::render
{

    bool CommandList::checkRecording(const char* call, bool needsPass)
    {
        if ( m_submitted )
        {
            log::warning("render: {}: the command list was already submitted", call);
            return false;
        }
        if ( needsPass && !m_inPass )
        {
            log::warning("render: {}: recorded outside a pass", call);
            return false;
        }
        return true;
    }

    bool CommandList::beginPass(const PassDesc& desc)
    {
        if ( !checkRecording("beginPass", false) )
        {
            return false;
        }
        if ( m_inPass )
        {
            log::warning("render: beginPass: a pass is already open; passes don't nest");
            return false;
        }
        if ( desc.colorTargets.empty() && !desc.depthTarget.has_value() )
        {
            log::warning("render: beginPass: the pass has no attachment");
            return false;
        }
        if ( desc.colorTargets.size() > kMaxColorTargets )
        {
            log::warning("render: beginPass: {} color attachments, at most {} allowed",
                desc.colorTargets.size(), kMaxColorTargets);
            return false;
        }
        if ( desc.colorTargets.size() > 1 )
        {
            for ( const ColorAttachment& attachment : desc.colorTargets )
            {
                if ( attachment.target.isNull() )
                {
                    log::warning("render: beginPass: the swapchain must be the only color "
                        "attachment of its pass");
                    return false;
                }
            }
        }
        m_commands.emplace_back(BeginPass{ desc });
        m_inPass = true;
        m_pipelineSet = false;
        m_indexBufferSet = false;
        return true;
    }

    bool CommandList::endPass()
    {
        if ( !checkRecording("endPass", false) )
        {
            return false;
        }
        if ( !m_inPass )
        {
            log::warning("render: endPass: no pass is open");
            return false;
        }
        m_commands.emplace_back(EndPass{});
        m_inPass = false;
        return true;
    }

    void CommandList::setPipeline(PipelineHandle pipeline)
    {
        if ( !checkRecording("setPipeline", true) )
        {
            return;
        }
        m_commands.emplace_back(SetPipeline{ pipeline });
        m_pipelineSet = true;
    }

    void CommandList::setViewport(const Viewport& viewport)
    {
        if ( !checkRecording("setViewport", true) )
        {
            return;
        }
        // Written so that NaN fails every comparison and is refused too.
        const bool valid = viewport.width > 0.0f && viewport.height > 0.0f
            && viewport.minDepth >= 0.0f && viewport.minDepth <= viewport.maxDepth
            && viewport.maxDepth <= 1.0f;
        if ( !valid )
        {
            log::warning("render: setViewport: {}x{} with depth {}..{}: the size must be "
                "positive and 0 <= minDepth <= maxDepth <= 1", viewport.width, viewport.height,
                viewport.minDepth, viewport.maxDepth);
            return;
        }
        m_commands.emplace_back(SetViewport{ viewport });
    }

    void CommandList::setScissor(const ScissorRect& rect)
    {
        if ( !checkRecording("setScissor", true) )
        {
            return;
        }
        m_commands.emplace_back(SetScissor{ rect });
    }

    void CommandList::bindVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint32_t offset)
    {
        if ( !checkRecording("bindVertexBuffer", true) )
        {
            return;
        }
        if ( slot >= kMaxVertexBuffers )
        {
            log::warning("render: bindVertexBuffer: slot {} out of range, at most {}",
                slot, kMaxVertexBuffers - 1);
            return;
        }
        m_commands.emplace_back(BindVertexBuffer{ slot, buffer, offset });
    }

    void CommandList::bindIndexBuffer(BufferHandle buffer, IndexType type, std::uint32_t offset)
    {
        if ( !checkRecording("bindIndexBuffer", true) )
        {
            return;
        }
        m_commands.emplace_back(BindIndexBuffer{ buffer, type, offset });
        m_indexBufferSet = true;
    }

    void CommandList::bindTexture(ShaderStage stage, std::uint32_t slot, TextureHandle texture,
        SamplerHandle sampler)
    {
        if ( !checkRecording("bindTexture", true) )
        {
            return;
        }
        if ( slot >= kMaxTextureSlots )
        {
            log::warning("render: bindTexture: slot {} out of range, at most {}",
                slot, kMaxTextureSlots - 1);
            return;
        }
        m_commands.emplace_back(BindTexture{ stage, slot, texture, sampler });
    }

    void CommandList::pushUniforms(ShaderStage stage, std::uint32_t slot,
        std::span<const std::byte> data)
    {
        if ( !checkRecording("pushUniforms", true) )
        {
            return;
        }
        if ( slot >= kMaxUniformSlots )
        {
            log::warning("render: pushUniforms: slot {} out of range, at most {}",
                slot, kMaxUniformSlots - 1);
            return;
        }
        if ( data.empty() || data.size() > kMaxUniformBytes )
        {
            log::warning("render: pushUniforms: {} bytes, must be within 1 and {}",
                data.size(), kMaxUniformBytes);
            return;
        }
        m_commands.emplace_back(PushUniforms{ stage, slot,
            std::vector<std::byte>(data.begin(), data.end()) });
    }

    void CommandList::draw(std::uint32_t vertexCount, std::uint32_t firstVertex)
    {
        if ( !checkRecording("draw", true) )
        {
            return;
        }
        if ( !m_pipelineSet )
        {
            log::warning("render: draw: no pipeline was set in this pass");
            return;
        }
        if ( vertexCount == 0 )
        {
            return;   // nothing to draw, and no error
        }
        m_commands.emplace_back(Draw{ vertexCount, firstVertex });
    }

    void CommandList::drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex,
        std::int32_t vertexOffset)
    {
        if ( !checkRecording("drawIndexed", true) )
        {
            return;
        }
        if ( !m_pipelineSet )
        {
            log::warning("render: drawIndexed: no pipeline was set in this pass");
            return;
        }
        if ( !m_indexBufferSet )
        {
            log::warning("render: drawIndexed: no index buffer was bound in this pass");
            return;
        }
        if ( indexCount == 0 )
        {
            return;   // nothing to draw, and no error
        }
        m_commands.emplace_back(DrawIndexed{ indexCount, firstIndex, vertexOffset });
    }

    void CommandList::copyTexture(TextureHandle source, const TextureRegion& sourceRegion,
        TextureHandle destination, std::uint32_t destinationMip, std::uint32_t destinationX,
        std::uint32_t destinationY)
    {
        if ( !checkRecording("copyTexture", false) )
        {
            return;
        }
        if ( m_inPass )
        {
            // Copies run outside render passes on every GPU API this maps to.
            log::warning("render: copyTexture: recorded inside a pass");
            return;
        }
        m_commands.emplace_back(CopyTexture{ source, sourceRegion, destination, destinationMip,
            destinationX, destinationY });
    }

}
