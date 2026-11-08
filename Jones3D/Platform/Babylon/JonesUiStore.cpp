// Added: the Babylon column's Store screen; see JonesUiStore.h.
//
// Original behavior, from (git show 82b8f607^:Jones3D/Platform/Win32/jonesConfig.c):
// - jonesConfig_ShowStoreDialog (line 8998) copies *pBalance into the cart, runs dialog 190 and
//   copies the cart's balance back. It never writes pItemsState itself: the add/remove/clear
//   handlers mutate that array in place.
// - jonesConfig_InitStoreDialog (line 9165) builds the current-inventory and available lists from
//   the item state, clears each availability nibble, then clears total and updates the two
//   balances (lines 9185-9204). jonesConfig_StoreInitItemList (line 9264) shows the held count
//   from bits 4..15 and the price from the item table.
// - jonesConfig_AddStoreCartItem (line 9326) adds only when balance >= the selected items' cost;
//   a bonus map shows JONES_STR_PERU when accepted, else JONES_STR_NOPERU when refused, and a
//   single refused item shows JONES_STR_CANTBUY1. It increments the low 16 bits of the item state
//   and subtracts the cost (lines 9343-9412).
// - jonesConfig_RemoveStoreCartItem (line 9439) refunds each removed row's price and decrements
//   the low 16 bits.
// - jonesConfig_ClearStoreCart (line 9507) returns the total and zeroes the low 16 bits.
// - jonesConfig_UpdateBalances (line 9311) writes total and balance with a trailing space.
// - jonesConfig_StoreDialog_HandleWM_COMMAND (line 9477) runs the same confirmation for Done (1)
//   and Cancel (2) when balance >= 0; only an answer of 1 ends the dialog, with that command id.
//   The proc does not handle WM_CLOSE, so the dialog manager posts it as WM_COMMAND IDCANCEL (2):
//   the window close takes the Cancel path and its prompt too.
// - jonesConfig_ShowPurchaseMessageBox (line 9529) returns 1 (Yes) or 2 (No); its init (line 9557)
//   builds "\n" + ADDINV + " " + SUBCHEST(total) + "  " + YESNOBUY when total != 0, otherwise
//   NOCHANGE + "  " + ABANDON.
//
// The drag-and-drop and mouse handlers are dropped: ADR-0026 makes the store a list with a
// quantity per item. The Win32 item icons are resources, not HUD materials reachable from the
// Babylon painter, so the rows are text only.
#include <Jones3D/Platform/Babylon/JonesUiStore.h>

