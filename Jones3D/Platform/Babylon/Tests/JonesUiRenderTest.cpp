// Added: the GPU drawing and asset coverage of the Babylon column's menus (ADR-0035). The logic of
// each screen lives in the other suites; these cases prove that the shared RenderPainter draws the
// packaged fonts, PNGs, thumbnails and clipped UI, and that the present overlay layers the menu
// background, dim and screen at the window's pixel size.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <utility>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Jones3D/Platform/Babylon/JonesUiAssets.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiButtonLayout.h>

#include <std/General/stdControl.h>

#include <sith/Devices/sithControl.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/image/Image.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/Widget.h>
#include <babylon/uirender/RenderPainter.h>

namespace
{
    using namespace babylon;

    constexpr std::uint32_t kTargetSize = 480u;
    const babylon::ui::Color kBlack = { 0.0f, 0.0f, 0.0f, 1.0f };
    const babylon::ui::Color kWhite = { 1.0f, 1.0f, 1.0f, 1.0f };

    std::array<int, 4> PixelAt(const std::vector<std::byte>& pixels, std::uint32_t width,
        std::uint32_t x, std::uint32_t y)
    {
        const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
        return { static_cast<int>(pixels[index + 0u]), static_cast<int>(pixels[index + 1u]),
            static_cast<int>(pixels[index + 2u]), static_cast<int>(pixels[index + 3u]) };
    }

    bool AnyBrightPixel(const std::vector<std::byte>& pixels, std::uint32_t width,
        std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1)
    {
        for ( std::uint32_t y = y0; y < y1; ++y )
        {
            for ( std::uint32_t x = x0; x < x1; ++x )
            {
                const std::array<int, 4> pixel = PixelAt(pixels, width, x, y);
                if ( pixel[0] > 32 || pixel[1] > 32 || pixel[2] > 32 )
                {
                    return true;
                }
            }
        }
        return false;
    }

    render::TextureHandle CreateTarget(render::Device& device, render::TextureFormat format)
    {
        return device.createTexture(render::TextureDesc{ kTargetSize, kTargetSize, 1,
            format, render::TextureUsage::ColorTarget });
    }

    // A GPU device and the data file system, or nullopt when the environment has no GPU.
    struct GpuCase
    {
        std::unique_ptr<render::Device> device;
        std::unique_ptr<files::FileSystem> files;
    };

    std::optional<GpuCase> CreateGpuCase()
    {
        GpuCase gpu;
        gpu.device = render::createSdlGpuDevice(nullptr);
        if ( !gpu.device )
        {
            return std::nullopt;
        }

        gpu.files = files::createSdlFileSystem();
        if ( !gpu.files )
        {
            return std::nullopt;
        }

        return gpu;
    }

    void SkipWithoutGpu()
    {
        MESSAGE("skipped, no SDL GPU device");
    }
}

TEST_CASE("JonesUiAssets loads the packaged font and PNG assets")
{
    render::NullDevice device;
    uirender::RenderPainter painter(device);

    const std::unique_ptr<files::FileSystem> files = files::createSdlFileSystem();
    REQUIRE(files);

    const ui::FontId font = JonesUiAssets_LoadFont(painter, *files, "fonts/Jost[wght].ttf",
        16.0f);
    REQUIRE(font != 0);
    const ui::Size measured = painter.measureText(font, "Menu");
    CHECK(measured.width > 0.0f);
    CHECK(measured.height > 0.0f);

    const ui::ImageId png = JonesUiAssets_LoadPng(painter, *files,
        "prompts/keyboard/Enter_Key_Dark.png");
    CHECK(png != 0);

    CHECK(JonesUiAssets_LoadPng(painter, *files, "../escape.png") == 0);
    CHECK(JonesUiAssets_LoadFont(painter, *files, "fonts/missing.ttf", 16.0f) == 0);
}

