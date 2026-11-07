// Added: the Babylon column's Store screen (ADR-0026), the original store dialog (dialog 190)
// changed from drag and drop to a list with a quantity per item. The wrapper keeps the original
// return values: 1 when Done is confirmed, 2 when Cancel is confirmed or the screen is backed out.
//
// The cart and balance logic is plain C++ so a test can check it without a screen or a window.
// Each function names the original it ports:
// - JonesUiStoreCart_Init        <- jonesConfig_InitStoreDialog / jonesConfig_StoreInitItemList
// - JonesUiStoreCart_GetMaxQuantity <- the balance limit of jonesConfig_AddStoreCartItem
// - JonesUiStoreCart_AddItem     <- jonesConfig_AddStoreCartItem
// - JonesUiStoreCart_RemoveItem  <- jonesConfig_RemoveStoreCartItem
// - JonesUiStoreCart_Clear       <- jonesConfig_ClearStoreCart
// - JonesUiStoreCart_UpdateBalances <- jonesConfig_UpdateBalances
// - JonesUiStore_ApplyPurchase   <- the apItemsState writes of AddStoreCartItem and *pBalance of
//                                   jonesConfig_ShowStoreDialog
// - JonesUiStore_BuildPurchaseText <- jonesConfig_InitPurchaseMessageBox
// - JonesUiStore_AskPurchase     <- jonesConfig_ShowPurchaseMessageBox
// - JonesUiStore_RunStoreScreen  <- jonesConfig_ShowStoreDialog / jonesConfig_StoreDialogProc
#ifndef JONES3D_PLATFORM_BABYLON_JONESUISTORE_H
#define JONES3D_PLATFORM_BABYLON_JONESUISTORE_H
#ifndef __cplusplus
#error "JonesUiStore.h is C++ only: it names babylon::ui and std::string."
#endif

#include <babylon/ui/Painter.h>
#include <babylon/ui/Ui.h>

#include <cstddef>
#include <cstdint>
#include <string>

// JonesHud_aStoreItems has 14 entries; the array the caller passes has one int per entry.
inline constexpr std::size_t kJonesStoreItemCount = 14;

// The reason an add was accepted or refused. The original showed a message on the two refusal
// paths and the bonus-map warning on the accepted bonus-map path; see AddResultTextKey.
enum class JonesUiStoreAddResult
{
    Added,              // accepted
    AddedBonusMap,      // accepted, and the original first showed JONES_STR_PERU
    NotAvailable,       // the item's availability nibble was zero
    InsufficientBalance,// refused: single non-bonus item, JONES_STR_CANTBUY1
    InsufficientBonusMap,// refused: JONES_STR_NOPERU
};

// The live cart. quantities and held are indexed like JonesHud_aStoreItems. held comes from the
// input item state (bits 4..15) and is shown, not bought: the original store never capped a
// purchase by what the player already holds.
struct JonesUiStoreCart
{
    int startBalance = 0;
    int balance = 0;
    int total = 0;
    int quantities[kJonesStoreItemCount] = {};
    int held[kJonesStoreItemCount] = {};
    bool available[kJonesStoreItemCount] = {};
};

// Item table accessors, in JonesHud_aStoreItems order.
int JonesUiStore_GetPrice(std::size_t item);
const char* JonesUiStore_GetNameKey(std::size_t item);
const char* JonesUiStore_GetClipNameKey(std::size_t item);
bool JonesUiStore_IsBonusMap(std::size_t item);

// Reads the starting balance and each item's availability and held count from apItemsState, and
// starts an empty cart. apItemsState may be null, which leaves everything unavailable.
void JonesUiStoreCart_Init(JonesUiStoreCart& cart, int balance, const int* apItemsState);

// The largest quantity the cart can hold for item at the current balance: its current quantity
// plus floor(balance / price). An unavailable or free item cannot grow. This is the original
// store's only per-item limit; it has no separate maximum count.
int JonesUiStoreCart_GetMaxQuantity(const JonesUiStoreCart& cart, std::size_t item);

// Adds count of item. Refuses when the item is unavailable or the cost exceeds the remaining
// balance, like the original's balance test. A successful bonus-map add reports AddedBonusMap.
JonesUiStoreAddResult JonesUiStoreCart_AddItem(JonesUiStoreCart& cart, std::size_t item,
    int count);

// Returns count of item to the balance, clamped to what is in the cart. A negative count and an
// out-of-range item do nothing. Returns the number actually removed.
int JonesUiStoreCart_RemoveItem(JonesUiStoreCart& cart, std::size_t item, int count);

// Empties the cart and returns its total to the balance.
void JonesUiStoreCart_Clear(JonesUiStoreCart& cart);

// Recomputes total from the quantities and balance from startBalance, matching the two controls
// the original jonesConfig_UpdateBalances wrote.
void JonesUiStoreCart_UpdateBalances(JonesUiStoreCart& cart);

// Writes the cart the way jonesConfig_ShowStoreDialog did after the dialog whatever it returned:
// *pBalance becomes the remaining balance and each apItemsState entry keeps its high 16 bits with
// the low 16 replaced by the bought quantity. The screen calls this on every close. A null output
// is ignored.
void JonesUiStore_ApplyPurchase(const JonesUiStoreCart& cart, int* pBalance, int* apItemsState);

// The JONES_STR_* key of the message an add result shows, or null for Added/NotAvailable.
const char* JonesUiStore_AddResultTextKey(JonesUiStoreAddResult result);

// The exact text jonesConfig_InitPurchaseMessageBox built for this cart: a leading newline, the
// add-inventory text, the subtotal format with total and the yes/no text when something is in the
// cart; otherwise the no-change and abandon texts.
std::string JonesUiStore_BuildPurchaseText(const JonesUiStoreCart& cart);

// Puts the cart screen on top of ui and runs it until it closes. Done (1), Cancel (2) after the
// purchase prompt answered Yes, and the close paths without the prompt all write the cart through
// JonesUiStore_ApplyPurchase, exactly as the original did. Back and the window close behave like
// the original's WM_CLOSE: the dialog manager turns them into Cancel (2), so they run the same
// prompt; a quit closes without it. pPainter may be null.
int JonesUiStore_RunStoreScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    int* pBalance, int* apItemsState, int unknown2);

#endif // JONES3D_PLATFORM_BABYLON_JONESUISTORE_H
