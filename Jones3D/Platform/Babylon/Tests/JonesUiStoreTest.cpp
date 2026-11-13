// Added: the logic and scripted-screen tests of the Babylon column's Store screen (ADR-0026).
// The cart functions are checked against hand-computed cases from the original
// jonesConfig_AddStoreCartItem/RemoveStoreCartItem/ClearStoreCart and the purchase message of
// jonesConfig_InitPurchaseMessageBox. The screen is driven by one scripted platform event per
// frame for one purchase and one cancel. The drawing sample lives in JonesUiPainterTest.cpp.
#include <doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/time/NullClock.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Ui.h>

#include <Jones3D/Display/JonesHudConstants.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesUiStore.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/General/std.h>

namespace
{
    using namespace babylon;

    int J3DAPI TestPrint(const char* pFormat, ...)
    {
        (void)pFormat;
        return 0;
    }

    tHostServices g_hostServices;

    void SetupHostServices()
    {
        std::memset(&g_hostServices, 0, sizeof(g_hostServices));
        g_hostServices.pStatusPrint  = TestPrint;
        g_hostServices.pMessagePrint = TestPrint;
        g_hostServices.pWarningPrint = TestPrint;
        g_hostServices.pErrorPrint   = TestPrint;
        g_hostServices.pDebugPrint   = TestPrint;
        g_hostServices.pMalloc       = malloc;
        g_hostServices.pFree         = free;
        g_hostServices.pRealloc      = realloc;
        g_hostServices.pFileOpen     = stdFileOpen;
        g_hostServices.pFileClose    = stdFileClose;
        g_hostServices.pFileRead     = stdFileRead;
        g_hostServices.pFileGets     = stdFileGets;
        g_hostServices.pFileWrite    = stdFileWrite;
        g_hostServices.pFileEOF      = stdFileEof;
        g_hostServices.pFileTell     = stdFileTell;
        g_hostServices.pFileSeek     = stdFileSeek;
        g_hostServices.pFileSize     = stdFileSize;
        g_hostServices.pFilePrintf   = stdFilePrintf;
        g_hostServices.pFileGetws    = stdFileGetws;

        std_g_pHS = &g_hostServices;
    }

    // Delivers one event per modal frame. The pump drains a platform until pollEvent returns
    // empty, so each event is followed by one empty answer to end that frame. If a screen never
    // closes, a quit request ends it with its back result instead of hanging the suite.
    class ScriptedPlatform final : public platform::Platform
    {
    public:
        std::unique_ptr<platform::Window> createWindow(const platform::WindowDesc&) override
        {
            return nullptr;
        }

        std::optional<platform::Event> pollEvent() override
        {
            if ( m_delivering )
            {
                m_delivering = false;
                return std::nullopt;
            }

            if ( m_events.empty() )
            {
                if ( ++m_emptyPolls > 200 )
                {
                    m_emptyPolls = 0;
                    return platform::Event{ platform::QuitRequested{} };
                }

                return std::nullopt;
            }

            m_emptyPolls = 0;
            m_delivering = true;
            platform::Event event = m_events.front();
            m_events.pop_front();
            return event;
        }

        bool showMessageBox(std::string_view, std::string_view) override
        {
            return false;
        }

        bool rumbleGamepad(platform::GamepadId, float, float, std::uint32_t) override
        {
            return false;
        }

        void PushKey(platform::Key key)
        {
            m_events.push_back(platform::Event{
                platform::KeyChanged{ 1, key, true, false } });
            m_events.push_back(platform::Event{
                platform::KeyChanged{ 1, key, false, false } });
        }

        void PushQuit()
        {
            m_events.push_back(platform::Event{ platform::QuitRequested{} });
        }

    private:
        std::deque<platform::Event> m_events;
        bool m_delivering = false;
        int m_emptyPolls  = 0;
    };

    struct ScreenTestContext
    {
        ScriptedPlatform platform;
        time::NullClock clock;
        time::NullClock virtualClock;
        std::unique_ptr<files::FileSystem> pFileSystem;
        cvars::Registry cvars;
        StdBabylonContext context{};

        ScreenTestContext()
        {
            pFileSystem = files::createSdlFileSystem();

            context.pClock          = &clock;
            context.pVirtualClock   = &virtualClock;
            context.pFileSystem     = pFileSystem.get();
            context.pPlatform       = &platform;
            context.pScriptPlatform = nullptr;
            context.pWindow         = nullptr;
            context.pCvars          = &cvars;
            context.pRenderDevice   = nullptr;

            stdBabylon_SetContext(&context);
            if ( stdBabylonEvents_PeekQuitRequest() )
            {
                static_cast<void>(stdBabylonEvents_TakeQuitRequest());
            }

            SetupHostServices();
        }

