// Added: the drawing and layout helpers of the Button Layout screen.
#include <Jones3D/Platform/Babylon/JonesUiButtonLayout.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>
#include <Jones3D/Platform/Babylon/JonesUiAssets.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiPrompts.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>

#include <sith/Devices/sithControl.h>

#include <babylon/files/FileSystem.h>
#include <babylon/platform/Keys.h>
#include <babylon/input/InputState.h>
#include <babylon/ui/Binding.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <Jones3D/Platform/Babylon/JonesUiButtonLayoutInternal.h>

using namespace JonesUiButtonLayoutInternal;

namespace
{
    constexpr float kCanvasHeight = 480.0f;
    constexpr float kMargin       = 24.0f;
    constexpr float kHeaderRuleY  = 76.0f;
    constexpr float kListTop      = 94.0f;
    constexpr float kListGap      = 6.0f;
    constexpr float kListRowHeight = 34.0f;
    constexpr float kResetRowHeight = 28.0f;
    constexpr float kIconSize      = 24.0f;
    constexpr float kScrollWidth   = 4.0f;
    constexpr float kListWidthFraction = 0.40f;

    constexpr const char* kCategoryController = "CONTROLLER";          // TODO: [COMPAT]
    constexpr const char* kCategoryKeyboard   = "KEYBOARD & MOUSE";    // TODO: [COMPAT]

    constexpr const char* kPressPadText       = "Press a button...";   // TODO: [COMPAT]
    constexpr const char* kPressKeyText       = "Press a key...";      // TODO: [COMPAT]
    constexpr const char* kUnboundText        = "Not bound";           // TODO: [COMPAT]

    constexpr babylon::ui::Color kRowFill      = { 0.03f, 0.03f, 0.05f, 0.72f };
    constexpr babylon::ui::Color kRowBorder    = { 0.80f, 0.76f, 0.66f, 0.35f };
    constexpr babylon::ui::Color kFocusBorder  = { 1.00f, 0.88f, 0.60f, 0.95f };
    constexpr babylon::ui::Color kAccent       = { 1.00f, 0.82f, 0.45f, 1.00f };
    constexpr babylon::ui::Color kWarmGrey     = { 0.78f, 0.74f, 0.66f, 0.85f };
    constexpr babylon::ui::Color kRule         = { 0.80f, 0.76f, 0.66f, 0.40f };
    constexpr babylon::ui::Color kGlowOuter    = { 1.00f, 0.72f, 0.32f, 0.08f };
    constexpr babylon::ui::Color kGlowInner    = { 1.00f, 0.80f, 0.45f, 0.16f };
    constexpr babylon::ui::Color kCallout      = { 0.86f, 0.82f, 0.74f, 0.90f };
    constexpr babylon::ui::Color kScrollTrack  = { 1.00f, 1.00f, 1.00f, 0.10f };
    constexpr babylon::ui::Color kScrollThumb  = { 1.00f, 0.88f, 0.60f, 0.65f };

    struct CalloutAnchor
    {
        std::size_t controlId;
        std::vector<std::size_t> controls;
        float x;
        float y;
    };

