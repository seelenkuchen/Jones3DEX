// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>

#include <babylon/postfx/Effect.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>

#include <babylon/log/Log.h>

#include <algorithm>
#include <cstddef>

namespace babylon::postfx
{

    Chain::Chain(render::Device& device)
        : m_device(device)
        , m_resources(device)
        , m_targets(device)
    {
    }

    Chain::~Chain()
    {
        close();
    }

    bool Chain::open()
    {
        if ( m_open )
        {
            return true;
        }
        if ( !m_resources.open() )
        {
            return false;
        }
        m_open = true;
        m_failing.clear();
        return true;
    }

    void Chain::close()
    {
        if ( !m_open )
        {
            return;
        }

        for ( Effect* pEffect : m_hdr )
        {
            pEffect->releaseResources();
        }
        for ( Effect* pEffect : m_ldr )
        {
            pEffect->releaseResources();
        }
        if ( m_resolve != nullptr )
        {
            m_resolve->releaseResources();
        }
        m_copy.releaseResources();

        m_targets.releaseAll();
        m_resources.close();
        m_failing.clear();
        m_lastRun.clear();
        m_open = false;
    }

    bool Chain::add(Effect& effect)
    {
        const auto contains = [&effect](const std::vector<Effect*>& effects)
        {
            return std::find(effects.begin(), effects.end(), &effect) != effects.end();
        };
        if ( contains(m_hdr) || contains(m_ldr) || m_resolve == &effect )
        {
            log::warning("postfx: chain: the effect '{}' was already added", effect.name());
            return false;
        }

        switch ( effect.stage() )
        {
            case Stage::Hdr:
                m_hdr.push_back(&effect);
                break;
            case Stage::Resolve:
                if ( m_resolve != nullptr )
                {
                    m_resolve->releaseResources();
                }
                m_resolve = &effect;
                break;
            case Stage::Ldr:
                m_ldr.push_back(&effect);
                break;
        }
        return true;
    }

    bool Chain::remove(Effect& effect)
    {
        const auto erase = [&effect](std::vector<Effect*>& effects)
        {
            const auto it = std::find(effects.begin(), effects.end(), &effect);
            if ( it == effects.end() )
            {
                return false;
            }
            effects.erase(it);
            return true;
        };

        bool removed = erase(m_hdr);
        if ( !removed )
        {
            removed = erase(m_ldr);
        }
        if ( !removed && m_resolve == &effect )
        {
            m_resolve = nullptr;
            removed   = true;
        }
        if ( !removed )
        {
            return false;
        }

        effect.releaseResources();
        m_failing.erase(std::remove(m_failing.begin(), m_failing.end(), &effect),
            m_failing.end());
        return true;
    }