        ~ScreenTestContext()
        {
            stdBabylon_SetContext(nullptr);
        }
    };

    // The item-state encoding the caller (JonesHud_ShowLevelCompleted) builds: bit 0 is
    // availability, bits 4..15 the held count and bits 16.. the menu id.
    int MakeItemState(bool bAvailable, int held, int menuID)
    {
        return (int)(((std::uint32_t)menuID << 16) | ((std::uint32_t)held << 4)
            | (bAvailable ? 1u : 0u));
    }

    int MenuIDOf(std::size_t item)
    {
        return JonesHud_aStoreItems[item].menuID;
    }
}

TEST_CASE("the store cart adds each item up to its balance limit and refuses the next")
{
    for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
    {
        const int price   = JonesUiStore_GetPrice(item);
        const int balance = price * 3 + price / 2;

        int aState[kJonesStoreItemCount] = {};
        aState[item] = MakeItemState(true, 7, MenuIDOf(item));

        JonesUiStoreCart cart;
        JonesUiStoreCart_Init(cart, balance, aState);

        const int limit = balance / price;
        REQUIRE(limit == 3);

        for ( int added = 0; added < limit; ++added )
        {
            const JonesUiStoreAddResult result = JonesUiStoreCart_AddItem(cart, item, 1);
            CHECK((result == JonesUiStoreAddResult::Added
                || result == JonesUiStoreAddResult::AddedBonusMap));
        }

        CHECK(cart.quantities[item] == limit);
        CHECK(cart.total == limit * price);
        CHECK(cart.balance == balance - limit * price);
        CHECK(JonesUiStoreCart_GetMaxQuantity(cart, item) == limit);

        const JonesUiStoreAddResult refused = JonesUiStoreCart_AddItem(cart, item, 1);
        CHECK(refused == (JonesUiStore_IsBonusMap(item)
            ? JonesUiStoreAddResult::InsufficientBonusMap
            : JonesUiStoreAddResult::InsufficientBalance));
        CHECK(cart.quantities[item] == limit);
        CHECK(cart.balance == balance - limit * price);
    }
}

TEST_CASE("the store cart refuses beyond the balance and keeps the bonus map rule")
{
    const std::size_t mauser = 0;
    const std::size_t map    = 11;

    REQUIRE(JonesUiStore_GetPrice(mauser) == 50);
    REQUIRE(JonesUiStore_GetPrice(map) == 2500);
    REQUIRE(JonesUiStore_IsBonusMap(map));
    REQUIRE_FALSE(JonesUiStore_IsBonusMap(mauser));

    int aState[kJonesStoreItemCount] = {};
    aState[mauser] = MakeItemState(true, 0, MenuIDOf(mauser));
    aState[map]    = MakeItemState(true, 0, MenuIDOf(map));

    {
        JonesUiStoreCart cart;
        JonesUiStoreCart_Init(cart, 40, aState);
        CHECK(JonesUiStoreCart_AddItem(cart, mauser, 1)
            == JonesUiStoreAddResult::InsufficientBalance);
        CHECK(cart.balance == 40);
        CHECK(cart.total == 0);
        CHECK(cart.quantities[mauser] == 0);
        CHECK(std::string(JonesUiStore_AddResultTextKey(
            JonesUiStoreAddResult::InsufficientBalance)) == "JONES_STR_CANTBUY1");
    }

    {
        JonesUiStoreCart cart;
        JonesUiStoreCart_Init(cart, 2499, aState);
        CHECK(JonesUiStoreCart_AddItem(cart, map, 1)
            == JonesUiStoreAddResult::InsufficientBonusMap);
        CHECK(cart.balance == 2499);
        CHECK(cart.quantities[map] == 0);
        CHECK(std::string(JonesUiStore_AddResultTextKey(
            JonesUiStoreAddResult::InsufficientBonusMap)) == "JONES_STR_NOPERU");
    }

    {
        JonesUiStoreCart cart;
        JonesUiStoreCart_Init(cart, 2500, aState);
        CHECK(JonesUiStoreCart_AddItem(cart, map, 1)
            == JonesUiStoreAddResult::AddedBonusMap);
        CHECK(cart.balance == 0);
        CHECK(cart.quantities[map] == 1);
        CHECK(std::string(JonesUiStore_AddResultTextKey(
            JonesUiStoreAddResult::AddedBonusMap)) == "JONES_STR_PERU");
    }

    // An item whose availability nibble is zero cannot be added even when the balance allows it.
    int aUnavailable[kJonesStoreItemCount] = {};
    aUnavailable[mauser] = MakeItemState(false, 0, MenuIDOf(mauser));
    JonesUiStoreCart cart;
    JonesUiStoreCart_Init(cart, 1000, aUnavailable);
    CHECK(JonesUiStoreCart_AddItem(cart, mauser, 1)
        == JonesUiStoreAddResult::NotAvailable);
    CHECK(cart.balance == 1000);
    CHECK(JonesUiStoreCart_GetMaxQuantity(cart, mauser) == 0);
}

