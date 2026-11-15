// Added: model and layout tests of the "Button Layout" screens (ADR-0036). They need no display:
// the rows, focus, device rules and callout text are plain model functions, and the layout test
// records a NullPainter. The scripted capture and file cases live in JonesUiControlsTest.cpp,
// beside the existing Controls harness.
#include <doctest.h>

#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>

#include <std/General/stdControl.h>

#include <sith/Devices/sithControl.h>
#include <Jones3D/Platform/Babylon/JonesUiButtonLayout.h>

#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Widget.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    std::size_t ControlId(const char* pName)
    {
        std::size_t id = (std::size_t)-1;
        REQUIRE(JonesControlSchemes_ParseControlName(pName, &id) == 1);
        return id;
    }

    const std::size_t kPadSouth = STDCONTROL_JOYSTICK_GETBUTTON(0, 0);
    const std::size_t kPadEast  = STDCONTROL_JOYSTICK_GETBUTTON(0, 1);
    const std::size_t kPadNorth = STDCONTROL_JOYSTICK_GETBUTTON(0, 3);
    const std::size_t kPadLeft  = STDCONTROL_JOYSTICK_GETPOVLEFT(0, 0);

    JonesControlsScheme EmptyScheme()
    {
        JonesControlsScheme scheme{};
        std::snprintf(scheme.aName, sizeof(scheme.aName), "%s", "Test");
        return scheme;
    }

    bool hasText(const babylon::ui::NullPainter& painter, const std::string& text)
    {
        for ( const babylon::ui::RecordedCall& call : painter.recorded() )
        {
            if ( const auto* pText = std::get_if<babylon::ui::RecordedDrawText>(&call) )
            {
                if ( pText->text == text )
                {
                    return true;
                }
            }
        }
        return false;
    }

    const babylon::ui::RecordedDrawText* findText(const babylon::ui::NullPainter& painter,
        const std::string& text)
    {
        for ( const babylon::ui::RecordedCall& call : painter.recorded() )
        {
            if ( const auto* pText = std::get_if<babylon::ui::RecordedDrawText>(&call) )
            {
                if ( pText->text == text )
                {
                    return pText;
                }
            }
        }
        return nullptr;
    }

    bool hasImage(const babylon::ui::NullPainter& painter, babylon::ui::ImageId image)
    {
        for ( const babylon::ui::RecordedCall& call : painter.recorded() )
        {
            if ( const auto* pImage = std::get_if<babylon::ui::RecordedDrawImage>(&call) )
            {
                if ( pImage->image == image )
                {
                    return true;
                }
            }
        }
        return false;
    }
}

TEST_CASE("rows show the first binding of this screen's device")
{
    sithControl_SetModernControlStyle(false);
    JonesControlsScheme scheme = EmptyScheme();
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP,
        ControlId("Z")) == 1);
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, kPadSouth) == 1);

    const std::vector<JonesUiButtonLayoutRow> controller =
        JonesUiButtonLayout_BuildRows(scheme, JonesUiButtonLayoutDevice::Controller);
    const std::vector<JonesUiButtonLayoutRow> keyboard =
        JonesUiButtonLayout_BuildRows(scheme, JonesUiButtonLayoutDevice::KeyboardMouse);
    REQUIRE(controller.size() == JONESCONTROL_ACTION_NUMACTIONS);
    REQUIRE(keyboard.size() == JONESCONTROL_ACTION_NUMACTIONS);

    CHECK(controller[JONESCONTROL_ACTION_JUMP].controlId == kPadSouth);
    CHECK(keyboard[JONESCONTROL_ACTION_JUMP].controlId == ControlId("Z"));
    CHECK(controller[JONESCONTROL_ACTION_WALKFORWARD].controlId
        == kJonesUiButtonLayoutNoControl);
    CHECK(controller[JONESCONTROL_ACTION_JUMP].binding == "BUTTON1");
    CHECK(controller[JONESCONTROL_ACTION_WALKFORWARD].binding.empty());
    sithControl_SetModernControlStyle(true);
}