#include <Jones3D/Display/JonesHudConstants.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <babylon/ui/Screen.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <std/Babylon/stdBabylonEvents.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr int kJonesDialogDone   = 1;
    constexpr int kJonesDialogCancel = 2;

    constexpr babylon::ui::ActionId kJonesStoreActionDone   = kJonesDialogDone;
    constexpr babylon::ui::ActionId kJonesStoreActionCancel = kJonesDialogCancel;

    constexpr int kJonesStoreMessageIcon = 191;

    static_assert(STD_ARRAYLEN(JonesHud_aStoreItems) == kJonesStoreItemCount,
        "one cart entry for every store item");

    struct JonesUiStoreLayout
    {
        std::size_t items[kJonesStoreItemCount] = {};
        std::size_t total = 0;
        std::size_t balance = 0;
        std::size_t done = 0;
        std::size_t cancel = 0;
    };

    std::string JonesUiStore_Number(int value)
    {
        return std::to_string(value) + " ";
    }

    std::string JonesUiStore_ItemText(std::size_t item, const JonesUiStoreCart& cart)
    {
        char aText[64] = { 0 };
        std::snprintf(aText, sizeof(aText), "  %d  (%d held)",
            JonesUiStore_GetPrice(item), cart.held[item]);
        return JonesUiContext_ResolveString(JonesUiStore_GetNameKey(item)) + aText;
    }

    void JonesUiStore_SetBalanceWidgets(babylon::ui::Ui& ui, babylon::ui::ScreenId screenId,
        const JonesUiStoreLayout& layout, const JonesUiStoreCart& cart)
    {
        static_cast<void>(ui.setWidget(screenId, layout.total,
            babylon::ui::Label{ JonesUiContext_ResolveString("JONES_STR_BALANCE"),
                JonesUiStore_Number(cart.total) }));
        static_cast<void>(ui.setWidget(screenId, layout.balance,
            babylon::ui::Label{ JonesUiContext_ResolveString("JONES_STR_CREDIT"),
                JonesUiStore_Number(cart.balance) }));
    }

    void JonesUiStore_SetItemWidgets(babylon::ui::Ui& ui, babylon::ui::ScreenId screenId,
        const JonesUiStoreLayout& layout, const JonesUiStoreCart& cart)
    {
        for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
        {
            babylon::ui::Slider slider;
            slider.text    = JonesUiStore_ItemText(item, cart);
            slider.min     = 0.0;
            slider.max     = (double)JonesUiStoreCart_GetMaxQuantity(cart, item);
            slider.step    = 1.0;
            slider.enabled = cart.available[item];
            slider.value   = (double)cart.quantities[item];
            static_cast<void>(ui.setWidget(screenId, layout.items[item], std::move(slider)));
        }

        JonesUiStore_SetBalanceWidgets(ui, screenId, layout, cart);
    }

    babylon::ui::Screen JonesUiStore_BuildScreen(const JonesUiStoreCart& cart,
        JonesUiStoreLayout& layout)
    {
        babylon::ui::Screen screen;
        screen.title = JonesUiContext_ResolveString("JONES_STR_STORE");

        // The balances stay above the long item list so the live update is visible.
        layout.total = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString("JONES_STR_BALANCE"),
            JonesUiStore_Number(cart.total) });

        layout.balance = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Label{
            JonesUiContext_ResolveString("JONES_STR_CREDIT"),
            JonesUiStore_Number(cart.balance) });

        for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
        {
            layout.items[item] = screen.widgets.size();

            babylon::ui::Slider slider;
            slider.text    = JonesUiStore_ItemText(item, cart);
            slider.min     = 0.0;
            slider.max     = (double)JonesUiStoreCart_GetMaxQuantity(cart, item);
            slider.step    = 1.0;
            slider.enabled = cart.available[item];
            slider.value   = (double)cart.quantities[item];
            screen.widgets.emplace_back(std::move(slider));
        }

        layout.done = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_DONE"), kJonesStoreActionDone, true });

        layout.cancel = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_CANCEL"), kJonesStoreActionCancel, true });

        screen.backResult = kJonesDialogCancel;

        // The first available item, like the original's available list; with none, Done.
        screen.initialFocus = layout.done;
        for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
        {
            if ( cart.available[item] )
            {
                screen.initialFocus = layout.items[item];
                break;
            }
        }

        return screen;
    }

    void JonesUiStore_ShowAddMessage(JonesUiStoreAddResult result)
    {
        const char* pTextKey = JonesUiStore_AddResultTextKey(result);
        if ( pTextKey )
        {
            static_cast<void>(jonesConfig_ShowMessageDialog(NULL, "JONES_STR_STORE", pTextKey,
                kJonesStoreMessageIcon));
        }
    }

    // Splits the original text on its explicit newlines so each paragraph is one label row. The
    // text itself is unchanged; the leading newline only made the Win32 control put it below its
    // icon.
    void JonesUiStore_AppendTextRows(babylon::ui::Screen& screen, const std::string& text)
    {
        std::size_t start = 0;
        for ( ;; )
        {
            const std::size_t end = text.find('\n', start);
            screen.widgets.emplace_back(babylon::ui::Label{
                end == std::string::npos ? text.substr(start) : text.substr(start, end - start),
                std::string() });

            if ( end == std::string::npos )
            {
                break;
            }
            start = end + 1;
        }
    }

    babylon::ui::Screen JonesUiStore_BuildPurchaseScreen(const JonesUiStoreCart& cart)
    {
        babylon::ui::Screen screen;
        screen.title = JonesUiContext_ResolveString("JONES_STR_STORE");
        JonesUiStore_AppendTextRows(screen, JonesUiStore_BuildPurchaseText(cart));

        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_YES"), kJonesDialogDone, true });
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_NO"), kJonesDialogCancel, true });

        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = screen.widgets.size() - 2;
        return screen;
    }

    int JonesUiStore_AskPurchase(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
        const JonesUiStoreCart& cart)
    {
        return JonesUiModal_Run(ui, pPainter, JonesUiStore_BuildPurchaseScreen(cart),
            [](babylon::ui::ResultCode code) { return (int)code; });
    }

    bool JonesUiStore_HandleValueChanged(babylon::ui::Ui& ui,
        babylon::ui::ScreenId screenId, const JonesUiStoreLayout& layout,
        JonesUiStoreCart& cart, const babylon::ui::UiEvent& event)
    {
        const babylon::ui::ValueChanged* pChanged =
            std::get_if<babylon::ui::ValueChanged>(&event);
        if ( !pChanged || pChanged->screen != screenId )
        {
            return false;
        }

        const std::size_t firstItem = layout.items[0];
        if ( pChanged->widget < firstItem
            || pChanged->widget >= firstItem + kJonesStoreItemCount )
        {
            return false;
        }

        const double* pValue = std::get_if<double>(&pChanged->value);
        if ( !pValue )
        {
            return true;
        }

        const std::size_t item = pChanged->widget - firstItem;
        int newQuantity = (int)std::llround(*pValue);
        if ( newQuantity < 0 )
        {
            newQuantity = 0;
        }

        const int delta = newQuantity - cart.quantities[item];
        if ( delta > 0 )
        {
            const JonesUiStoreAddResult result = JonesUiStoreCart_AddItem(cart, item, delta);
            if ( result == JonesUiStoreAddResult::AddedBonusMap
                || result == JonesUiStoreAddResult::InsufficientBonusMap
                || result == JonesUiStoreAddResult::InsufficientBalance )
            {
                JonesUiStore_ShowAddMessage(result);
            }
        }
        else if ( delta < 0 )
        {
            static_cast<void>(JonesUiStoreCart_RemoveItem(cart, item, -delta));
        }

        JonesUiStore_SetItemWidgets(ui, screenId, layout, cart);
        return true;
    }

    bool JonesUiStore_HandleAction(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
        babylon::ui::ScreenId screenId, const JonesUiStoreCart& cart, bool& bActionClose,
        const babylon::ui::UiEvent& event)
    {
        const babylon::ui::ActionPressed* pAction =
            std::get_if<babylon::ui::ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId
            || (pAction->action != kJonesStoreActionDone
                && pAction->action != kJonesStoreActionCancel) )
        {
            return false;
        }

        // The original ends without the prompt only when the balance is already negative; both
        // Done and Cancel otherwise go through the same confirmation.
        if ( cart.balance < 0
            || JonesUiStore_AskPurchase(ui, pPainter, cart) == kJonesDialogDone )
        {
            bActionClose = true;
            static_cast<void>(ui.pop((babylon::ui::ResultCode)pAction->action));
        }

        return true;
    }
}