    const std::vector<CalloutAnchor>& Anchors()
    {
        using babylon::platform::GamepadButton;
        static const std::vector<CalloutAnchor> anchors = {
            { ControllerControlId(GamepadButton::DpadLeft),
                { ControllerControlId(GamepadButton::DpadLeft),
                    ControllerControlId(GamepadButton::DpadRight),
                    ControllerControlId(GamepadButton::DpadUp),
                    ControllerControlId(GamepadButton::DpadDown) },
                0.24f, 0.56f },
            { ControllerControlId(GamepadButton::LeftShoulder),
                { ControllerControlId(GamepadButton::LeftShoulder) }, 0.20f, 0.20f },
            { AxisControlId(babylon::platform::GamepadAxis::LeftTrigger, 1),
                { AxisControlId(babylon::platform::GamepadAxis::LeftTrigger, 1) },
                0.18f, 0.08f },
            { ControllerControlId(GamepadButton::West),
                { ControllerControlId(GamepadButton::West) }, 0.38f, 0.24f },
            { ControllerControlId(GamepadButton::North),
                { ControllerControlId(GamepadButton::North) }, 0.50f, 0.14f },
            { ControllerControlId(GamepadButton::South),
                { ControllerControlId(GamepadButton::South) }, 0.62f, 0.24f },
            { ControllerControlId(GamepadButton::East),
                { ControllerControlId(GamepadButton::East) }, 0.74f, 0.24f },
            { ControllerControlId(GamepadButton::RightShoulder),
                { ControllerControlId(GamepadButton::RightShoulder) }, 0.80f, 0.20f },
            { AxisControlId(babylon::platform::GamepadAxis::RightTrigger, 1),
                { AxisControlId(babylon::platform::GamepadAxis::RightTrigger, 1) },
                0.82f, 0.08f },
            { ControllerControlId(GamepadButton::Back),
                { ControllerControlId(GamepadButton::Back) }, 0.38f, 0.34f },
            { ControllerControlId(GamepadButton::Start),
                { ControllerControlId(GamepadButton::Start) }, 0.62f, 0.34f },
            { ControllerControlId(GamepadButton::LeftStick),
                { ControllerControlId(GamepadButton::LeftStick) }, 0.38f, 0.64f },
            { ControllerControlId(GamepadButton::RightStick),
                { ControllerControlId(GamepadButton::RightStick) }, 0.62f, 0.64f },
            { JONESCONFIG_JOYAXISKID_UP,
                { JONESCONFIG_JOYAXISKID_UP, JONESCONFIG_JOYAXISKID_DOWN,
                    JONESCONFIG_JOYAXISKID_LEFT, JONESCONFIG_JOYAXISKID_RIGHT },
                0.38f, 0.58f },
            { JONESCONTROLSCHEMES_JOYAXIS_RX_POS,
                { JONESCONTROLSCHEMES_JOYAXIS_RX_NEG, JONESCONTROLSCHEMES_JOYAXIS_RX_POS,
                    JONESCONTROLSCHEMES_JOYAXIS_RY_NEG, JONESCONTROLSCHEMES_JOYAXIS_RY_POS },
                0.62f, 0.58f },
        };
        return anchors;
    }

    void DrawBorder(babylon::ui::Painter& painter, const babylon::ui::Rect& rect,
        const babylon::ui::Color& color, float width)
    {
        painter.fillRect(babylon::ui::Rect{ rect.x, rect.y, rect.width, width }, color);
        painter.fillRect(babylon::ui::Rect{ rect.x, rect.y + rect.height - width,
            rect.width, width }, color);
        painter.fillRect(babylon::ui::Rect{ rect.x, rect.y, width, rect.height }, color);
        painter.fillRect(babylon::ui::Rect{ rect.x + rect.width - width, rect.y,
            width, rect.height }, color);
    }

    void DrawPadlock(babylon::ui::Painter& painter, float x, float y,
        const babylon::ui::Color& color)
    {
        // A 10 x 12 padlock: a filled body with a thin three-piece shackle.
        painter.fillRect(babylon::ui::Rect{ x + 2.0f, y, 1.0f, 5.0f }, color);
        painter.fillRect(babylon::ui::Rect{ x + 8.0f, y, 1.0f, 5.0f }, color);
        painter.fillRect(babylon::ui::Rect{ x + 2.0f, y, 7.0f, 1.0f }, color);
        painter.fillRect(babylon::ui::Rect{ x, y + 5.0f, 10.0f, 7.0f }, color);
    }

    std::string Ellipsize(babylon::ui::Painter& painter, babylon::ui::FontId font,
        const std::string& text, float width)
    {
        if ( painter.measureText(font, text).width <= width )
        {
            return text;
        }

        const std::string ellipsis = "...";
        if ( painter.measureText(font, ellipsis).width > width )
        {
            return std::string();
        }

        std::string cut = text;
        while ( !cut.empty() )
        {
            cut.pop_back();
            if ( painter.measureText(font, cut + ellipsis).width <= width )
            {
                break;
            }
        }
        return cut + ellipsis;
    }