TEST_CASE("Modern stick-owned actions get a stick row and group callouts")
{
    sithControl_SetModernControlStyle(true);

    JonesControlsScheme scheme = EmptyScheme();
    JonesControlsConfig config{};
    config.numSchemes = 1;
    config.aSchemes = &scheme;

    const std::vector<JonesUiButtonLayoutRow> rows =
        JonesUiButtonLayout_BuildRows(scheme, JonesUiButtonLayoutDevice::Controller);
    CHECK(rows[JONESCONTROL_ACTION_WALKFORWARD].stickOwnership
        == JonesUiStickOwnership::Left);
    CHECK(rows[JONESCONTROL_ACTION_WALKFORWARD].controlId == JONESCONFIG_JOYAXISKID_UP);
    CHECK(rows[JONESCONTROL_ACTION_STEPLEFT].stickOwnership == JonesUiStickOwnership::Left);
    CHECK(rows[JONESCONTROL_ACTION_RUN].stickOwnership == JonesUiStickOwnership::Left);
    CHECK(rows[JONESCONTROL_ACTION_LOOK].stickOwnership == JonesUiStickOwnership::Right);
    CHECK(rows[JONESCONTROL_ACTION_JUMP].stickOwnership == JonesUiStickOwnership::None);

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    JonesUiButtonLayoutAssets assets;
    assets.icons[JONESCONFIG_JOYAXISKID_UP] = 33;
    assets.icons[JONESCONTROLSCHEMES_JOYAXIS_RX_POS] = 34;

    babylon::ui::NullPainter painter;
    babylon::ui::Skin skin;
    skin.titleFont = 1;
    skin.textFont = 2;
    JonesUiButtonLayout_Draw(model, painter, babylon::ui::Size{ 853.0f, 480.0f },
        skin, assets);
    CHECK(hasText(painter, "Move"));
    CHECK(hasText(painter, "Camera"));

    sithControl_SetModernControlStyle(true);
}

TEST_CASE("the required-binding check keeps the original and Modern exemptions")
{
    sithControl_SetModernControlStyle(true);
    JonesControlsScheme scheme = EmptyScheme();
    const std::string modern = JonesUiControls_GetMissingActions(scheme);
    CHECK(modern.find(JonesControlSchemes_GetCapActionName(JONESCONTROL_ACTION_STEPLEFT))
        == std::string::npos);
    CHECK(modern.find(JonesControlSchemes_GetCapActionName(JONESCONTROL_ACTION_IMP1))
        == std::string::npos);
    CHECK(modern.find(JonesControlSchemes_GetCapActionName(JONESCONTROL_ACTION_IMP5))
        == std::string::npos);
    CHECK(modern.find(JonesControlSchemes_GetCapActionName(JONESCONTROL_ACTION_CRAWL))
        != std::string::npos);

    sithControl_SetModernControlStyle(false);
    const std::string classic = JonesUiControls_GetMissingActions(scheme);
    CHECK(classic.find(JonesControlSchemes_GetCapActionName(JONESCONTROL_ACTION_STEPLEFT))
        != std::string::npos);
    CHECK(classic.find(JonesControlSchemes_GetCapActionName(JONESCONTROL_ACTION_IMP1))
        == std::string::npos);
    sithControl_SetModernControlStyle(true);
}

TEST_CASE("the list moves focus and scrolls without dropping it out of view")
{
    JonesControlsScheme scheme = EmptyScheme();
    JonesControlsConfig config{};
    config.numSchemes = 1;
    config.aSchemes = &scheme;

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    JonesUiButtonLayout_MoveFocus(model, (long)kJonesUiButtonLayoutVisibleRows + 2);
    CHECK(model.focus == kJonesUiButtonLayoutVisibleRows + 2);
    CHECK(model.scroll == 3);

    JonesUiButtonLayout_MoveFocus(model, -2);
    CHECK(model.scroll == 3);
    JonesUiButtonLayout_MoveFocus(model, -10);
    CHECK(model.focus == 0);
    CHECK(model.scroll == 0);

    JonesUiButtonLayout_Scroll(model, 100);
    CHECK(model.scroll == model.rows.size() - kJonesUiButtonLayoutVisibleRows);
    JonesUiButtonLayout_Scroll(model, -100);
    CHECK(model.scroll == 0);
}