int JonesUiStore_GetPrice(std::size_t item)
{
    return item < kJonesStoreItemCount ? JonesHud_aStoreItems[item].price : 0;
}

const char* JonesUiStore_GetNameKey(std::size_t item)
{
    return item < kJonesStoreItemCount ? JonesHud_aStoreItems[item].aName : "";
}

const char* JonesUiStore_GetClipNameKey(std::size_t item)
{
    return item < kJonesStoreItemCount ? JonesHud_aStoreItems[item].aClipName : "";
}

bool JonesUiStore_IsBonusMap(std::size_t item)
{
    return item < kJonesStoreItemCount
        && JonesHud_aStoreItems[item].menuID == JONESHUD_MENU_INVITEM_BONUSMAP;
}

void JonesUiStoreCart_Init(JonesUiStoreCart& cart, int balance, const int* apItemsState)
{
    cart = JonesUiStoreCart{};
    cart.startBalance = balance;
    cart.balance      = balance;

    if ( !apItemsState )
    {
        return;
    }

    for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
    {
        const std::uint32_t state = (std::uint32_t)apItemsState[item];
        cart.available[item] = (state & 0xFu) != 0;
        cart.held[item]      = (int)((state & 0xFFF0u) >> 4);
    }
}

int JonesUiStoreCart_GetMaxQuantity(const JonesUiStoreCart& cart, std::size_t item)
{
    if ( item >= kJonesStoreItemCount || !cart.available[item] )
    {
        return 0;
    }

    const int price = JonesUiStore_GetPrice(item);
    if ( price <= 0 || cart.balance <= 0 )
    {
        return cart.quantities[item];
    }

    return cart.quantities[item] + cart.balance / price;
}

JonesUiStoreAddResult JonesUiStoreCart_AddItem(JonesUiStoreCart& cart, std::size_t item,
    int count)
{
    if ( item >= kJonesStoreItemCount || count <= 0 || !cart.available[item] )
    {
        return JonesUiStoreAddResult::NotAvailable;
    }

    const long long cost = (long long)JonesUiStore_GetPrice(item) * count;
    if ( cost > cart.balance )
    {
        return JonesUiStore_IsBonusMap(item)
            ? JonesUiStoreAddResult::InsufficientBonusMap
            : JonesUiStoreAddResult::InsufficientBalance;
    }

    cart.quantities[item] += count;
    cart.total   += (int)cost;
    cart.balance -= (int)cost;

    return JonesUiStore_IsBonusMap(item)
        ? JonesUiStoreAddResult::AddedBonusMap
        : JonesUiStoreAddResult::Added;
}

int JonesUiStoreCart_RemoveItem(JonesUiStoreCart& cart, std::size_t item, int count)
{
    if ( item >= kJonesStoreItemCount || count <= 0 )
    {
        return 0;
    }

    const int remove = std::min(count, cart.quantities[item]);
    if ( remove <= 0 )
    {
        return 0;
    }

    const int refund = JonesUiStore_GetPrice(item) * remove;
    cart.quantities[item] -= remove;
    cart.total   -= refund;
    cart.balance += refund;
    return remove;
}