TEST_CASE("RenderPainter draws the packaged font and PNG")
{
    std::optional<GpuCase> gpu = CreateGpuCase();
    if ( !gpu )
    {
        SkipWithoutGpu();
        return;
    }

    uirender::RenderPainter painter(*gpu->device);
    const ui::FontId font = JonesUiAssets_LoadFont(painter, *gpu->files,
        "fonts/Jost[wght].ttf", 32.0f);
    REQUIRE(font != 0);
    const ui::ImageId icon = JonesUiAssets_LoadPng(painter, *gpu->files,
        "prompts/xbox/XboxSeriesX_A.png");
    REQUIRE(icon != 0);

    const render::TextureHandle target = CreateTarget(*gpu->device, render::TextureFormat::Rgba8);
    REQUIRE_FALSE(target.isNull());

    uirender::FrameTarget frameTarget;
    frameTarget.texture = target;
    frameTarget.format  = render::TextureFormat::Rgba8;
    frameTarget.load    = render::LoadOp::Clear;
    frameTarget.clear   = render::ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

    REQUIRE(painter.begin(kTargetSize, kTargetSize, frameTarget));
    painter.drawText(font, "Indiana Jones", ui::Rect{ 40.0f, 40.0f, 400.0f, 60.0f },
        ui::TextAlign::Left, kWhite);
    painter.drawImage(icon, ui::Rect{ 380.0f, 10.0f, 80.0f, 80.0f });
    REQUIRE(painter.end());

    const std::vector<std::byte> pixels = gpu->device->readTexture(target, 0);
    REQUIRE(pixels.size() == static_cast<std::size_t>(kTargetSize) * kTargetSize * 4u);
    CHECK(AnyBrightPixel(pixels, kTargetSize, 40u, 40u, 440u, 100u));
    CHECK(AnyBrightPixel(pixels, kTargetSize, 380u, 10u, 460u, 90u));

    gpu->device->destroyTexture(target);
}

TEST_CASE("RGB save thumbnails become RGBA images with the right colours")
{
    std::optional<GpuCase> gpu = CreateGpuCase();
    if ( !gpu )
    {
        SkipWithoutGpu();
        return;
    }

    uirender::RenderPainter painter(*gpu->device);

    // Four identical BGR pixels: B = 10, G = 20, R = 30.
    const std::uint8_t aBgr[2u * 2u * 3u] = {
        10u, 20u, 30u, 10u, 20u, 30u,
        10u, 20u, 30u, 10u, 20u, 30u,
    };
    const ui::ImageId thumbnail = JonesUiAssets_AddRgbImage(painter, aBgr, 2u, 2u);
    REQUIRE(thumbnail != 0);

    const render::TextureHandle target = CreateTarget(*gpu->device, render::TextureFormat::Rgba8);
    REQUIRE_FALSE(target.isNull());

    uirender::FrameTarget frameTarget;
    frameTarget.texture = target;
    frameTarget.format  = render::TextureFormat::Rgba8;
    frameTarget.load    = render::LoadOp::Clear;
    frameTarget.clear   = render::ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

    REQUIRE(painter.begin(kTargetSize, kTargetSize, frameTarget));
    painter.drawImage(thumbnail, ui::Rect{ 10.0f, 10.0f, 20.0f, 20.0f });
    REQUIRE(painter.end());

    const std::vector<std::byte> pixels = gpu->device->readTexture(target, 0);
    REQUIRE(pixels.size() == static_cast<std::size_t>(kTargetSize) * kTargetSize * 4u);
    CHECK(PixelAt(pixels, kTargetSize, 20u, 20u) == std::array<int, 4>{ 30, 20, 10, 255 });

    gpu->device->destroyTexture(target);
}

TEST_CASE("The clip stack bounds drawn UI pixels")
{
    std::optional<GpuCase> gpu = CreateGpuCase();
    if ( !gpu )
    {
        SkipWithoutGpu();
        return;
    }

    uirender::RenderPainter painter(*gpu->device);
    const render::TextureHandle target = CreateTarget(*gpu->device, render::TextureFormat::Rgba8);
    REQUIRE_FALSE(target.isNull());

    uirender::FrameTarget frameTarget;
    frameTarget.texture = target;
    frameTarget.format  = render::TextureFormat::Rgba8;
    frameTarget.load    = render::LoadOp::Clear;
    frameTarget.clear   = render::ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f };

    const ui::Color red = { 1.0f, 0.0f, 0.0f, 1.0f };
    REQUIRE(painter.begin(kTargetSize, kTargetSize, frameTarget));
    painter.pushClip(ui::Rect{ 10.0f, 10.0f, 20.0f, 20.0f });
    painter.fillRect(ui::Rect{ 0.0f, 0.0f, 480.0f, 480.0f }, red);
    painter.popClip();
    REQUIRE(painter.end());

    const std::vector<std::byte> pixels = gpu->device->readTexture(target, 0);
    REQUIRE(pixels.size() == static_cast<std::size_t>(kTargetSize) * kTargetSize * 4u);
    CHECK(PixelAt(pixels, kTargetSize, 5u, 5u) == std::array<int, 4>{ 0, 0, 0, 255 });
    CHECK(PixelAt(pixels, kTargetSize, 20u, 20u) == std::array<int, 4>{ 255, 0, 0, 255 });
    CHECK(PixelAt(pixels, kTargetSize, 35u, 35u) == std::array<int, 4>{ 0, 0, 0, 255 });

    gpu->device->destroyTexture(target);
}