TEST_CASE("the store cart removes and clears with the original refunds")
{
    const std::size_t mauser = 0;
    const std::size_t herb   = 13;

    int aState[kJonesStoreItemCount] = {};
    aState[mauser] = MakeItemState(true, 0, MenuIDOf(mauser));
    aState[herb]   = MakeItemState(true, 0, MenuIDOf(herb));

    JonesUiStoreCart cart;
    JonesUiStoreCart_Init(cart, 300, aState);

    REQUIRE(JonesUiStoreCart_AddItem(cart, mauser, 2) == JonesUiStoreAddResult::Added);
    REQUIRE(JonesUiStoreCart_AddItem(cart, herb, 1) == JonesUiStoreAddResult::Added);
    CHECK(cart.total == 200);
    CHECK(cart.balance == 100);

    CHECK(JonesUiStoreCart_RemoveItem(cart, mauser, 1) == 1);
    CHECK(cart.quantities[mauser] == 1);
    CHECK(cart.total == 150);
    CHECK(cart.balance == 150);

    // Removing more than is in the cart refunds only what is there.
    CHECK(JonesUiStoreCart_RemoveItem(cart, herb, 5) == 1);
    CHECK(cart.quantities[herb] == 0);
    CHECK(cart.total == 50);
    CHECK(cart.balance == 250);

    CHECK(JonesUiStoreCart_RemoveItem(cart, herb, 1) == 0);

    JonesUiStoreCart_Clear(cart);
    CHECK(cart.total == 0);
    CHECK(cart.balance == 300);
    for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
    {
        CHECK(cart.quantities[item] == 0);
    }

    // Recomputing the balances from the quantities gives the same numbers.
    REQUIRE(JonesUiStoreCart_AddItem(cart, mauser, 3) == JonesUiStoreAddResult::Added);
    CHECK(cart.total == 150);
    CHECK(cart.balance == 150);
    cart.total   = 0;
    cart.balance = 0;
    JonesUiStoreCart_UpdateBalances(cart);
    CHECK(cart.total == 150);
    CHECK(cart.balance == 150);
}

TEST_CASE("a purchase writes the original item-state bytes and balance")
{
    const std::size_t mauser = 0;
    const std::size_t ppsh   = 1;
    const std::size_t simon  = 2;

    int aState[kJonesStoreItemCount] = {};
    aState[mauser] = MakeItemState(true, 3, MenuIDOf(mauser));
    aState[ppsh]   = MakeItemState(true, 2, MenuIDOf(ppsh));
    aState[simon]  = MakeItemState(false, 0, MenuIDOf(simon));

    JonesUiStoreCart cart;
    JonesUiStoreCart_Init(cart, 100, aState);
    CHECK(cart.held[mauser] == 3);
    CHECK(cart.held[ppsh] == 2);

    REQUIRE(JonesUiStoreCart_AddItem(cart, mauser, 2) == JonesUiStoreAddResult::Added);
    CHECK(cart.balance == 0);

    int balance = 100;
    JonesUiStore_ApplyPurchase(cart, &balance, aState);

    CHECK(balance == 0);
    CHECK((std::uint32_t)aState[mauser]
        == (((std::uint32_t)MenuIDOf(mauser) << 16) | 2u));
    // The original cleared the low 16 bits of every entry, bought or not, before it ended.
    CHECK((std::uint32_t)aState[ppsh] == ((std::uint32_t)MenuIDOf(ppsh) << 16));
    CHECK((std::uint32_t)aState[simon] == ((std::uint32_t)MenuIDOf(simon) << 16));
}

TEST_CASE("the purchase message uses the original texts for a cart and an empty cart")
{
    JonesUiStoreCart cart;
    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 0, MenuIDOf(0));
    JonesUiStoreCart_Init(cart, 100, aState);

    CHECK(JonesUiStore_BuildPurchaseText(cart)
        == "JONES_STR_NOCHANGE  JONES_STR_ABANDON");

    REQUIRE(JonesUiStoreCart_AddItem(cart, 0, 1) == JonesUiStoreAddResult::Added);
    CHECK(JonesUiStore_BuildPurchaseText(cart)
        == "\nJONES_STR_ADDINV JONES_STR_SUBCHEST  JONES_STR_YESNOBUY");
}

