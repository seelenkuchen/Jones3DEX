// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/render/Handle.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace babylon::render
{

    // Slots of resources addressed by Handle<Tag>, for the render backends.
    // A slot's generation starts at 1 and goes up on every erase, so old
    // handles to a reused slot are stale. A slot whose generation would wrap
    // to 0 is retired instead of reused, so no handle is ever valid twice.
    template <class Tag, class T>
    class HandlePool
    {
    public:
        using HandleType = Handle<Tag>;

        // Stores value and returns its handle; nullopt once every index is
        // used up.
        std::optional<HandleType> insert(T value)
        {
            if ( !m_free.empty() )
            {
                const std::uint32_t index = m_free.back();
                m_free.pop_back();
                Slot& slot = m_slots[index];
                slot.value.emplace(std::move(value));
                return HandleType{ index, slot.generation };
            }
            if ( m_slots.size() >= std::numeric_limits<std::uint32_t>::max() )
            {
                return std::nullopt;
            }
            const auto index = static_cast<std::uint32_t>(m_slots.size());
            m_slots.push_back(Slot{ 1, std::move(value) });
            return HandleType{ index, 1 };
        }

        // The live value of handle, or nullptr for null and stale handles.
        T* find(HandleType handle)
        {
            if ( handle.isNull() || handle.index >= m_slots.size() )
            {
                return nullptr;
            }
            Slot& slot = m_slots[handle.index];
            return slot.generation == handle.generation && slot.value.has_value()
                ? &*slot.value : nullptr;
        }

        const T* find(HandleType handle) const
        {
            return const_cast<HandlePool*>(this)->find(handle);
        }

        // Destroys the value of a live handle; false for null and stale ones.
        bool erase(HandleType handle)
        {
            if ( find(handle) == nullptr )
            {
                return false;
            }
            Slot& slot = m_slots[handle.index];
            slot.value.reset();
            ++slot.generation;
            if ( slot.generation != 0 )
            {
                m_free.push_back(handle.index);
            }
            return true;
        }

        // Calls visit(value) for every live value, for backends that release
        // what is left when the device goes.
        template <class Visit>
        void forEach(Visit visit)
        {
            for ( Slot& slot : m_slots )
            {
                if ( slot.value.has_value() )
                {
                    visit(*slot.value);
                }
            }
        }

    private:
        struct Slot
        {
            std::uint32_t generation;
            std::optional<T> value;
        };

        std::vector<Slot> m_slots;
        std::vector<std::uint32_t> m_free;
    };

}