TEST_CASE("The present overlay layers background, overlay and screen")
{
    std::optional<GpuCase> gpu = CreateGpuCase();
    if ( !gpu )
    {
        SkipWithoutGpu();
        return;
    }

    JonesUiContext context(*gpu->device);
    static_cast<void>(context.painter.setPixelScale(1.0f));

    const ui::FontId font = JonesUiAssets_LoadFont(context.painter, *gpu->files,
        "fonts/Jost[wght].ttf", 16.0f);
    REQUIRE(font != 0);
    context.titleFont  = font;
    context.rowFont    = font;
    context.promptFont = font;

    cvars::Registry cvars;
    context.pUi = std::make_unique<ui::Ui>(context.painter, cvars);

    ui::Skin skin = JonesUiSkin_Create(font, font, &context.painter, font);
    skin.panel            = ui::Color{ 0.0f, 0.0f, 1.0f, 1.0f };
    skin.panelImage       = 0;
    skin.panelBorder      = ui::Color{ 0.0f, 0.0f, 0.0f, 0.0f };
    skin.borderWidth      = 0.0f;
    skin.placement        = ui::PanelPlacement::Top;
    skin.fixedPanelWidth  = 0.5f;
    skin.topMargin        = 0.0f;
    skin.titleHeight      = 0.0f;
    skin.rowHeight        = 80.0f;
    skin.rowSpacing       = 0.0f;
    skin.panelPadding     = 0.0f;
    skin.minPanelWidth    = 0.0f;
    skin.canvasMargin     = 0.0f;
    REQUIRE(context.pUi->setSkin(skin));

    ui::Screen screen;
    screen.widgets.emplace_back(ui::Label{ "Row", std::string() });
    REQUIRE(context.pUi->push(screen) != ui::kInvalidScreen);
    REQUIRE(context.pUi->setViewport(kTargetSize, kTargetSize));

    image::Image green;
    green.width  = 1;
    green.height = 1;
    green.pixels = { 0u, 255u, 0u, 255u };
    const ui::ImageId greenImage = context.painter.addImage(green);
    REQUIRE(greenImage != 0);

    context.modalBackground.image  = greenImage;
    context.modalBackground.width  = 1.0f;
    context.modalBackground.height = 1.0f;
    context.modalBackground.overlay = [&context]()
    {
        context.painter.fillRect(ui::Rect{ 0.0f, 0.0f, 20.0f, 20.0f },
            ui::Color{ 1.0f, 0.0f, 0.0f, 1.0f });
    };

    const render::TextureHandle target = CreateTarget(*gpu->device, render::TextureFormat::Bgra8);
    REQUIRE_FALSE(target.isNull());

    const auto clearTarget = [&gpu, &target](render::CommandList& list)
    {
        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ target, 0, render::LoadOp::Clear,
            render::StoreOp::Store, render::ClearColor{ 1.0f, 1.0f, 1.0f, 1.0f } });
        REQUIRE(list.beginPass(pass));
        REQUIRE(list.endPass());
    };

    {
        render::CommandList list = gpu->device->beginFrame();
        clearTarget(list);
        REQUIRE(JonesUiContext_DrawOverlayForTest(context, list, kTargetSize, kTargetSize,
            target));
        REQUIRE(gpu->device->submit(list));

        const std::vector<std::byte> pixels = gpu->device->readTexture(target, 0);
        REQUIRE(pixels.size() == static_cast<std::size_t>(kTargetSize) * kTargetSize * 4u);
        // Red overlay in the top-left corner, green background at the top-right, blue panel in
        // the middle.
        CHECK(PixelAt(pixels, kTargetSize, 5u, 5u) == std::array<int, 4>{ 0, 0, 255, 255 });
        CHECK(PixelAt(pixels, kTargetSize, 475u, 5u) == std::array<int, 4>{ 0, 255, 0, 255 });
        CHECK(PixelAt(pixels, kTargetSize, 350u, 20u) == std::array<int, 4>{ 255, 0, 0, 255 });
    }

    {
        context.modalBackground = JonesUiModalBackground{};
        render::CommandList list = gpu->device->beginFrame();
        clearTarget(list);
        REQUIRE(JonesUiContext_DrawOverlayForTest(context, list, kTargetSize, kTargetSize,
            target));
        REQUIRE(gpu->device->submit(list));

        const std::vector<std::byte> pixels = gpu->device->readTexture(target, 0);
        REQUIRE(pixels.size() == static_cast<std::size_t>(kTargetSize) * kTargetSize * 4u);
        // The skin's black dim at 55% over the white clear leaves a mid grey outside the panel.
        const std::array<int, 4> dimmed = PixelAt(pixels, kTargetSize, 5u, 5u);
        CHECK(std::abs(dimmed[0] - 114) <= 2);
        CHECK(std::abs(dimmed[1] - 114) <= 2);
        CHECK(std::abs(dimmed[2] - 114) <= 2);
    }

    gpu->device->destroyTexture(target);
}