    // Greedily wraps text into at most maxLines lines. When words remain after the last line,
    // they are appended to it and the line is ellipsized, so a label never runs past its column.
    std::vector<std::string> WrapLines(babylon::ui::Painter& painter, babylon::ui::FontId font,
        const std::string& text, float width, std::size_t maxLines)
    {
        std::vector<std::string> lines;
        std::size_t pos = 0;
        while ( pos < text.size() && lines.size() < maxLines )
        {
            while ( pos < text.size() && text[pos] == ' ' )
            {
                ++pos;
            }
            if ( pos >= text.size() )
            {
                break;
            }

            const std::size_t end = text.find(' ', pos);
            const std::string word = text.substr(pos, end == std::string::npos
                ? std::string::npos : end - pos);
            std::string candidate = lines.empty() || !lines.back().empty()
                ? (lines.empty() ? word : lines.back() + " " + word) : word;

            if ( lines.empty() || painter.measureText(font, candidate).width <= width )
            {
                if ( lines.empty() )
                {
                    lines.push_back(word);
                }
                else
                {
                    lines.back() = candidate;
                }
            }
            else
            {
                lines.push_back(word);
            }
            pos = end == std::string::npos ? text.size() : end;
        }

        while ( pos < text.size() )
        {
            while ( pos < text.size() && text[pos] == ' ' )
            {
                ++pos;
            }
            if ( pos >= text.size() )
            {
                break;
            }
            const std::size_t end = text.find(' ', pos);
            const std::string word = text.substr(pos, end == std::string::npos
                ? std::string::npos : end - pos);
            lines.back() += " " + word;
            pos = end == std::string::npos ? text.size() : end;
        }

        for ( std::string& line : lines )
        {
            line = Ellipsize(painter, font, line, width);
        }
        return lines;
    }

    float ListWidth(const babylon::ui::Size& canvas)
    {
        return std::max(220.0f, canvas.width * kListWidthFraction);
    }

    void DrawScrollbar(const JonesUiButtonLayoutModel& model, babylon::ui::Painter& painter,
        const babylon::ui::Size& canvas)
    {
        if ( model.rows.size() <= kJonesUiButtonLayoutVisibleRows )
        {
            return;
        }

        const float listHeight = kJonesUiButtonLayoutVisibleRows * kListRowHeight
            + (kJonesUiButtonLayoutVisibleRows - 1) * kListGap;
        const float x = kMargin + ListWidth(canvas) + 10.0f;
        painter.fillRect(babylon::ui::Rect{ x, kListTop, kScrollWidth, listHeight },
            kScrollTrack);

        const float thumbHeight = std::max(24.0f, listHeight
            * (float)kJonesUiButtonLayoutVisibleRows / (float)model.rows.size());
        const std::size_t maxScroll = model.rows.size() - kJonesUiButtonLayoutVisibleRows;
        const float thumbY = maxScroll == 0 ? kListTop
            : kListTop + (listHeight - thumbHeight) * (float)model.scroll / (float)maxScroll;
        painter.fillRect(babylon::ui::Rect{ x, thumbY, kScrollWidth, thumbHeight },
            kScrollThumb);
    }
}