    bool Chain::record(render::CommandList& list, const FrameInputs& frame,
        const OutputTarget& output)
    {
        if ( !m_open )
        {
            log::warning("postfx: chain: record needs an open chain");
            return false;
        }
        if ( frame.color.isNull() )
        {
            log::warning("postfx: chain: the frame has no color texture");
            return false;
        }
        if ( frame.width == 0 || frame.height == 0 )
        {
            log::warning("postfx: chain: the frame has a zero size");
            return false;
        }
        if ( output.width == 0 || output.height == 0 )
        {
            log::warning("postfx: chain: the output has a zero size");
            return false;
        }
        if ( list.inPass() )
        {
            log::warning("postfx: chain: record needs a command list outside a pass");
            return false;
        }

        m_lastRun.clear();

        const render::TextureFormat outputFormat = output.texture.isNull()
            ? m_device.info().swapchainFormat : output.format;

        const auto canRun = [&frame](const Effect& effect)
        {
            if ( !effect.active() )
            {
                return false;
            }
            const Needs needs = effect.needs();
            if ( needs.depth && frame.depth.isNull() )
            {
                return false;
            }
            if ( needs.normals && frame.normals.isNull() )
            {
                return false;
            }
            return true;
        };
        const auto noteFailure = [this](Effect& effect)
        {
            if ( std::find(m_failing.begin(), m_failing.end(), &effect) == m_failing.end() )
            {
                m_failing.push_back(&effect);
                log::warning("postfx: chain: the effect '{}' failed and is skipped",
                    effect.name());
            }
        };
        const auto noteSuccess = [this](Effect& effect)
        {
            m_failing.erase(std::remove(m_failing.begin(), m_failing.end(), &effect),
                m_failing.end());
        };

        render::TextureHandle input       = frame.color;
        render::TextureFormat inputFormat = frame.colorFormat;
        std::uint32_t inputWidth          = frame.width;
        std::uint32_t inputHeight         = frame.height;

        // Hdr effects ping-pong between two scene-sized targets of the frame's color format.
        render::TextureHandle hdrTargets[2];
        for ( Effect* pEffect : m_hdr )
        {
            if ( !canRun(*pEffect) )
            {
                continue;
            }

            render::TextureHandle& slot =
                (input == hdrTargets[0]) ? hdrTargets[1] : hdrTargets[0];
            if ( slot.isNull() )
            {
                slot = m_targets.acquire(frame.width, frame.height, frame.colorFormat);
            }
            if ( slot.isNull() )
            {
                continue;
            }

            PassContext ctx{ list, m_resources, m_targets, frame, input, inputFormat,
                inputWidth, inputHeight, slot, frame.colorFormat, frame.width, frame.height };
            if ( pEffect->record(ctx) )
            {
                noteSuccess(*pEffect);
                m_lastRun.push_back(pEffect->name());
                input       = slot;
                inputFormat = frame.colorFormat;
                inputWidth  = frame.width;
                inputHeight = frame.height;
            }
            else
            {
                noteFailure(*pEffect);
            }
        }

        // The eligible Ldr effects decide whether the resolve writes a scratch target or the
        // OutputTarget directly.
        std::vector<Effect*> ldrRun;
        for ( Effect* pEffect : m_ldr )
        {
            if ( canRun(*pEffect) )
            {
                ldrRun.push_back(pEffect);
            }
        }

        Effect* pResolve = m_resolve != nullptr ? m_resolve : &m_copy;
        if ( !canRun(*pResolve) )
        {
            // The built-in copy always runs, so a skipped custom resolve still yields an image.
            pResolve = &m_copy;
        }

        render::TextureHandle ldrTargets[2];
        bool ldrCanRun     = !ldrRun.empty();
        bool outputWritten = false;
        bool resolveWrote  = false;

        if ( canRun(*pResolve) )
        {
            bool resolveToOutput = !ldrCanRun;
            render::TextureHandle resolveOutput;
            render::TextureFormat resolveFormat = outputFormat;
            if ( resolveToOutput )
            {
                resolveOutput = output.texture;
            }
            else
            {
                render::TextureHandle& slot =
                    (input == ldrTargets[0]) ? ldrTargets[1] : ldrTargets[0];
                if ( slot.isNull() )
                {
                    slot = m_targets.acquire(output.width, output.height,
                        render::TextureFormat::Rgba8);
                }
                if ( slot.isNull() )
                {
                    // No scratch target: write straight to the output and skip the Ldr stage.
                    ldrCanRun       = false;
                    resolveToOutput = true;
                    resolveOutput   = output.texture;
                }
                else
                {
                    resolveOutput = slot;
                    resolveFormat = render::TextureFormat::Rgba8;
                }
            }

            PassContext ctx{ list, m_resources, m_targets, frame, input, inputFormat,
                inputWidth, inputHeight, resolveOutput, resolveFormat, output.width,
                output.height };
            if ( pResolve->record(ctx) )
            {
                noteSuccess(*pResolve);
                m_lastRun.push_back(pResolve->name());
                input        = resolveOutput;
                inputFormat  = resolveFormat;
                inputWidth   = output.width;
                inputHeight  = output.height;
                resolveWrote = true;
                if ( resolveToOutput )
                {
                    outputWritten = true;
                }
            }
            else
            {
                noteFailure(*pResolve);
                ldrCanRun = false;
            }
        }
        else
        {
            ldrCanRun = false;
        }

        // Ldr effects run at output size, ping-ponging in Rgba8 targets; the last one writes the
        // OutputTarget directly.
        if ( resolveWrote && ldrCanRun )
        {
            for ( std::size_t index = 0; index < ldrRun.size(); ++index )
            {
                Effect* pEffect   = ldrRun[index];
                const bool isLast = (index + 1 == ldrRun.size());

                render::TextureHandle passOutput;
                render::TextureFormat passFormat = render::TextureFormat::Rgba8;
                if ( isLast )
                {
                    passOutput = output.texture;
                    passFormat = outputFormat;
                }
                else
                {
                    render::TextureHandle& slot =
                        (input == ldrTargets[0]) ? ldrTargets[1] : ldrTargets[0];
                    if ( slot.isNull() )
                    {
                        slot = m_targets.acquire(output.width, output.height,
                            render::TextureFormat::Rgba8);
                    }
                    if ( slot.isNull() )
                    {
                        continue;
                    }
                    passOutput = slot;
                }

                PassContext ctx{ list, m_resources, m_targets, frame, input, inputFormat,
                    inputWidth, inputHeight, passOutput, passFormat, output.width,
                    output.height };
                if ( pEffect->record(ctx) )
                {
                    noteSuccess(*pEffect);
                    m_lastRun.push_back(pEffect->name());
                    input       = passOutput;
                    inputFormat = passFormat;
                    inputWidth  = output.width;
                    inputHeight = output.height;
                    if ( isLast )
                    {
                        outputWritten = true;
                    }
                }
                else
                {
                    noteFailure(*pEffect);
                }
            }
        }

        // A skipped or failed final effect must still leave an image in the output.
        if ( !outputWritten )
        {
            PassContext ctx{ list, m_resources, m_targets, frame, input, inputFormat,
                inputWidth, inputHeight, output.texture, outputFormat, output.width,
                output.height };
            if ( m_copy.record(ctx) )
            {
                noteSuccess(m_copy);
                m_lastRun.push_back(m_copy.name());
            }
            else
            {
                noteFailure(m_copy);
            }
        }

        m_targets.endFrame();
        return true;
    }

    const std::vector<std::string_view>& Chain::lastRun() const
    {
        return m_lastRun;
    }

}
