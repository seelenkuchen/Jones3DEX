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
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Jones3D/Platform/Babylon/JonesUiAssets.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
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
