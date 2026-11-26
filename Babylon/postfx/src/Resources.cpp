// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Resources.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Sampler.h>
#include <babylon/shaders/babylon_postfx.h>

#include <babylon/log/Log.h>

#include <cstdint>

namespace babylon::postfx
{
    namespace
    {
        render::SamplerDesc clampSampler(render::Filter filter)
        {
            render::SamplerDesc desc;
            desc.minFilter = filter;
            desc.magFilter = filter;
            desc.mipFilter = render::Filter::Nearest;
            desc.addressU  = render::AddressMode::ClampToEdge;
            desc.addressV  = render::AddressMode::ClampToEdge;
            return desc;
        }
    }

    Resources::Resources(render::Device& device)
        : m_device(device)
    {
    }

    Resources::~Resources()
    {
        close();
    }

    bool Resources::open()
    {
        if ( m_open )
        {
            return true;
        }

        m_linearClamp = m_device.createSampler(clampSampler(render::Filter::Linear));
        if ( m_linearClamp.isNull() )
        {
            log::warning("postfx: the linear clamp sampler could not be created");
            return false;
        }

        m_nearestClamp = m_device.createSampler(clampSampler(render::Filter::Nearest));
        if ( m_nearestClamp.isNull() )
        {
            m_device.destroySampler(m_linearClamp);
            m_linearClamp = {};
            log::warning("postfx: the nearest clamp sampler could not be created");
            return false;
        }

        m_open = true;
        return true;
    }

    void Resources::close()
    {
        if ( !m_open )
        {
            return;
        }

        if ( !m_linearClamp.isNull() )
        {
            m_device.destroySampler(m_linearClamp);
            m_linearClamp = {};
        }
        if ( !m_nearestClamp.isNull() )
        {
            m_device.destroySampler(m_nearestClamp);
            m_nearestClamp = {};
        }
        m_open = false;
    }

    bool Resources::isOpen() const
    {
        return m_open;
    }

    render::Device& Resources::device() const
    {
        return m_device;
    }

    render::SamplerHandle Resources::linearClamp() const
    {
        return m_linearClamp;
    }

    render::SamplerHandle Resources::nearestClamp() const
    {
        return m_nearestClamp;
    }

    const render::ShaderDesc& Resources::fullscreenVertexShader()
    {
        return shaders::fullscreenVert();
    }

    bool Resources::recordFullscreenPass(render::CommandList& list, render::TextureHandle target,
        render::PipelineHandle pipeline, std::span<const render::TextureHandle> textures,
        render::SamplerHandle sampler, std::span<const std::byte> uniforms)
    {
        if ( list.inPass() )
        {
            log::warning("postfx: a full-screen pass needs a command list outside a pass");
            return false;
        }

        render::PassDesc pass;
        render::ColorAttachment attachment;
        attachment.target = target;
        attachment.load   = render::LoadOp::DontCare;
        attachment.store  = render::StoreOp::Store;
        pass.colorTargets.push_back(attachment);
        if ( !list.beginPass(pass) )
        {
            return false;
        }

        list.setPipeline(pipeline);
        for ( std::size_t index = 0; index < textures.size(); ++index )
        {
            list.bindTexture(render::ShaderStage::Fragment, static_cast<std::uint32_t>(index),
                textures[index], sampler);
        }
        if ( !uniforms.empty() )
        {
            list.pushUniforms(render::ShaderStage::Fragment, 0, uniforms);
        }
        list.draw(3);
        return list.endPass();
    }

}