TEST_CASE("the store screen buys the adjusted quantities and returns Done")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 3, MenuIDOf(0));
    int balance = 100;

    // The first available item is focused: increase it twice (2 x 50), then Done and Yes.
    testContext.platform.PushKey(platform::Key::Right);
    testContext.platform.PushKey(platform::Key::Right);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiStore_RunStoreScreen(ui, &painter, &balance, aState, 11) == 1);
    CHECK(balance == 0);
    CHECK((std::uint32_t)aState[0]
        == (((std::uint32_t)MenuIDOf(0) << 16) | 2u));
    for ( std::size_t item = 1; item < kJonesStoreItemCount; ++item )
    {
        CHECK((std::uint32_t)aState[item] == 0);
    }
}

TEST_CASE("the store screen keeps the cart when Cancel is confirmed")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 3, MenuIDOf(0));
    int balance = 100;

    // Increase one item, move to Cancel, confirm Yes: the original answers 2 but has already
    // written the cart into the caller's buffers, so the purchase stays.
    testContext.platform.PushKey(platform::Key::Right);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiStore_RunStoreScreen(ui, &painter, &balance, aState, 11) == 2);
    CHECK(balance == 50);
    CHECK((std::uint32_t)aState[0]
        == (((std::uint32_t)MenuIDOf(0) << 16) | 1u));
}

TEST_CASE("Done on an empty cart still returns 1 and clears the availability nibbles")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 3, MenuIDOf(0));
    int balance = 100;

    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiStore_RunStoreScreen(ui, &painter, &balance, aState, 11) == 1);
    CHECK(balance == 100);
    CHECK((std::uint32_t)aState[0] == ((std::uint32_t)MenuIDOf(0) << 16));
}

TEST_CASE("answering No to the purchase prompt keeps the store screen open")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 0, MenuIDOf(0));
    int balance = 100;

    // Cancel, No in the prompt; the store is still there, so back opens the same prompt, and Yes
    // then closes with 2. The close writes the cart, which clears the availability nibble.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiStore_RunStoreScreen(ui, &painter, &balance, aState, 11) == 2);
    CHECK(balance == 100);
    CHECK((std::uint32_t)aState[0] == ((std::uint32_t)MenuIDOf(0) << 16));
}

TEST_CASE("back runs the purchase prompt and keeps the cart when Yes")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 0, MenuIDOf(0));
    int balance = 100;

    // One item, then back: the original's WM_CLOSE becomes IDCANCEL, which prompts, and Yes
    // closes with 2 after writing the cart.
    testContext.platform.PushKey(platform::Key::Right);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiStore_RunStoreScreen(ui, &painter, &balance, aState, 11) == 2);
    CHECK(balance == 50);
    CHECK((std::uint32_t)aState[0]
        == (((std::uint32_t)MenuIDOf(0) << 16) | 1u));
}

TEST_CASE("answering No to the back prompt reopens the store with its cart")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 0, MenuIDOf(0));
    int balance = 100;

    // One item, back, No: the store reopens with the quantity still in the cart. Done then Yes
    // closes with 1 and writes it.
    testContext.platform.PushKey(platform::Key::Right);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiStore_RunStoreScreen(ui, &painter, &balance, aState, 11) == 1);
    CHECK(balance == 50);
    CHECK((std::uint32_t)aState[0]
        == (((std::uint32_t)MenuIDOf(0) << 16) | 1u));
}

TEST_CASE("a negative balance closes without the prompt and still writes the outputs")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 3, MenuIDOf(0));
    int balance = -1;

    // Done at once: the original's balance < 0 path ends without ShowPurchaseMessageBox.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);

    CHECK(JonesUiStore_RunStoreScreen(ui, &painter, &balance, aState, 11) == 1);
    CHECK(balance == -1);
    CHECK((std::uint32_t)aState[0] == ((std::uint32_t)MenuIDOf(0) << 16));
}

TEST_CASE("a quit closes the store without the prompt and still writes the outputs")
{
    ScreenTestContext testContext;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 3, MenuIDOf(0));
    int balance = 100;

    // A game quit is one of the close paths without the prompt; it still writes the cart.
    testContext.platform.PushQuit();

    CHECK(JonesUiStore_RunStoreScreen(ui, &painter, &balance, aState, 11) == 2);
    CHECK(balance == 100);
    CHECK((std::uint32_t)aState[0] == ((std::uint32_t)MenuIDOf(0) << 16));

    if ( stdBabylonEvents_PeekQuitRequest() )
    {
        static_cast<void>(stdBabylonEvents_TakeQuitRequest());
    }
}

TEST_CASE("the store wrapper is a no-op headless")
{
    ScreenTestContext testContext;

    int aState[kJonesStoreItemCount] = {};
    aState[0] = MakeItemState(true, 3, MenuIDOf(0));
    const int originalState = aState[0];
    int balance = 100;

    CHECK(jonesConfig_ShowStoreDialog(NULL, &balance, aState, 11) == 2);
    CHECK(balance == 100);
    CHECK(aState[0] == originalState);
}