namespace
{
    // Writes a top-first RGBA image as a 24-bit bottom-up BMP, the format the reviewer opens.
    bool WriteBmp(const char* pPath, const std::vector<std::byte>& pixels,
        std::uint32_t width, std::uint32_t height)
    {
        const std::uint32_t rowBytes = width * 3u;
        const std::uint32_t padding = (4u - rowBytes % 4u) % 4u;
        const std::uint32_t imageBytes = (rowBytes + padding) * height;
        const std::uint32_t fileBytes = 14u + 40u + imageBytes;

        std::vector<std::uint8_t> header(fileBytes, 0u);
        header[0] = 'B';
        header[1] = 'M';
        const auto put32 = [&header](std::size_t offset, std::uint32_t value)
        {
            header[offset + 0u] = (std::uint8_t)(value & 0xFFu);
            header[offset + 1u] = (std::uint8_t)((value >> 8u) & 0xFFu);
            header[offset + 2u] = (std::uint8_t)((value >> 16u) & 0xFFu);
            header[offset + 3u] = (std::uint8_t)((value >> 24u) & 0xFFu);
        };
        put32(2u, fileBytes);
        put32(10u, 54u);
        put32(14u, 40u);
        put32(18u, width);
        put32(22u, height);
        header[26u] = 1u;
        header[28u] = 24u;
        put32(34u, imageBytes);

        for ( std::uint32_t y = 0; y < height; ++y )
        {
            const std::uint32_t sourceY = height - 1u - y;
            std::size_t row = 54u + (std::size_t)y * (rowBytes + padding);
            for ( std::uint32_t x = 0; x < width; ++x )
            {
                const std::size_t source = ((std::size_t)sourceY * width + x) * 4u;
                header[row + 0u] = (std::uint8_t)pixels[source + 2u];
                header[row + 1u] = (std::uint8_t)pixels[source + 1u];
                header[row + 2u] = (std::uint8_t)pixels[source + 0u];
                row += 3u;
            }
        }

        std::ofstream file(pPath, std::ios::binary | std::ios::trunc);
        if ( !file )
        {
            return false;
        }
        file.write(reinterpret_cast<const char*>(header.data()),
            (std::streamsize)header.size());
        return file.good();
    }