TEST_CASE("a bind replaces only this device's binding")
{
    JonesControlsScheme scheme = EmptyScheme();
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP,
        ControlId("Z")) == 1);
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, kPadSouth) == 1);

    REQUIRE(JonesUiButtonLayout_SetDeviceBinding(scheme, JONESCONTROL_ACTION_JUMP,
        JonesUiButtonLayoutDevice::Controller, kPadEast));
    CHECK(JonesControlSchemes_GetNumBindings(&scheme, JONESCONTROL_ACTION_JUMP) == 2);
    std::size_t keyboard = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(scheme, JONESCONTROL_ACTION_JUMP,
        JonesUiButtonLayoutDevice::KeyboardMouse, keyboard));
    CHECK(keyboard == ControlId("Z"));

    std::size_t controller = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(scheme, JONESCONTROL_ACTION_JUMP,
        JonesUiButtonLayoutDevice::Controller, controller));
    CHECK(controller == kPadEast);
}

TEST_CASE("reset restores one device and keeps the other")
{
    JonesControlsScheme scheme = EmptyScheme();
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP,
        ControlId("Z")) == 1);
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, kPadSouth) == 1);

    JonesUiButtonLayout_ResetDevice(scheme, JonesUiButtonLayoutDevice::Controller, 0);
    CHECK(scheme.bModified == 1);

    std::size_t keyboard = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(scheme, JONESCONTROL_ACTION_JUMP,
        JonesUiButtonLayoutDevice::KeyboardMouse, keyboard));
    CHECK(keyboard == ControlId("Z"));

    // The default Jump binding is a controller control and is back (possibly the same one).
    std::size_t controller = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(scheme, JONESCONTROL_ACTION_JUMP,
        JonesUiButtonLayoutDevice::Controller, controller));
    CHECK(JonesUiButtonLayout_IsDeviceControl(controller,
        JonesUiButtonLayoutDevice::Controller));
}

TEST_CASE("callouts group every action bound to one pad input")
{
    JonesControlsScheme scheme = EmptyScheme();
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, kPadSouth) == 1);
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_ACTIVATE, kPadNorth) == 1);
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_RUN, kPadNorth) == 1);

    CHECK(JonesUiButtonLayout_CalloutText(scheme, kPadSouth)
        == JonesControlSchemes_GetCapActionName(JONESCONTROL_ACTION_JUMP));
    const std::string north = JonesUiButtonLayout_CalloutText(scheme, kPadNorth);
    CHECK(north.find(JonesControlSchemes_GetCapActionName(JONESCONTROL_ACTION_ACTIVATE))
        != std::string::npos);
    CHECK(north.find(" | ") != std::string::npos);
    CHECK(north.find(JonesControlSchemes_GetCapActionName(JONESCONTROL_ACTION_RUN))
        != std::string::npos);
    CHECK(JonesUiButtonLayout_CalloutText(scheme, kPadLeft).empty());
}

