#ifdef __3DS__
#include "global.h"
#include <overlays/misc/ovl_kaleido_scope/z_kaleido_scope.h>
#include "soh/framebuffer_effects.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include "QuickItems3DS.h"
#include "QuickItemsPolicy3DS.h"

int Soh3dsTouchRead(int* x, int* y) __attribute__((weak));
static Soh3dsQuickCapture sCapture;
static const PlayState* sPlay;
static u32 sFrame;
static int sScene, sAge, sFile;
static uintptr_t sContext;
static const char* sSlotKeys[2] = { CVAR_ENHANCEMENT("DualScreen.QuickSlot1"),
                                  CVAR_ENHANCEMENT("DualScreen.QuickSlot2") };

int Soh3dsQuickItems_Slot(int shortcut) {
    if (shortcut == 0) return SLOT_OCARINA;
    if (shortcut < 1 || shortcut > 2) return -1;
    int slot = CVarGetInteger(sSlotKeys[shortcut - 1], -1);
    return slot >= 0 && slot < 24 ? slot : -1;
}

static int SlotItem(int slot) {
    if (slot < 0 || slot >= 24) return ITEM_NONE;
    int item = gSaveContext.inventory.items[slot];
    if (item >= ITEM_NONE_FE || item == ITEM_SOLD_OUT) return ITEM_NONE;
    if (item == ITEM_ARROW_FIRE || item == ITEM_ARROW_ICE || item == ITEM_ARROW_LIGHT) {
        if (gSaveContext.inventory.items[SLOT_BOW] != ITEM_BOW) return ITEM_NONE;
        return item == ITEM_ARROW_FIRE ? ITEM_BOW_ARROW_FIRE :
               item == ITEM_ARROW_ICE ? ITEM_BOW_ARROW_ICE : ITEM_BOW_ARROW_LIGHT;
    }
    return item;
}

int Soh3dsQuickItems_Item(int shortcut) { return SlotItem(Soh3dsQuickItems_Slot(shortcut)); }

static int CanPlay(PlayState* play) {
    if (!play || play->pauseCtx.state || play->pauseCtx.debugState ||
        play->msgCtx.msgMode != MSGMODE_NONE || Play_InCsMode(play) ||
        play->gameOverCtx.state != GAMEOVER_INACTIVE || gSaveContext.health <= 0 ||
        gSaveContext.gameMode != GAMEMODE_NORMAL || play->transitionTrigger != TRANS_TRIGGER_OFF ||
        play->transitionMode != TRANS_MODE_OFF || play->shootingGalleryStatus || play->bombchuBowlingStatus ||
        gSaveContext.minigameState || (gSaveContext.eventInf[0] & 0xF) == 1 ||
        play->sceneNum == SCENE_FISHING_POND || GameInteractor_PacifistModeActive()) return 0;
    Player* player = GET_PLAYER(play);
    if (!player || (player->stateFlags1 & (PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_CLIMBING_LADDER |
        PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_IN_CUTSCENE)) ||
        (player->stateFlags2 & PLAYER_STATE2_CRAWLING)) return 0;
    return 1;
}

static int CanUseSlot(PlayState* play, int slot) {
    if (!CanPlay(play) || slot < 0 || slot >= 24 || !CHECK_AGE_REQ_SLOT(slot)) return 0;
    int item = SlotItem(slot);
    if (item >= ITEM_NONE_FE) return 0;
    int hazard = Player_GetEnvironmentalHazard(play);
    if (hazard >= 2 && hazard < 5 &&
        !(hazard == 2 && (item == ITEM_HOOKSHOT || item == ITEM_LONGSHOT))) return 0;
    InterfaceContext* ui = &play->interfaceCtx;
    if (item >= ITEM_BOTTLE && item <= ITEM_POE) return ui->restrictions.bottles == 0;
    if (item >= ITEM_WEIRD_EGG && item <= ITEM_CLAIM_CHECK) return ui->restrictions.tradeItems == 0;
    if (item == ITEM_OCARINA_FAIRY || item == ITEM_OCARINA_TIME) return ui->restrictions.ocarina == 0;
    if (item == ITEM_HOOKSHOT || item == ITEM_LONGSHOT) {
        if (ui->restrictions.hookshot) return 0;
    } else if (item == ITEM_FARORES_WIND) {
        if (ui->restrictions.farores) return 0;
    } else if (item == ITEM_DINS_FIRE || item == ITEM_NAYRUS_LOVE) {
        if (ui->restrictions.dinsNayrus) return 0;
    }
    return !ui->restrictions.all || (item == ITEM_LENS && play->sceneNum == SCENE_TREASURE_BOX_SHOP);
}