    void AddControllerSampleScheme(JonesControlsConfig& config, JonesControlsScheme& scheme)
    {
        std::memset(&scheme, 0, sizeof(scheme));
        std::snprintf(scheme.aName, sizeof(scheme.aName), "%s", "Sample");
        config.numSchemes = 1;
        config.aSchemes = &scheme;
        config.selectedShemeIdx = 0;

        using babylon::platform::GamepadButton;
        using babylon::platform::GamepadAxis;
        const auto button = [](GamepadButton value)
        {
            std::size_t id = 0;
            switch ( value )
            {
                case GamepadButton::South: id = STDCONTROL_JOYSTICK_GETBUTTON(0, 0); break;
                case GamepadButton::East: id = STDCONTROL_JOYSTICK_GETBUTTON(0, 1); break;
                case GamepadButton::West: id = STDCONTROL_JOYSTICK_GETBUTTON(0, 2); break;
                case GamepadButton::North: id = STDCONTROL_JOYSTICK_GETBUTTON(0, 3); break;
                case GamepadButton::LeftShoulder: id = STDCONTROL_JOYSTICK_GETBUTTON(0, 4); break;
                case GamepadButton::RightShoulder: id = STDCONTROL_JOYSTICK_GETBUTTON(0, 5); break;
                case GamepadButton::DpadUp: id = STDCONTROL_JOYSTICK_GETPOVUP(0, 0); break;
                case GamepadButton::DpadDown: id = STDCONTROL_JOYSTICK_GETPOVDOWN(0, 0); break;
                case GamepadButton::DpadLeft: id = STDCONTROL_JOYSTICK_GETPOVLEFT(0, 0); break;
                case GamepadButton::DpadRight: id = STDCONTROL_JOYSTICK_GETPOVRIGHT(0, 0); break;
                default: id = STDCONTROL_JOYSTICK_GETBUTTON(0, 0); break;
            }
            return id;
        };

        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, button(GamepadButton::South));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_ROLL, button(GamepadButton::East));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_ACTIVATE, button(GamepadButton::West));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_TOGGLEWEAPON, button(GamepadButton::North));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_STEPLEFT, button(GamepadButton::LeftShoulder));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_STEPRIGHT, button(GamepadButton::RightShoulder));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_MAP, button(GamepadButton::DpadUp));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_HEALTH, button(GamepadButton::DpadDown));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_PREVWEAPON, button(GamepadButton::DpadLeft));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_NEXTWEAPON, button(GamepadButton::DpadRight));
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_WALKFORWARD, JONESCONFIG_JOYAXISKID_UP);
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_WALKBACK, JONESCONFIG_JOYAXISKID_DOWN);
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_TURNLEFT, JONESCONFIG_JOYAXISKID_LEFT);
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_TURNRIGHT, JONESCONFIG_JOYAXISKID_RIGHT);
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_LOOK, JONESCONTROLSCHEMES_JOYAXIS_RX_POS);
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_RUN, JONESCONFIG_JOYAXISKID_AIR);
        JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_CRAWL, JONESCONFIG_JOYAXISKID_GROUND);

        // Keyboard and mouse bindings so the Keyboard & Mouse sample has rows with text.
        const std::pair<JonesControlActions, const char*> aKeys[] = {
            { JONESCONTROL_ACTION_WALKFORWARD, "W" },
            { JONESCONTROL_ACTION_WALKBACK, "S" },
            { JONESCONTROL_ACTION_TURNLEFT, "A" },
            { JONESCONTROL_ACTION_TURNRIGHT, "D" },
            { JONESCONTROL_ACTION_JUMP, "SPACE" },
            { JONESCONTROL_ACTION_ROLL, "C" },
            { JONESCONTROL_ACTION_ACTIVATE, "E" },
            { JONESCONTROL_ACTION_TOGGLEWEAPON, "Q" },
            { JONESCONTROL_ACTION_STEPLEFT, "Z" },
            { JONESCONTROL_ACTION_STEPRIGHT, "X" },
            { JONESCONTROL_ACTION_MAP, "TAB" },
            { JONESCONTROL_ACTION_HEALTH, "H" },
            { JONESCONTROL_ACTION_ACTIVATE, "MOUSE1" },
            { JONESCONTROL_ACTION_LOOK, "MOUSE2" },
        };
        for ( const auto& [action, pKey] : aKeys )
        {
            std::size_t id = 0;
            if ( JonesControlSchemes_ParseControlName(pKey, &id) )
            {
                JonesControlSchemes_AddBinding(&scheme, action, id);
            }
        }
        // Toggle light, interface, health and the other actions stay unbound for the sample.
        (void)GamepadAxis::LeftX;
    }

    std::optional<JonesUiButtonLayoutAssets> LoadButtonLayoutAssets(
        uirender::RenderPainter& painter, files::FileSystem& files,
        JonesUiButtonLayoutDevice device)
    {
        JonesUiButtonLayoutAssets assets;
        if ( device == JonesUiButtonLayoutDevice::KeyboardMouse )
        {
            assets.mouse = JonesUiAssets_LoadPng(painter, files,
                "prompts/keyboard/Mouse_Simple_Key_Dark.png");
            const std::pair<const char*, const char*> aKeys[] = {
                { "W", "W_Key_Dark.png" }, { "S", "S_Key_Dark.png" },
                { "A", "A_Key_Dark.png" }, { "D", "D_Key_Dark.png" },
                { "SPACE", "Space_Key_Dark.png" }, { "C", "C_Key_Dark.png" },
                { "E", "E_Key_Dark.png" }, { "Q", "Q_Key_Dark.png" },
                { "Z", "Z_Key_Dark.png" }, { "X", "X_Key_Dark.png" },
                { "TAB", "Tab_Key_Dark.png" }, { "H", "H_Key_Dark.png" },
                { "MOUSE1", "Mouse_Left_Key_Dark.png" },
                { "MOUSE2", "Mouse_Right_Key_Dark.png" },
            };
            for ( const auto& [pName, pFile] : aKeys )
            {
                std::size_t id = 0;
                if ( JonesControlSchemes_ParseControlName(pName, &id) )
                {
                    assets.icons[id] = JonesUiAssets_LoadPng(painter, files,
                        (std::string("prompts/keyboard/") + pFile).c_str());
                }
            }
            return assets;
        }

        assets.diagram = JonesUiAssets_LoadPng(painter, files,
            "prompts/xbox/XboxSeriesX_Diagram_Simple.png");
        const std::size_t controls[] = {
            STDCONTROL_JOYSTICK_GETBUTTON(0, 0), STDCONTROL_JOYSTICK_GETBUTTON(0, 1),
            STDCONTROL_JOYSTICK_GETBUTTON(0, 2), STDCONTROL_JOYSTICK_GETBUTTON(0, 3),
            STDCONTROL_JOYSTICK_GETBUTTON(0, 4), STDCONTROL_JOYSTICK_GETBUTTON(0, 5),
            STDCONTROL_JOYSTICK_GETPOVUP(0, 0), STDCONTROL_JOYSTICK_GETPOVDOWN(0, 0),
            STDCONTROL_JOYSTICK_GETPOVLEFT(0, 0), STDCONTROL_JOYSTICK_GETPOVRIGHT(0, 0),
            JONESCONFIG_JOYAXISKID_UP, JONESCONFIG_JOYAXISKID_DOWN,
            JONESCONFIG_JOYAXISKID_LEFT, JONESCONFIG_JOYAXISKID_RIGHT,
            JONESCONFIG_JOYAXISKID_AIR, JONESCONFIG_JOYAXISKID_GROUND,
            JONESCONTROLSCHEMES_JOYAXIS_RX_POS,
        };
        const char* const aFiles[] = {
            "prompts/xbox/XboxSeriesX_A.png", "prompts/xbox/XboxSeriesX_B.png",
            "prompts/xbox/XboxSeriesX_X.png", "prompts/xbox/XboxSeriesX_Y.png",
            "prompts/xbox/XboxSeriesX_LB.png", "prompts/xbox/XboxSeriesX_RB.png",
            "prompts/xbox/XboxSeriesX_Dpad_Up.png", "prompts/xbox/XboxSeriesX_Dpad_Down.png",
            "prompts/xbox/XboxSeriesX_Dpad_Left.png", "prompts/xbox/XboxSeriesX_Dpad_Right.png",
            "prompts/xbox/XboxSeriesX_Left_Stick.png", "prompts/xbox/XboxSeriesX_Left_Stick.png",
            "prompts/xbox/XboxSeriesX_Left_Stick.png", "prompts/xbox/XboxSeriesX_Left_Stick.png",
            "prompts/xbox/XboxSeriesX_LT.png", "prompts/xbox/XboxSeriesX_RT.png",
            "prompts/xbox/XboxSeriesX_Right_Stick.png",
        };
        for ( std::size_t cur = 0; cur < std::size(controls); ++cur )
        {
            assets.icons[controls[cur]] = JonesUiAssets_LoadPng(painter, files, aFiles[cur]);
        }
        return assets;
    }

    bool RenderButtonLayoutSample(JonesUiButtonLayoutDevice device, const char* pPath)
    {
        std::optional<GpuCase> gpu = CreateGpuCase();
        if ( !gpu )
        {
            SkipWithoutGpu();
            return false;
        }

        uirender::RenderPainter painter(*gpu->device);
        static_cast<void>(painter.setPixelScale(800.0f / 480.0f));

        const ui::FontId titleFont = JonesUiAssets_LoadFont(painter, *gpu->files,
            "fonts/Cinzel[wght].ttf", 22.0f);
        const ui::FontId rowFont = JonesUiAssets_LoadFont(painter, *gpu->files,
            "fonts/Jost[wght].ttf", 16.0f);
        REQUIRE(titleFont != 0);
        REQUIRE(rowFont != 0);

        JonesControlsScheme scheme;
        JonesControlsConfig config;
        AddControllerSampleScheme(config, scheme);

        JonesUiButtonLayoutModel model;
        model.pConfig = &config;
        model.schemeIndex = 0;
        model.device = device;
        model.focus = 2;
        JonesUiButtonLayout_RefreshRows(model);

        // English display names for the sample, since the test has no game string table.
        const char* const aNames[] = {
            "Walk Forward", "Walk Back", "Turn Left", "Turn Right", "Step Left",
            "Step Right", "Crawl", "Run", "Roll", "Jump", "Look", "Activate",
            "Toggle Weapon", "Previous Weapon", "Next Weapon", "Toggle Light", "Fists",
            "Whip", "Mauser", "PPSh-41", "Pistol", "Simonov", "Tokarev", "Toz-34",
            "Bazooka", "Machete", "Satchel", "Grenade", "Map", "Interface", "Health",
            "Imp 1", "Imp 2", "Imp 3", "Imp 4", "Imp 5", "Chalk",
        };
        for ( std::size_t cur = 0; cur < model.rows.size() && cur < std::size(aNames); ++cur )
        {
            model.rows[cur].name = aNames[cur];
        }

        const std::optional<JonesUiButtonLayoutAssets> assets =
            LoadButtonLayoutAssets(painter, *gpu->files, device);
        REQUIRE(assets.has_value());

        const ui::Skin skin = JonesUiSkin_Create(titleFont, rowFont, &painter, rowFont);

        constexpr std::uint32_t kWidth = 1280u;
        constexpr std::uint32_t kHeight = 800u;
        const render::TextureHandle target = gpu->device->createTexture(render::TextureDesc{
            kWidth, kHeight, 1, render::TextureFormat::Rgba8,
            render::TextureUsage::ColorTarget });
        REQUIRE_FALSE(target.isNull());

        uirender::FrameTarget frameTarget;
        frameTarget.texture = target;
        frameTarget.format  = render::TextureFormat::Rgba8;
        frameTarget.load    = render::LoadOp::Clear;
        frameTarget.clear   = render::ClearColor{ 0.02f, 0.02f, 0.03f, 1.0f };

        REQUIRE(painter.begin(kWidth, kHeight, frameTarget));
        const ui::Size canvas{ 480.0f * (float)kWidth / (float)kHeight, 480.0f };
        JonesUiButtonLayout_Draw(model, painter, canvas, skin, *assets);
        REQUIRE(painter.end());

        const std::vector<std::byte> pixels = gpu->device->readTexture(target, 0);
        REQUIRE(pixels.size() == (std::size_t)kWidth * kHeight * 4u);
        const bool bWritten = WriteBmp(pPath, pixels, kWidth, kHeight);
        gpu->device->destroyTexture(target);
        REQUIRE(bWritten);
        MESSAGE("wrote ", pPath);
        return true;
    }
}

TEST_CASE("the Button Layout screens render offscreen samples")
{
    sithControl_SetModernControlStyle(true);
    static_cast<void>(RenderButtonLayoutSample(JonesUiButtonLayoutDevice::Controller,
        "../../../../ui-button-layout-controller.bmp"));
    static_cast<void>(RenderButtonLayoutSample(JonesUiButtonLayoutDevice::KeyboardMouse,
        "../../../../ui-button-layout-keyboard.bmp"));
}
