// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/TargetPool.h>

#include <babylon/render/Device.h>

#include <babylon/log/Log.h>

namespace babylon::postfx
{

    TargetPool::TargetPool(render::Device& device)
        : m_device(device)
    {
    }

    TargetPool::~TargetPool()
    {
        releaseAll();
    }

    render::TextureHandle TargetPool::acquire(std::uint32_t width, std::uint32_t height,
        render::TextureFormat format)
    {
        if ( width == 0 || height == 0 )
        {
            log::warning("postfx: target pool: bad target size {}x{}", width, height);
            return {};
        }
        if ( render::isDepthFormat(format) )
        {
            log::warning("postfx: target pool: a depth format is not a scratch color target");
            return {};
        }
        if ( !m_device.supportsFormat(format,
            render::TextureUsage::Sampled | render::TextureUsage::ColorTarget) )
        {
            log::warning("postfx: target pool: the device cannot sample and render this format");
            return {};
        }

        const render::TextureDesc desc{ width, height, 1, format,
            render::TextureUsage::Sampled | render::TextureUsage::ColorTarget };
        for ( Entry& entry : m_entries )
        {
            if ( !entry.held && entry.desc == desc )
            {
                entry.held = true;
                return entry.texture;
            }
        }

        const render::TextureHandle texture = m_device.createTexture(desc);
        if ( texture.isNull() )
        {
            log::warning("postfx: target pool: the scratch texture could not be created");
            return {};
        }

        m_entries.push_back(Entry{ desc, texture, true });
        return texture;
    }

    void TargetPool::endFrame()
    {
        for ( auto it = m_entries.begin(); it != m_entries.end(); )
        {
            if ( !it->held )
            {
                m_device.destroyTexture(it->texture);
                it = m_entries.erase(it);
            }
            else
            {
                it->held = false;
                ++it;
            }
        }
    }

    void TargetPool::releaseAll()
    {
        for ( Entry& entry : m_entries )
        {
            m_device.destroyTexture(entry.texture);
        }
        m_entries.clear();
    }

    std::size_t TargetPool::textureCount() const
    {
        return m_entries.size();
    }

}
