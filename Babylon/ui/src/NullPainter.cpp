// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/ui/NullPainter.h>

#include <utility>

namespace babylon::ui
{

    struct NullPainter::State
    {
        float glyphWidth = 0.0f;
        float lineHeight = 0.0f;
        bool record = true;
        std::vector<RecordedCall> calls;
    };

    NullPainter::NullPainter(const NullPainterDesc& desc)
        : m_state(std::make_unique<State>())
    {
        m_state->glyphWidth = desc.glyphWidth > 0.0f ? desc.glyphWidth : 0.0f;
        m_state->lineHeight = desc.lineHeight > 0.0f ? desc.lineHeight : 0.0f;
        m_state->record = desc.record;
    }

    NullPainter::~NullPainter() = default;

    void NullPainter::fillRect(const Rect& rect, const Color& color)
    {
        if ( m_state->record )
        {
            m_state->calls.emplace_back(RecordedFillRect{ rect, color });
        }
    }

    void NullPainter::drawImage(ImageId image, const Rect& rect)
    {
        if ( m_state->record )
        {
            m_state->calls.emplace_back(RecordedDrawImage{ image, rect });
        }
    }

    void NullPainter::drawText(FontId font, std::string_view text, const Rect& rect,
        TextAlign align, const Color& color)
    {
        if ( m_state->record )
        {
            m_state->calls.emplace_back(RecordedDrawText{ font, std::string(text),
                rect, align, color });
        }
    }

    Size NullPainter::measureText(FontId, std::string_view text) const
    {
        return Size{ static_cast<float>(text.size()) * m_state->glyphWidth,
            m_state->lineHeight };
    }

    void NullPainter::pushClip(const Rect& rect)
    {
        if ( m_state->record )
        {
            m_state->calls.emplace_back(RecordedPushClip{ rect });
        }
    }

    void NullPainter::popClip()
    {
        if ( m_state->record )
        {
            m_state->calls.emplace_back(RecordedPopClip{});
        }
    }

    const std::vector<RecordedCall>& NullPainter::recorded() const
    {
        return m_state->calls;
    }

    void NullPainter::clearRecorded()
    {
        m_state->calls.clear();
    }

}