int Soh3dsQuickItems_CanUse(PlayState* play, int shortcut) {
    return CanUseSlot(play, Soh3dsQuickItems_Slot(shortcut));
}

int Soh3dsQuickItems_Assign(PlayState* play, int shortcut, int slot) {
    /* C-button equip hooks perform equipment changes (for example ItemUnequip).
     * A shortcut is a separate binding; registering it must not invoke them. */
    if (shortcut < 1 || shortcut > 2 || !CanPlay(play) || slot < 0 || slot >= 24 ||
        SlotItem(slot) >= ITEM_NONE_FE || !CHECK_AGE_REQ_SLOT(slot)) return 0;
    CVarSetInteger(sSlotKeys[shortcut - 1], slot);
    CVarSave();
    return 1;
}

void Soh3dsQuickItems_Update(PlayState* play) {
    if (sPlay != play || sFrame > play->state.frames || sScene != play->sceneNum ||
        sAge != gSaveContext.linkAge || sFile != gSaveContext.fileNum) ++sContext;
    sPlay = play; sFrame = play->state.frames; sScene = play->sceneNum;
    sAge = gSaveContext.linkAge; sFile = gSaveContext.fileNum;
    int x = 0, y = 0;
    int held = Soh3dsTouchRead && Soh3dsTouchRead(&x, &y);
    int tab = Soh3dsBottomScreen_Tab();
    int target = (tab == SOH3DS_TAB_MAP || tab == SOH3DS_TAB_GEAR) ? Soh3dsQuickTarget(x, y) : -1;
    int allowed = target >= 0 && Soh3dsQuickItems_CanUse(play, target) &&
                  !(play->state.input[0].press.button & BTN_START);
    Soh3dsQuickCapture_Update(&sCapture, held, target, allowed, sContext);
}

int Soh3dsQuickItems_Button(PlayState* play, int pressed) {
    int shortcut = sCapture.active;
    if (shortcut < 0 || (pressed && !sCapture.pressed) || !Soh3dsQuickItems_CanUse(play, shortcut)) return -1;
    return 8 + Soh3dsQuickItems_Slot(shortcut);
}

int Soh3dsQuickItems_ButtonItem(PlayState* play, int button) {
    if (button < 8 || button >= 32 || !CanUseSlot(play, button - 8)) return ITEM_NONE;
    return SlotItem(button - 8);
}

int Soh3dsQuickItems_UpdateBottle(PlayState* play, int item, int button) {
    if (button < 8) return 0;
    int slot = button - 8;
    if (slot < SLOT_BOTTLE_1 || slot > SLOT_BOTTLE_4) return 1;
    if (GameInteractor_Should(VB_EMPTY_BOTTLE_TO_HALF_MILK,
        gSaveContext.inventory.items[slot] == ITEM_MILK_BOTTLE && item == ITEM_BOTTLE, button, item)) item = ITEM_MILK_HALF;
    if (GameInteractor_Should(VB_UPDATE_BOTTLE_ITEM, true, button, item)) gSaveContext.inventory.items[slot] = item;
    /* Update actual equipped copies of this bottle, leaving every other slot alone. */
    for (int i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); ++i) {
        if (gSaveContext.equips.cButtonSlots[i - 1] == slot) {
            gSaveContext.equips.buttonItems[i] = item;
            Interface_LoadItemIcon1(play, i);
            /* Index four is B's saved status; extension buttons skip it. */
            gSaveContext.buttonStatus[i >= 4 ? i + 1 : i] = BTN_ENABLED;
        }
    }
    play->pauseCtx.cursorItem[PAUSE_ITEM] = item;
    return 1;
}
#endif