namespace JonesUiButtonLayoutInternal
{
    JonesUiButtonLayoutAssets LoadAssets(babylon::uirender::RenderPainter& painter,
        babylon::files::FileSystem& files, const JonesControlsScheme& scheme,
        JonesUiButtonLayoutDevice device, const char* pSet)
    {
        JonesUiButtonLayoutAssets assets;
        if ( device == JonesUiButtonLayoutDevice::Controller )
        {
            assets.diagram = JonesUiAssets_LoadPng(painter, files,
                "prompts/xbox/XboxSeriesX_Diagram_Simple.png");
        }
        else
        {
            assets.mouse = JonesUiAssets_LoadPng(painter, files,
                "prompts/keyboard/Mouse_Simple_Key_Dark.png");
        }

        const auto load = [&](std::size_t controlId)
        {
            if ( controlId == kJonesUiButtonLayoutNoControl || assets.icons.count(controlId) )
            {
                return;
            }
            const std::string path = device == JonesUiButtonLayoutDevice::Controller
                ? ControllerIconPath(controlId, pSet) : KeyboardIconPath(controlId);
            if ( !path.empty() )
            {
                assets.icons[controlId] = JonesUiAssets_LoadPng(painter, files, path.c_str());
            }
        };

        for ( std::size_t action = 0; action < JONESCONTROL_ACTION_NUMACTIONS; ++action )
        {
            std::size_t controlId = 0;
            if ( JonesUiButtonLayout_GetDeviceBinding(scheme, (JonesControlActions)action,
                    device, controlId) )
            {
                load(controlId);
            }

            if ( device == JonesUiButtonLayoutDevice::Controller )
            {
                const JonesUiStickOwnership ownership =
                    JonesUiControls_GetModernStickOwnership((JonesControlActions)action);
                if ( ownership == JonesUiStickOwnership::Left )
                {
                    load(JONESCONFIG_JOYAXISKID_UP);
                }
                else if ( ownership == JonesUiStickOwnership::Right )
                {
                    load(JONESCONTROLSCHEMES_JOYAXIS_RX_POS);
                }
            }
        }

        return assets;
    }

    babylon::ui::Rect RowRect(const babylon::ui::Size& canvas, std::size_t visualRow)
    {
        return babylon::ui::Rect{ kMargin,
            kListTop + (float)visualRow * (kListRowHeight + kListGap),
            ListWidth(canvas), kListRowHeight };
    }

    // Added: the visible Reset row under the action list. The prompt bar starts below it.
    babylon::ui::Rect ResetRowRect(const babylon::ui::Size& canvas)
    {
        return babylon::ui::Rect{ kMargin,
            kListTop + (float)kJonesUiButtonLayoutVisibleRows * (kListRowHeight + kListGap),
            ListWidth(canvas), kResetRowHeight };
    }
}

