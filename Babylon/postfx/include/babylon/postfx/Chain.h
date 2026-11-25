// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/postfx/CopyEffect.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/Resources.h>
#include <babylon/postfx/TargetPool.h>

#include <string_view>
#include <vector>

namespace babylon::render
{
    class CommandList;
    class Device;
}

namespace babylon::postfx
{

    class Effect;

    // Runs effects in order over a frame. Effects are borrowed and must outlive the chain or be
    // removed. Hdr effects run in the order added, ping-ponging between scene-sized targets of the
    // frame's color format from the pool; then the Resolve effect (the built-in CopyEffect when none
    // is set) writes LDR; then the Ldr effects in the order added, at output size in Rgba8 targets.
    // The last effect that runs writes the OutputTarget directly. An inactive effect, or one whose
    // needs the frame can't meet, is skipped. A failed effect is skipped too: its input carries on
    // to the next effect, and one warning is logged when an effect starts failing (not every frame).
    // record() never writes frame.color.
    class Chain
    {
    public:
        explicit Chain(render::Device& device);
        ~Chain();                         // calls close()
        Chain(const Chain&) = delete;
        Chain& operator=(const Chain&) = delete;

        [[nodiscard]] bool open();        // opens its Resources; false with one warning
        // Calls releaseResources() on every effect added and on the built-in copy, releases the
        // pool's targets and closes the Resources. Safe no-op when not open.
        void close();

        // Adds effect at the end of its stage. A Resolve effect replaces the current resolve. False,
        // with one warning, for an effect already added.
        bool add(Effect& effect);
        // Removes effect (calling its releaseResources()); false when it wasn't added.
        bool remove(Effect& effect);

        // Records the chain into list (the device's current frame, outside a pass) and ends the
        // pool's frame. False, with one warning and nothing recorded, when not open, frame.color is
        // null, a size is 0, or list is inside a pass. True otherwise, even when effects were
        // skipped: the output always receives an image (at worst the plain copy).
        [[nodiscard]] bool record(render::CommandList& list, const FrameInputs& frame,
            const OutputTarget& output);

        // For tests and logs: the names of the effects the last record() ran, in order.
        const std::vector<std::string_view>& lastRun() const;

    private:
        render::Device& m_device;
        Resources m_resources;
        TargetPool m_targets;
        CopyEffect m_copy;
        std::vector<Effect*> m_hdr;
        std::vector<Effect*> m_ldr;
        Effect* m_resolve = nullptr;
        std::vector<Effect*> m_failing;
        std::vector<std::string_view> m_lastRun;
        bool m_open = false;
    };

}