void JonesUiStoreCart_Clear(JonesUiStoreCart& cart)
{
    cart.balance += cart.total;
    cart.total = 0;
    for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
    {
        cart.quantities[item] = 0;
    }
}

void JonesUiStoreCart_UpdateBalances(JonesUiStoreCart& cart)
{
    int total = 0;
    for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
    {
        total += JonesUiStore_GetPrice(item) * cart.quantities[item];
    }

    cart.total   = total;
    cart.balance = cart.startBalance - total;
}

void JonesUiStore_ApplyPurchase(const JonesUiStoreCart& cart, int* pBalance,
    int* apItemsState)
{
    if ( pBalance )
    {
        *pBalance = cart.balance;
    }

    if ( !apItemsState )
    {
        return;
    }

    for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
    {
        const std::uint32_t saved = (std::uint32_t)apItemsState[item];
        const std::uint32_t quantity = (std::uint32_t)cart.quantities[item] & 0xFFFFu;
        apItemsState[item] = (int)(quantity | (saved & 0xFFFF0000u));
    }
}

const char* JonesUiStore_AddResultTextKey(JonesUiStoreAddResult result)
{
    switch ( result )
    {
        case JonesUiStoreAddResult::AddedBonusMap:
            return "JONES_STR_PERU";
        case JonesUiStoreAddResult::InsufficientBonusMap:
            return "JONES_STR_NOPERU";
        case JonesUiStoreAddResult::InsufficientBalance:
            return "JONES_STR_CANTBUY1";
        case JonesUiStoreAddResult::Added:
        case JonesUiStoreAddResult::NotAvailable:
            break;
    }

    return NULL;
}

std::string JonesUiStore_BuildPurchaseText(const JonesUiStoreCart& cart)
{
    std::string text;

    if ( cart.total != 0 )
    {
        text += "\n";
        text += JonesUiContext_ResolveString("JONES_STR_ADDINV");
        text += " ";

        const std::string format = JonesUiContext_ResolveString("JONES_STR_SUBCHEST");
        char aTotalText[256] = { 0 };
        std::snprintf(aTotalText, sizeof(aTotalText), format.c_str(), cart.total);
        text += aTotalText;
        text += "  ";
        text += JonesUiContext_ResolveString("JONES_STR_YESNOBUY");
    }
    else
    {
        text += JonesUiContext_ResolveString("JONES_STR_NOCHANGE");
        text += "  ";
        text += JonesUiContext_ResolveString("JONES_STR_ABANDON");
    }

    return text;
}

int JonesUiStore_RunStoreScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    int* pBalance, int* apItemsState, int unknown2)
{
    (void)unknown2;

    if ( !pBalance || !apItemsState )
    {
        return kJonesDialogCancel;
    }

    JonesUiStoreCart cart;
    JonesUiStoreCart_Init(cart, *pBalance, apItemsState);

    for ( ;; )
    {
        JonesUiStoreLayout layout;
        const babylon::ui::Screen screen = JonesUiStore_BuildScreen(cart, layout);
        bool bActionClose = false;

        const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
            babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
        {
            if ( JonesUiStore_HandleValueChanged(uiRef, screenId, layout, cart, event) )
            {
                return true;
            }

            return JonesUiStore_HandleAction(uiRef, pPainter, screenId, cart, bActionClose,
                event);
        };

        const int result = JonesUiModal_RunHandled(ui, pPainter, screen,
            [](babylon::ui::ResultCode code) { return (int)code; }, handler);

        // Done, Cancel (after Yes) and the negative-balance direct close all write the cart to the
        // caller's buffers, as jonesConfig_ShowStoreDialog did after the dialog whatever it
        // returned.
        if ( bActionClose )
        {
            JonesUiStore_ApplyPurchase(cart, pBalance, apItemsState);
            return result;
        }

        // The screen closed with back, the window close or a quit. The original store proc did not
        // handle WM_CLOSE, so the dialog manager posted IDCANCEL (2) to it, which runs the same
        // purchase prompt; a quit has no usable prompt and closes like the direct path.
        if ( stdBabylonEvents_PeekQuitRequest() || cart.balance < 0
            || JonesUiStore_AskPurchase(ui, pPainter, cart) == kJonesDialogDone )
        {
            JonesUiStore_ApplyPurchase(cart, pBalance, apItemsState);
            return result;
        }

        // No: the original stays in the store. Reopen it with the cart unchanged.
    }
}

#ifdef J3D_TEST
void JonesUiStore_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens)
{
    JonesUiStoreCart cart;
    JonesUiStoreCart_Init(cart, 1000, nullptr);

    JonesUiStoreLayout layout;
    screens.push_back(JonesUiStore_BuildScreen(cart, layout));
    screens.push_back(JonesUiStore_BuildPurchaseScreen(cart));
}
#endif
