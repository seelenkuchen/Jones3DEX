// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/shadow/StaticCasterSet.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/Device.h>

#include <babylon/log/Log.h>

#include <cstdint>
#include <limits>

namespace babylon::shadow
{

    StaticCasterSet::StaticCasterSet(render::Device& device)
        : m_device(device)
    {
    }

    StaticCasterSet::~StaticCasterSet()
    {
        clear();
    }

    bool StaticCasterSet::upload(std::span<const float> positions)
    {
        if ( positions.size() % 9 != 0 )
        {
            log::warning("shadow: a static caster set needs whole triangles, but {} floats is "
                "not a multiple of 9", positions.size());
            return false;
        }
        if ( positions.empty() )
        {
            clear();
            return true;
        }

        const std::uint64_t bytes = positions.size() * sizeof(float);
        const std::uint64_t maxBytes = m_device.info().limits.maxBufferSize;
        if ( bytes > maxBytes || bytes > std::numeric_limits<std::uint32_t>::max() )
        {
            log::warning("shadow: the static caster vertices do not fit in the vertex buffer");
            return false;
        }

        if ( m_buffer.isNull() || m_capacityBytes < bytes )
        {
            if ( !m_buffer.isNull() )
            {
                m_device.destroyBuffer(m_buffer);
                m_buffer = {};
                m_capacityBytes = 0;
            }
            m_buffer = m_device.createBuffer(render::BufferDesc{
                static_cast<std::uint32_t>(bytes), render::BufferUsage::Vertex });
            if ( m_buffer.isNull() )
            {
                return false;
            }
            m_capacityBytes = bytes;
        }

        if ( !m_device.uploadBuffer(m_buffer, 0, std::as_bytes(positions)) )
        {
            clear();
            return false;
        }

        Bounds bounds = {
            { positions[0], positions[1], positions[2] },
            { positions[0], positions[1], positions[2] },
        };
        for ( std::size_t index = 3; index + 2 < positions.size(); index += 3 )
        {
            for ( int axis = 0; axis < 3; ++axis )
            {
                const float value = positions[index + static_cast<std::size_t>(axis)];
                bounds.min[axis] = value < bounds.min[axis] ? value : bounds.min[axis];
                bounds.max[axis] = value > bounds.max[axis] ? value : bounds.max[axis];
            }
        }

        m_numVertices = positions.size() / 3;
        m_bounds      = bounds;
        return true;
    }

    void StaticCasterSet::clear()
    {
        if ( !m_buffer.isNull() )
        {
            m_device.destroyBuffer(m_buffer);
            m_buffer = {};
        }
        m_capacityBytes = 0;
        m_numVertices   = 0;
        m_bounds        = Bounds{};
    }

    std::size_t StaticCasterSet::numVertices() const
    {
        return m_numVertices;
    }

    render::BufferHandle StaticCasterSet::buffer() const
    {
        return m_buffer;
    }

    Bounds StaticCasterSet::bounds() const
    {
        return m_bounds;
    }

}