TEST_CASE("the layout draws the header, framed rows, icons, scrollbar and diagram")
{
    JonesControlsScheme scheme = EmptyScheme();
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, kPadSouth) == 1);
    JonesControlsConfig config{};
    config.numSchemes = 1;
    config.aSchemes = &scheme;

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.device = JonesUiButtonLayoutDevice::Controller;
    model.focus = JONESCONTROL_ACTION_JUMP;
    JonesUiButtonLayout_RefreshRows(model);

    JonesUiButtonLayoutAssets assets;
    assets.diagram = 77;
    assets.icons[kPadSouth] = 88;

    babylon::ui::NullPainter painter;
    babylon::ui::Skin skin;
    skin.titleFont = 1;
    skin.textFont = 2;
    skin.focusMarker = "\xE2\x80\xA2";

    const babylon::ui::Size canvas{ 853.0f, 480.0f };
    JonesUiButtonLayout_Draw(model, painter, canvas, skin, assets);

    CHECK(hasText(painter, "CONTROLLER"));
    CHECK(hasText(painter, "BUTTON LAYOUT"));
    CHECK(hasText(painter, JonesControlSchemes_GetActionName(JONESCONTROL_ACTION_JUMP)));
    CHECK(hasImage(painter, 77));
    CHECK(hasImage(painter, 88));

    const babylon::ui::RecordedDrawText* pRow =
        findText(painter, JonesControlSchemes_GetActionName(JONESCONTROL_ACTION_JUMP));
    REQUIRE(pRow != nullptr);
    CHECK(pRow->rect.x < canvas.width * 0.4f);
    CHECK(pRow->rect.y > 80.0f);

    // Rows are framed boxes: each visible row has at least four border fills around its text.
    std::size_t narrowFills = 0;
    for ( const babylon::ui::RecordedCall& call : painter.recorded() )
    {
        if ( const auto* pFill = std::get_if<babylon::ui::RecordedFillRect>(&call) )
        {
            if ( pFill->rect.width < 5.0f || pFill->rect.height < 5.0f )
            {
                ++narrowFills;
            }
        }
    }
    CHECK(narrowFills >= 8u);

    // The scrollbar is the thin track in the gap right of the list.
    bool bScrollbar = false;
    for ( const babylon::ui::RecordedCall& call : painter.recorded() )
    {
        if ( const auto* pFill = std::get_if<babylon::ui::RecordedFillRect>(&call) )
        {
            if ( pFill->rect.width <= 4.0f && pFill->rect.height > 200.0f )
            {
                bScrollbar = true;
            }
        }
    }
    CHECK(bScrollbar);

    for ( const babylon::ui::RecordedCall& call : painter.recorded() )
    {
        if ( const auto* pText = std::get_if<babylon::ui::RecordedDrawText>(&call) )
        {
            CHECK(pText->rect.x >= 0.0f);
            CHECK(pText->rect.x + pText->rect.width <= canvas.width + 0.01f);
        }
    }
}

TEST_CASE("the four left-stick walk and turn actions share one Move callout")
{
    JonesControlsScheme scheme = EmptyScheme();
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_WALKFORWARD,
        JONESCONFIG_JOYAXISKID_UP) == 1);
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_WALKBACK,
        JONESCONFIG_JOYAXISKID_DOWN) == 1);
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_TURNLEFT,
        JONESCONFIG_JOYAXISKID_LEFT) == 1);
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_TURNRIGHT,
        JONESCONFIG_JOYAXISKID_RIGHT) == 1);

    JonesControlsConfig config{};
    config.numSchemes = 1;
    config.aSchemes = &scheme;

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    JonesUiButtonLayoutAssets assets;
    assets.icons[JONESCONFIG_JOYAXISKID_UP] = 44;

    babylon::ui::NullPainter painter;
    babylon::ui::Skin skin;
    skin.titleFont = 1;
    skin.textFont = 2;
    JonesUiButtonLayout_Draw(model, painter, babylon::ui::Size{ 853.0f, 480.0f },
        skin, assets);

    CHECK(hasText(painter, "Move"));
    CHECK(hasImage(painter, 44));
}

TEST_CASE("the Keyboard & Mouse layout draws the mouse and key icons, not the pad")
{
    JonesControlsScheme scheme = EmptyScheme();
    const std::size_t keyW = ControlId("W");
    REQUIRE(JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, keyW) == 1);
    JonesControlsConfig config{};
    config.numSchemes = 1;
    config.aSchemes = &scheme;

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.device = JonesUiButtonLayoutDevice::KeyboardMouse;
    model.focus = JONESCONTROL_ACTION_JUMP;
    JonesUiButtonLayout_RefreshRows(model);

    JonesUiButtonLayoutAssets assets;
    assets.diagram = 77;
    assets.mouse = 55;
    assets.icons[keyW] = 66;

    babylon::ui::NullPainter painter;
    babylon::ui::Skin skin;
    skin.titleFont = 1;
    skin.textFont = 2;
    const babylon::ui::Size canvas{ 853.0f, 480.0f };
    JonesUiButtonLayout_Draw(model, painter, canvas, skin, assets);

    CHECK(hasImage(painter, 55));
    CHECK(hasImage(painter, 66));
    CHECK_FALSE(hasImage(painter, 77));
    CHECK(hasText(painter, "Not bound"));
    CHECK(hasText(painter, "KEYBOARD & MOUSE"));
}
