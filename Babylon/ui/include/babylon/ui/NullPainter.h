// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/ui/Painter.h>
#include <babylon/ui/Types.h>

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace babylon::ui
{

    // One fillRect call as it was given.
    struct RecordedFillRect
    {
        Rect rect;
        Color color;
        bool operator==(const RecordedFillRect&) const = default;
    };

    // One drawImage call as it was given.
    struct RecordedDrawImage
    {
        ImageId image = 0;
        Rect rect;
        bool operator==(const RecordedDrawImage&) const = default;
    };

    // One drawText call as it was given. The text is copied, so it stays
    // valid as long as the record does.
    struct RecordedDrawText
    {
        FontId font = 0;
        std::string text;
        Rect rect;
        TextAlign align = TextAlign::Left;
        Color color;
        bool operator==(const RecordedDrawText&) const = default;
    };

    // One pushClip call as it was given.
    struct RecordedPushClip
    {
        Rect rect;
        bool operator==(const RecordedPushClip&) const = default;
    };

    // The matching popClip call.
    struct RecordedPopClip
    {
        bool operator==(const RecordedPopClip&) const = default;
    };

    // One entry of a NullPainter's record, in call order.
    using RecordedCall = std::variant<RecordedFillRect, RecordedDrawImage,
        RecordedDrawText, RecordedPushClip, RecordedPopClip>;

    // How a NullPainter measures and records.
    struct NullPainterDesc
    {
        // The advance width of one byte of text and the height of one line,
        // both in canvas units, so measureText() is deterministic and needs
        // no font data. Negative values are treated as 0.
        float glyphWidth = 8.0f;
        float lineHeight = 16.0f;

        // Whether calls are kept in recorded(). Off, recorded() stays empty
        // and a long run keeps no memory per frame; measureText() is
        // unchanged.
        bool record = true;
    };

    // Painter that draws nothing and records what a Ui asked it to draw, for
    // tests and headless runs. It honors the whole Painter contract:
    // pushClip/popClip are recorded in order but clipping itself has no
    // effect because nothing is rasterized, and measureText() is
    // text.size() * glyphWidth wide and lineHeight high.
    //
    // Every accepted call after construction is appended to recorded() when
    // recording is on. Negative glyphWidth or lineHeight is normalized to 0;
    // calls with a non-positive rectangle or a zero image or font id are
    // still accepted and recorded, because recording is what tests compare.
    class NullPainter final : public Painter
    {
    public:
        explicit NullPainter(const NullPainterDesc& desc = {});
        ~NullPainter() override;
        NullPainter(const NullPainter&) = delete;
        NullPainter& operator=(const NullPainter&) = delete;

        void fillRect(const Rect& rect, const Color& color) override;
        void drawImage(ImageId image, const Rect& rect) override;
        void drawText(FontId font, std::string_view text, const Rect& rect,
            TextAlign align, const Color& color) override;
        Size measureText(FontId font, std::string_view text) const override;
        void pushClip(const Rect& rect) override;
        void popClip() override;

        // Every call recorded since construction or the last clearRecorded().
        // The reference stays valid while the painter lives, but later calls
        // move the vector's contents; re-fetch elements after every draw.
        const std::vector<RecordedCall>& recorded() const;

        // Forgets all recorded calls.
        void clearRecorded();

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };

}