void JonesUiButtonLayout_Draw(const JonesUiButtonLayoutModel& model,
    babylon::ui::Painter& painter, const babylon::ui::Size& canvas,
    const babylon::ui::Skin& skin, const JonesUiButtonLayoutAssets& assets)
{
    using babylon::ui::Rect;
    using babylon::ui::TextAlign;

    const babylon::ui::FontId titleFont = skin.titleFont;
    const babylon::ui::FontId rowFont = skin.textFont;

    const char* pCategory = model.device == JonesUiButtonLayoutDevice::Controller
        ? kCategoryController : kCategoryKeyboard;
    painter.drawText(rowFont, pCategory, Rect{ kMargin, 16.0f, canvas.width - 2.0f * kMargin, 14.0f },
        TextAlign::Left, kWarmGrey);
    painter.drawText(titleFont, kTitleText,
        Rect{ kMargin, 32.0f, canvas.width - 2.0f * kMargin, 34.0f }, TextAlign::Left,
        skin.title);
    painter.fillRect(Rect{ kMargin, kHeaderRuleY, canvas.width - 2.0f * kMargin, 1.0f }, kRule);

    const float listWidth = ListWidth(canvas);
    const float listHeight = kJonesUiButtonLayoutVisibleRows * kListRowHeight
        + (kJonesUiButtonLayoutVisibleRows - 1) * kListGap;

    painter.pushClip(Rect{ kMargin - 16.0f, kListTop - 4.0f, listWidth + 36.0f,
        listHeight + 8.0f });
    for ( std::size_t visual = 0; visual < kJonesUiButtonLayoutVisibleRows; ++visual )
    {
        const std::size_t index = model.scroll + visual;
        if ( index >= model.rows.size() )
        {
            break;
        }

        const JonesUiButtonLayoutRow& row = model.rows[index];
        const Rect rect = RowRect(canvas, visual);
        const bool bFocused = index == model.focus;

        if ( bFocused )
        {
            painter.fillRect(Rect{ rect.x - 8.0f, rect.y - 5.0f, rect.width + 16.0f,
                rect.height + 10.0f }, kGlowOuter);
            painter.fillRect(Rect{ rect.x - 4.0f, rect.y - 2.0f, rect.width + 8.0f,
                rect.height + 4.0f }, kGlowInner);
        }

        painter.fillRect(rect, kRowFill);
        DrawBorder(painter, rect, bFocused ? kFocusBorder : kRowBorder, 1.0f);

        if ( bFocused && !skin.focusMarker.empty() )
        {
            painter.drawText(rowFont, skin.focusMarker,
                Rect{ kMargin - 18.0f, rect.y, 14.0f, rect.height }, TextAlign::Left,
                kAccent);
        }

        std::string label = row.name;
        if ( bFocused && model.capturing )
        {
            label = model.device == JonesUiButtonLayoutDevice::Controller
                ? kPressPadText : kPressKeyText;
        }
        const float iconSpace = kIconSize + 16.0f;
        const float lockSpace = row.stickOwnership != JonesUiStickOwnership::None ? 18.0f : 0.0f;
        babylon::ui::Color labelColor = bFocused ? skin.textFocused : skin.text;
        if ( row.stickOwnership != JonesUiStickOwnership::None )
        {
            labelColor.a *= 0.7f;
        }
        painter.drawText(rowFont, label,
            Rect{ rect.x + 12.0f, rect.y,
                rect.width - 24.0f - iconSpace - lockSpace, rect.height },
            TextAlign::Left, labelColor);

        if ( row.stickOwnership != JonesUiStickOwnership::None )
        {
            const auto icon = assets.icons.find(row.controlId);
            const float iconX = rect.x + rect.width - 12.0f - kIconSize;
            const Rect iconRect{ iconX, rect.y + (rect.height - kIconSize) * 0.5f,
                kIconSize, kIconSize };
            if ( icon != assets.icons.end() && icon->second != 0 )
            {
                painter.drawImage(icon->second, iconRect);
                painter.fillRect(iconRect, babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.30f });
            }

            babylon::ui::Color lockColor = kWarmGrey;
            lockColor.a *= 0.80f;
            DrawPadlock(painter, iconX - 18.0f, rect.y + (rect.height - 12.0f) * 0.5f,
                lockColor);
        }
        else if ( row.controlId != kJonesUiButtonLayoutNoControl )
        {
            const auto icon = assets.icons.find(row.controlId);
            if ( icon != assets.icons.end() && icon->second != 0 )
            {
                painter.drawImage(icon->second,
                    Rect{ rect.x + rect.width - 12.0f - kIconSize,
                        rect.y + (rect.height - kIconSize) * 0.5f, kIconSize, kIconSize });
            }
            else
            {
                const float textWidth = painter.measureText(rowFont, row.binding).width;
                const float capWidth = std::max(30.0f, textWidth + 12.0f);
                const Rect cap{ rect.x + rect.width - 12.0f - capWidth,
                    rect.y + (rect.height - 24.0f) * 0.5f, capWidth, 24.0f };
                if ( model.device == JonesUiButtonLayoutDevice::KeyboardMouse )
                {
                    painter.fillRect(cap, kRowFill);
                    DrawBorder(painter, cap, bFocused ? kFocusBorder : kRowBorder, 1.0f);
                }
                painter.drawText(rowFont, row.binding, cap, TextAlign::Center,
                    bFocused ? skin.textFocused : skin.text);
            }
        }
        else
        {
            painter.drawText(rowFont, kUnboundText,
                Rect{ rect.x + rect.width - 12.0f - 100.0f, rect.y, 100.0f, rect.height },
                TextAlign::Right, bFocused ? skin.textFocused : skin.textDisabled);
        }
    }
    painter.popClip();

    // Added: the visible Reset button under the list, so the action is not only a hidden shortcut.
    const Rect resetRect = ResetRowRect(canvas);
    painter.fillRect(resetRect, kRowFill);
    DrawBorder(painter, resetRect, kRowBorder, 1.0f);
    painter.drawText(rowFont, kResetText,
        Rect{ resetRect.x + 12.0f, resetRect.y, resetRect.width - 24.0f, resetRect.height },
        TextAlign::Left, skin.text);

    DrawScrollbar(model, painter, canvas);

    // The right side: the pad or mouse drawing and one callout per physical input, in columns
    // entirely outside the image. Labels are sorted by their anchor's y and spread evenly, wrapped
    // to two lines and ellipsized so nothing overlaps or runs past the canvas.
    const float rightX = kMargin + listWidth + 34.0f;
    const float rightWidth = std::max(0.0f, canvas.width - rightX - kMargin);
    const float labelWidth = std::min(180.0f, std::max(82.0f, rightWidth * 0.22f));
    const float labelGap = 14.0f;

    std::size_t focusedControl = kJonesUiButtonLayoutNoControl;
    if ( model.focus < model.rows.size() )
    {
        focusedControl = model.rows[model.focus].controlId;
    }

    struct Callout
    {
        std::size_t controlId = 0;
        std::vector<std::size_t> controls;
        std::string text;
        std::vector<std::string> lines;
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        bool bFocused = false;
    };

    std::vector<Callout> leftCallouts;
    std::vector<Callout> rightCallouts;

    const JonesControlsScheme* pScheme = nullptr;
    if ( model.pConfig && model.schemeIndex < model.pConfig->numSchemes )
    {
        pScheme = &model.pConfig->aSchemes[model.schemeIndex];
    }

    // The actions with a binding of this device in the group, in action order. Looking at every
    // binding (not only the row's first one) keeps an action visible on each physical input it
    // uses, such as a key and a mouse button.
    const auto groupActions = [&](const std::vector<std::size_t>& controls,
        std::vector<JonesControlActions>& actions, std::vector<std::string>& names)
    {
        if ( !pScheme )
        {
            return;
        }

        for ( std::size_t action = 0; action < JONESCONTROL_ACTION_NUMACTIONS; ++action )
        {
            const std::size_t count = JonesControlSchemes_GetNumBindings(pScheme,
                (JonesControlActions)action);
            for ( std::size_t index = 0; index < count; ++index )
            {
                std::size_t bound = 0;
                if ( JonesControlSchemes_GetBinding(pScheme, (JonesControlActions)action, index,
                        &bound)
                    && JonesUiButtonLayout_IsDeviceControl(bound, model.device)
                    && std::find(controls.begin(), controls.end(), bound) != controls.end() )
                {
                    actions.push_back((JonesControlActions)action);
                    names.push_back(action < model.rows.size()
                        ? model.rows[action].name : CapActionName((JonesControlActions)action));
                    break;
                }
            }
        }
    };

    const auto collect = [&](std::size_t controlId, const std::vector<std::size_t>& controls,
        float normalizedX, float normalizedY, const babylon::ui::Rect& image,
        float textWidth, const char* pOverrideText = nullptr)
    {
        std::vector<JonesControlActions> actions;
        std::vector<std::string> names;
        groupActions(controls, actions, names);
        if ( names.empty() && !pOverrideText )
        {
            return;
        }

        std::string text;
        const bool bStickMove = controls.size() == 4
            && std::find(controls.begin(), controls.end(), JONESCONFIG_JOYAXISKID_UP)
                != controls.end()
            && actions.size() == 4
            && std::find(actions.begin(), actions.end(), JONESCONTROL_ACTION_WALKFORWARD)
                != actions.end()
            && std::find(actions.begin(), actions.end(), JONESCONTROL_ACTION_WALKBACK)
                != actions.end()
            && std::find(actions.begin(), actions.end(), JONESCONTROL_ACTION_TURNLEFT)
                != actions.end()
            && std::find(actions.begin(), actions.end(), JONESCONTROL_ACTION_TURNRIGHT)
                != actions.end();
        if ( pOverrideText )
        {
            text = pOverrideText;
        }
        else if ( bStickMove )
        {
            text = "Move";
        }
        else
        {
            for ( std::size_t cur = 0; cur < names.size(); ++cur )
            {
                if ( cur > 0 )
                {
                    text += " | ";
                }
                text += names[cur];
            }
        }

        Callout callout;
        callout.controlId = controlId;
        callout.controls = controls;
        callout.text = text;
        callout.lines = WrapLines(painter, rowFont, text, textWidth, 2);
        callout.anchorX = image.x + image.width * normalizedX;
        callout.anchorY = image.y + image.height * normalizedY;
        callout.bFocused = focusedControl != kJonesUiButtonLayoutNoControl
            && std::find(controls.begin(), controls.end(), focusedControl) != controls.end();
        (normalizedX < 0.5f ? leftCallouts : rightCallouts).push_back(std::move(callout));
    };

    babylon::ui::Rect image;
    if ( model.device == JonesUiButtonLayoutDevice::Controller )
    {
        const float size = std::min(330.0f, std::max(80.0f, rightWidth
            - 2.0f * (labelWidth + labelGap) - 8.0f));
        image = babylon::ui::Rect{ rightX + (rightWidth - size) * 0.5f, 108.0f, size, size };
        if ( assets.diagram != 0 )
        {
            painter.drawImage(assets.diagram, image);
        }

        for ( const CalloutAnchor& anchor : Anchors() )
        {
            const float columnWidth = anchor.x < 0.5f
                ? std::max(1.0f, image.x - labelGap - rightX)
                : std::max(1.0f, rightX + rightWidth - (image.x + image.width + labelGap));
            const bool bHasIcon = assets.icons.find(anchor.controlId) != assets.icons.end()
                && assets.icons.at(anchor.controlId) != 0;
            const char* pOverrideText = nullptr;
            if ( sithControl_IsModernGamepadStyle() )
            {
                if ( std::find(anchor.controls.begin(), anchor.controls.end(),
                        JONESCONFIG_JOYAXISKID_UP) != anchor.controls.end() )
                {
                    pOverrideText = "Move";
                }
                else if ( std::find(anchor.controls.begin(), anchor.controls.end(),
                        JONESCONTROLSCHEMES_JOYAXIS_RX_POS) != anchor.controls.end() )
                {
                    pOverrideText = "Camera";
                }
            }
            collect(anchor.controlId, anchor.controls, anchor.x, anchor.y, image,
                columnWidth - (bHasIcon ? 20.0f : 0.0f), pOverrideText);
        }
    }
    else
    {
        const float mouseSize = std::min(260.0f, std::max(80.0f, rightWidth
            - 2.0f * (labelWidth + labelGap) - 8.0f));
        image = babylon::ui::Rect{ rightX + (rightWidth - mouseSize) * 0.5f, 108.0f,
            mouseSize, mouseSize };
        if ( assets.mouse != 0 )
        {
            painter.drawImage(assets.mouse, image);
        }

        const std::size_t aMouse[] = {
            STDCONTROL_KID_MOUSE_LBUTTON, STDCONTROL_KID_MOUSE_RBUTTON,
            STDCONTROL_KID_MOUSE_MBUTTON,
        };
        const float aX[] = { 0.34f, 0.66f, 0.50f };
        const float aY[] = { 0.32f, 0.32f, 0.22f };
        for ( std::size_t cur = 0; cur < std::size(aMouse); ++cur )
        {
            const float columnWidth = aX[cur] < 0.5f
                ? std::max(1.0f, image.x - labelGap - rightX)
                : std::max(1.0f, rightX + rightWidth - (image.x + image.width + labelGap));
            const bool bHasIcon = assets.icons.find(aMouse[cur]) != assets.icons.end()
                && assets.icons.at(aMouse[cur]) != 0;
            collect(aMouse[cur], { aMouse[cur] }, aX[cur], aY[cur], image,
                columnWidth - (bHasIcon ? 20.0f : 0.0f));
        }
    }

    const float lineHeight = 18.0f;
    const auto drawCallouts = [&](std::vector<Callout>& callouts, bool bLeft)
    {
        std::sort(callouts.begin(), callouts.end(),
            [](const Callout& a, const Callout& b) { return a.anchorY < b.anchorY; });

        const float columnX = bLeft ? rightX : image.x + image.width + labelGap;
        const float columnWidth = bLeft
            ? std::max(1.0f, image.x - labelGap - rightX)
            : std::max(1.0f, rightX + rightWidth - (image.x + image.width + labelGap));
        const float columnTop = image.y;
        const float columnHeight = model.device == JonesUiButtonLayoutDevice::Controller
            ? std::max(image.height, 420.0f - columnTop) : image.height;

        for ( std::size_t index = 0; index < callouts.size(); ++index )
        {
            Callout& callout = callouts[index];
            const float slot = columnHeight / (float)callouts.size();
            const float totalHeight = lineHeight * (float)callout.lines.size();
            const float y = columnTop + slot * ((float)index + 0.5f) - totalHeight * 0.5f;
            const babylon::ui::Color color = callout.bFocused ? kAccent : kCallout;

            const auto icon = assets.icons.find(callout.controlId);
            const bool bHasIcon = icon != assets.icons.end() && icon->second != 0;
            if ( bHasIcon )
            {
                painter.drawImage(icon->second,
                    babylon::ui::Rect{ columnX, y + (totalHeight - 16.0f) * 0.5f, 16.0f, 16.0f });
            }

            const float textX = columnX + (bHasIcon ? 20.0f : 0.0f);
            const float textWidth = std::max(1.0f, columnWidth - (bHasIcon ? 20.0f : 0.0f));
            for ( std::size_t line = 0; line < callout.lines.size(); ++line )
            {
                painter.drawText(rowFont, callout.lines[line],
                    babylon::ui::Rect{ textX, y + lineHeight * (float)line, textWidth,
                        lineHeight },
                    bLeft ? babylon::ui::TextAlign::Right : babylon::ui::TextAlign::Left,
                    color);
            }

            const float lineY = y + totalHeight * 0.5f;
            const float innerX = bLeft ? image.x - labelGap : image.x + image.width + labelGap;
            painter.fillRect(babylon::ui::Rect{ std::min(innerX, callout.anchorX), lineY,
                std::fabs(callout.anchorX - innerX), 1.0f }, color);
            if ( std::fabs(callout.anchorY - lineY) > 0.5f )
            {
                painter.fillRect(babylon::ui::Rect{ callout.anchorX,
                    std::min(lineY, callout.anchorY), 1.0f,
                    std::fabs(callout.anchorY - lineY) }, color);
            }
        }
    };

    drawCallouts(leftCallouts, true);
    drawCallouts(rightCallouts, false);

    if ( model.device == JonesUiButtonLayoutDevice::KeyboardMouse )
    {
        // The focused key in a clear band below the mouse and its callouts.
        const float largeIcon = 56.0f;
        const float textWidth = std::max(1.0f, rightWidth - largeIcon - 80.0f);
        std::string text = kUnboundText;
        std::size_t controlId = kJonesUiButtonLayoutNoControl;
        if ( model.focus < model.rows.size() )
        {
            const JonesUiButtonLayoutRow& row = model.rows[model.focus];
            controlId = row.controlId;
            if ( controlId != kJonesUiButtonLayoutNoControl )
            {
                text = row.binding + " - " + row.name;
            }
        }

        const float measured = painter.measureText(rowFont, text).width;
        const float panelWidth = largeIcon + 16.0f + std::min(measured, textWidth);
        const float x = rightX + (rightWidth - panelWidth) * 0.5f;
        const float y = image.y + image.height + 40.0f;

        const auto icon = assets.icons.find(controlId);
        if ( icon != assets.icons.end() && icon->second != 0 )
        {
            painter.drawImage(icon->second,
                babylon::ui::Rect{ x, y, largeIcon, largeIcon });
        }
        else if ( controlId != kJonesUiButtonLayoutNoControl )
        {
            const Rect cap{ x, y, largeIcon, largeIcon };
            painter.fillRect(cap, kRowFill);
            DrawBorder(painter, cap, kRowBorder, 1.0f);
            painter.drawText(rowFont, model.rows[model.focus].binding, cap,
                babylon::ui::TextAlign::Center, skin.textFocused);
        }
        painter.drawText(rowFont, Ellipsize(painter, rowFont, text, textWidth),
            babylon::ui::Rect{ x + largeIcon + 16.0f, y, textWidth, largeIcon },
            babylon::ui::TextAlign::Left, skin.textFocused);
    }

}

#ifdef J3D_TEST
babylon::ui::Rect JonesUiButtonLayout_ResetRowRectForTest(const babylon::ui::Size& canvas)
{
    return ResetRowRect(canvas);
}
#endif
