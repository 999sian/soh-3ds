// SoH-3DS dual screen: Items, Gear, Map, Tracker and Settings touch tabs.
// Inventory supports C-button equipment and two independent quick shortcuts.
// The live minimap is drawn by Interface_Draw into gBottomScreenZoomFrameBuffer.
// Everything here is 320x240 Fast3D 2D on
// POLY_OPA_DISP bracketed by gsSPSetFB(gBottomScreenFrameBuffer).
//
// Touch arrives once per frame from the window backend (Soh3dsTouchRead);
// the state machine below is the usual tap-vs-drag split on a 10 px slop,
// modelled on igawa6/dusklight's companion_touch.cpp (CC0).
#ifdef __3DS__
#include <stdio.h>
#include <string.h>
#include "global.h"
#include <overlays/misc/ovl_kaleido_scope/z_kaleido_scope.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/OTRGlobals.h"
#include "soh/framebuffer_effects.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipUtils.h"
#include "SettingsBridge3DS.h"
#include "BitmapFont3DS.h"
#include "ControlsBridge3DS.h"
#include "TrackerBridge3DS.h"
#include "QuickItems3DS.h"
#include "QuickItemsPolicy3DS.h"

int Soh3dsTouchRead(int* x, int* y) __attribute__((weak));

#define TAB_H 22
#define TAB_W 64
// GEAR: 4 rows (sword, shield, tunic, boots) x [upgrade | 3 equipment cells]
#define GEAR_X 72
#define GRID_X 28
#define GRID_Y 30
#define CELL 44
#define ROW_H 40
#define ICON 32
#define TARGET_Y 196
#define TARGET_W 36
#define TARGET_X0 86
#define TARGET_PITCH 56
#define SLOP_SQ 100
#define ROW_PITCH 20

static struct {
    int tab;
    int heldFrames; // consecutive samples with the panel pressed
    int down;
    int downX, downY;
    int x, y;
    int dragSlot; // inventory slot under the finger at touch-down, or -1
    int dragging; // moved past the slop with an item in hand
    int selSlot;  // tap-selected inventory slot, or -1
    // SETTINGS: page index (0 = the port's own page, then SoH's menu
    // sidebars), list scroll in px, last-tapped row for the tooltip box.
    int page;
    int scroll;
    int selRow;
} sUi = { SOH3DS_TAB_ITEMS, 0, 0, 0, 0, 0, 0, -1, 0, -1, 0, 0, -1 };

// SETTINGS layout: header (page name + arrows), 8-row list, tooltip box.
#define SET_HEAD_Y TAB_H
#define SET_HEAD_H 20
#define SET_LIST_Y (SET_HEAD_Y + SET_HEAD_H)
#define SET_ROWS 8
#define SET_LIST_H (SET_ROWS * ROW_PITCH)
#define SET_TIP_Y (SET_LIST_Y + SET_LIST_H)
#define SET_VALUE_X 196 // value column; taps left/right of its middle step -/+
#define SET_ARROW_W 40

// Page 0 is the port's own settings (CVars SoH's menu does not know about);
// page 1 is Controls; pages 2.. are SoH's menu sidebars via SettingsBridge3DS.
static int Bs_PageCount(void) {
    return sUi.tab == SOH3DS_TAB_TRACKER ? Soh3dsTracker_PageCount() : 2 + Soh3dsSettings_PageCount();
}

static const char* Bs_PageName(int page) {
    if (sUi.tab == SOH3DS_TAB_TRACKER) return Soh3dsTracker_PageName(page);
    return page == 0 ? "3DS" : page == 1 ? "Controls" : Soh3dsSettings_PageName(page - 2);
}

static int Bs_RowCount(int page) {
    if (sUi.tab == SOH3DS_TAB_TRACKER) return Soh3dsTracker_RowCount(page);
    return page == 0 ? 3 : page == 1 ? Soh3dsControls_RowCount() : Soh3dsSettings_RowCount(page - 2);
}

static int Bs_GetRow(int page, int row, Soh3dsSettingsRow* out) {
    if (sUi.tab == SOH3DS_TAB_TRACKER) return Soh3dsTracker_Row(page, row, out);
    if (page == 1) return Soh3dsControls_Row(row, out);
    if (page != 0) {
        return Soh3dsSettings_Row(page - 2, row, out);
    }
    static char value[16];
    out->value = "";
    out->on = 0;
    out->disabled = 0;
    switch (row) {
        case 0:
            out->kind = SOH3DS_ROW_HEADING;
            out->label = "Dual screen";
            out->tooltip = "";
            return 1;
        case 1:
            out->kind = SOH3DS_ROW_TOGGLE;
            out->label = "Pause menu on bottom screen";
            out->tooltip = "Draw the pause pages on the bottom screen; the top keeps the frozen game view.";
            out->on = CVarGetInteger(CVAR_ENHANCEMENT("DualScreen.Pause"), 1) != 0;
            return 1;
        case 2:
            out->kind = SOH3DS_ROW_CHOICE;
            out->label = "Minimap zoom";
            out->tooltip = "How much the Map tab enlarges the minimap.";
            snprintf(value, sizeof(value), "%.1fx", CVarGetFloat(CVAR_ENHANCEMENT("DualScreen.MinimapScale"), 2.0f));
            out->value = value;
            return 1;
        default:
            return 0;
    }
}

static void Bs_AdjustRow(int page, int row, int dir) {
    if (sUi.tab == SOH3DS_TAB_TRACKER) return;
    if (page == 1) { Soh3dsControls_Adjust(row, dir); return; }
    if (page != 0) {
        Soh3dsSettings_Adjust(page - 2, row, dir);
        return;
    }
    if (row == 1) {
        CVarSetInteger(CVAR_ENHANCEMENT("DualScreen.Pause"), !CVarGetInteger(CVAR_ENHANCEMENT("DualScreen.Pause"), 1));
    } else if (row == 2) {
        float scale = CVarGetFloat(CVAR_ENHANCEMENT("DualScreen.MinimapScale"), 2.0f) + 0.5f * dir;
        CVarSetFloat(CVAR_ENHANCEMENT("DualScreen.MinimapScale"), scale < 1.0f ? 2.0f : scale > 2.0f ? 1.0f : scale);
    } else {
        return;
    }
    CVarSave();
}

int Soh3dsBottomScreen_Tab(void) {
    return sUi.tab;
}

static int Bs_SlotAt(int x, int y) {
    int col = (x - GRID_X) / CELL;
    int row = (y - GRID_Y) / ROW_H;
    if (x < GRID_X || y < GRID_Y || col >= 6 || row >= 4) {
        return -1;
    }
    return row * 6 + col;
}

static int Bs_TargetAt(int x, int y) {
    if (y < TARGET_Y || y >= TARGET_Y + TARGET_W) {
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        int x0 = TARGET_X0 + i * TARGET_PITCH;
        if (x >= x0 && x < x0 + TARGET_W) {
            return i;
        }
    }
    return -1;
}

static int Bs_QuickAssignAt(int x, int y) {
    if (y < TARGET_Y || y >= TARGET_Y + TARGET_W) return -1;
    if (x >= 12 && x < 48) return 1;
    if (x >= 272 && x < 308) return 2;
    return -1;
}

static void Bs_Sfx(u16 id) {
    Audio_PlaySfxGeneral(id, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                         &gSfxDefaultReverb);
}

static int Bs_CanEquipNow(PlayState* play) {
    return play->pauseCtx.state == 0 && play->msgCtx.msgMode == MSGMODE_NONE && !Play_InCsMode(play) &&
           play->gameOverCtx.state == GAMEOVER_INACTIVE;
}

// Same commit as KaleidoScope_UpdateItemEquip minus the flying-icon animation:
// arrows become bow+arrow, an item already on another C button swaps.
static void Bs_Equip(PlayState* play, int cbtn, int slot) {
    u16 item = gSaveContext.inventory.items[slot];
    if (!Bs_CanEquipNow(play) || item == ITEM_NONE || item == ITEM_SOLD_OUT || !CHECK_AGE_REQ_SLOT(slot) ||
        !GameInteractor_Should(VB_EQUIP_ITEM_TO_C_BUTTON, true, play, slot, item)) {
        Bs_Sfx(NA_SE_SY_ERROR);
        return;
    }
    if (item == ITEM_ARROW_FIRE || item == ITEM_ARROW_ICE || item == ITEM_ARROW_LIGHT) {
        item = item == ITEM_ARROW_FIRE ? ITEM_BOW_ARROW_FIRE :
               item == ITEM_ARROW_ICE ? ITEM_BOW_ARROW_ICE : ITEM_BOW_ARROW_LIGHT;
        if (!CVarGetInteger(CVAR_ENHANCEMENT("SeparateArrows"), 0)) {
            slot = SLOT_BOW;
        }
    }
    u16 targetButton = cbtn + 1;
    for (u16 other = 0; other < ARRAY_COUNT(gSaveContext.equips.cButtonSlots); other++) {
        u16 otherButton = other + 1;
        if ((int)other == cbtn) {
            continue;
        }
        if (slot == gSaveContext.equips.cButtonSlots[other]) {
            if (gSaveContext.equips.buttonItems[targetButton] != ITEM_NONE) {
                gSaveContext.equips.buttonItems[otherButton] = gSaveContext.equips.buttonItems[targetButton];
                gSaveContext.equips.cButtonSlots[other] = gSaveContext.equips.cButtonSlots[cbtn];
                Interface_LoadItemIcon2(play, otherButton);
            } else {
                gSaveContext.equips.buttonItems[otherButton] = ITEM_NONE;
                gSaveContext.equips.cButtonSlots[other] = SLOT_NONE;
            }
        }
        if (item == ITEM_BOW && gSaveContext.equips.buttonItems[otherButton] >= ITEM_BOW_ARROW_FIRE &&
            gSaveContext.equips.buttonItems[otherButton] <= ITEM_BOW_ARROW_LIGHT &&
            !CVarGetInteger(CVAR_ENHANCEMENT("SeparateArrows"), 0)) {
            gSaveContext.equips.buttonItems[otherButton] = gSaveContext.equips.buttonItems[targetButton];
            gSaveContext.equips.cButtonSlots[other] = gSaveContext.equips.cButtonSlots[cbtn];
            Interface_LoadItemIcon2(play, otherButton);
        }
    }
    gSaveContext.equips.buttonItems[targetButton] = item;
    gSaveContext.equips.cButtonSlots[cbtn] = slot;
    Interface_LoadItemIcon1(play, targetButton);
    { char line[64]; snprintf(line, sizeof(line), "soh-3ds bottom: equip item %u slot %d -> C%d\n", (unsigned)item, slot, cbtn); fputs(line, stderr); }
    Bs_Sfx(NA_SE_SY_DECIDE);
}

// GEAR cell under (x, y): row = equip type 0..3, col 0 = upgrade (display
// only), 1..3 = equipment value. Returns 0 when nothing is hit.
static int Bs_GearAt(int x, int y, int* type, int* value) {
    int col = (x - GEAR_X) / CELL;
    int row = (y - GRID_Y) / ROW_H;
    if (x < GEAR_X || y < GRID_Y || col >= 4 || row >= 4) {
        return 0;
    }
    *type = row;
    *value = col;
    return 1;
}

// The pause Equipment page's commit (KaleidoScope_DrawEquipment) applied
// live, the way SoH's "equip now" prompt does: change the save's equipment,
// sword also rewrites the B button, then Player_SetEquipmentData swaps the
// model/boots data. value is 1..3.
static void Bs_EquipGear(PlayState* play, int type, int value) {
    Player* player = GET_PLAYER(play);
    if (!Bs_CanEquipNow(play) || player == NULL || !CHECK_OWNED_EQUIP(type, value - 1) ||
        !CHECK_AGE_REQ_EQUIP(type, value)) {
        Bs_Sfx(NA_SE_SY_ERROR);
        return;
    }
    Inventory_ChangeEquipment(type, value);
    if (type == EQUIP_TYPE_SWORD) {
        gSaveContext.infTable[29] = 0;
        gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI + value - 1;
        if (value == 3 && gSaveContext.bgsFlag != 0) {
            gSaveContext.equips.buttonItems[0] = ITEM_SWORD_BGS;
            gSaveContext.swordHealth = 8;
        } else if (gSaveContext.equips.buttonItems[0] == ITEM_SWORD_BGS && gSaveContext.bgsFlag == 0 &&
                   CHECK_OWNED_EQUIP_ALT(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_BROKENGIANTKNIFE)) {
            gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KNIFE;
        }
        Interface_LoadItemIcon1(play, 0);
    }
    Player_SetEquipmentData(play, player);
    { char line[48]; snprintf(line, sizeof(line), "soh-3ds bottom: gear type %d value %d\n", type, value); fputs(line, stderr); }
    Bs_Sfx(NA_SE_SY_DECIDE);
}

static int Bs_ScrollMax(void) {
    int max = Bs_RowCount(sUi.page) * ROW_PITCH - SET_LIST_H;
    return max < 0 ? 0 : max;
}

static void Bs_SetPage(int page) {
    int count = Bs_PageCount();
    sUi.page = (page % count + count) % count;
    sUi.scroll = 0;
    sUi.selRow = -1;
    Bs_Sfx(NA_SE_SY_CURSOR);
}

// A tap on the SETTINGS tab (release without a scroll drag).
static void Bs_SettingsTap(int x, int y) {
    if (y < SET_LIST_Y) {
        if (x < SET_ARROW_W) {
            Bs_SetPage(sUi.page - 1);
        } else if (x >= 320 - SET_ARROW_W) {
            Bs_SetPage(sUi.page + 1);
        }
        return;
    }
    if (y >= SET_TIP_Y) {
        return;
    }
    int row = (y - SET_LIST_Y + sUi.scroll) / ROW_PITCH;
    Soh3dsSettingsRow info;
    if (row >= Bs_RowCount(sUi.page) || !Bs_GetRow(sUi.page, row, &info)) {
        return;
    }
    sUi.selRow = row;
    if (info.kind == SOH3DS_ROW_HEADING || info.kind == SOH3DS_ROW_TEXT) {
        return;
    }
    if (info.disabled) {
        Bs_Sfx(NA_SE_SY_ERROR);
        return;
    }
    // Toggles flip anywhere; choices/sliders step down on the left half of
    // the value column and up everywhere else.
    int dir = (info.kind != SOH3DS_ROW_TOGGLE && x >= SET_VALUE_X && x < (SET_VALUE_X + 312) / 2) ? -1 : 1;
    Bs_AdjustRow(sUi.page, row, dir);
    { char line[48]; snprintf(line, sizeof(line), "soh-3ds bottom: adjust page %d row %d %+d\n", sUi.page, row, dir); fputs(line, stderr); }
    Bs_Sfx(NA_SE_SY_CURSOR);
}

static void Bs_Update(PlayState* play) {
    int x = 0, y = 0;
    int held = Soh3dsTouchRead != NULL && Soh3dsTouchRead(&x, &y);
    if (held) {
        // Two consecutive samples before a press counts: Azahar emits stray
        // one-frame touches along the screen seam, and a resistive panel's
        // first contact sample is the noisiest.
        if (++sUi.heldFrames < 2) {
            return;
        }
        // SETTINGS list: finger-follow scroll while held (dusklight's map
        // pan pattern); the tap is decided on release from the total travel.
        if (sUi.down && (sUi.tab == SOH3DS_TAB_SETTINGS || sUi.tab == SOH3DS_TAB_TRACKER) &&
            sUi.downY >= SET_LIST_Y && sUi.downY < SET_TIP_Y) {
            sUi.scroll -= y - sUi.y;
            int max = Bs_ScrollMax();
            sUi.scroll = sUi.scroll < 0 ? 0 : sUi.scroll > max ? max : sUi.scroll;
        }
        sUi.x = x;
        sUi.y = y;
        if (!sUi.down) {
            sUi.down = 1;
            sUi.downX = x;
            sUi.downY = y;
            sUi.dragging = 0;
            sUi.dragSlot = -1;
            if (sUi.tab == SOH3DS_TAB_ITEMS) {
                int slot = Bs_SlotAt(x, y);
                if (slot >= 0 && gSaveContext.inventory.items[slot] != ITEM_NONE) {
                    sUi.dragSlot = slot;
                }
            }
        } else if (sUi.dragSlot >= 0 && !sUi.dragging) {
            int dx = x - sUi.downX, dy = y - sUi.downY;
            if (dx * dx + dy * dy > SLOP_SQ) {
                sUi.dragging = 1;
                sUi.selSlot = -1;
            }
        }
        return;
    }
    sUi.heldFrames = 0;
    if (!sUi.down) {
        return;
    }
    // Release: the last held position is where the finger lifted.
    sUi.down = 0;
    x = sUi.x;
    y = sUi.y;
    if (sUi.dragging) {
        int quick = Bs_QuickAssignAt(x, y);
        int target = Bs_TargetAt(x, y);
        if (quick >= 0) {
            Bs_Sfx(Soh3dsQuickItems_Assign(play, quick, sUi.dragSlot) ? NA_SE_SY_DECIDE : NA_SE_SY_ERROR);
        } else if (target >= 0) {
            Bs_Equip(play, target, sUi.dragSlot);
        }
        sUi.dragging = 0;
        sUi.dragSlot = -1;
        return;
    }
    if (y < TAB_H) {
        int tab = x / TAB_W;
        if (tab < SOH3DS_TAB_COUNT && tab != sUi.tab) {
            sUi.tab = tab;
            sUi.page = 0;
            sUi.scroll = 0;
            sUi.selRow = -1;
            { char line[32]; snprintf(line, sizeof(line), "soh-3ds bottom: tab %d\n", tab); fputs(line, stderr); }
            sUi.selSlot = -1;
            Bs_Sfx(NA_SE_SY_CURSOR);
        }
        return;
    }
    switch (sUi.tab) {
        case SOH3DS_TAB_ITEMS: {
            int quick = Bs_QuickAssignAt(x, y);
            if (quick >= 0 && sUi.selSlot >= 0) {
                Bs_Sfx(Soh3dsQuickItems_Assign(play, quick, sUi.selSlot) ? NA_SE_SY_DECIDE : NA_SE_SY_ERROR);
                sUi.selSlot = -1;
                break;
            }
            int slot = Bs_SlotAt(x, y);
            if (slot >= 0) {
                if (gSaveContext.inventory.items[slot] != ITEM_NONE) {
                    sUi.selSlot = slot == sUi.selSlot ? -1 : slot;
                    Bs_Sfx(NA_SE_SY_CURSOR);
                }
                return;
            }
            int target = Bs_TargetAt(x, y);
            if (target >= 0 && sUi.selSlot >= 0) {
                Bs_Equip(play, target, sUi.selSlot);
                sUi.selSlot = -1;
            }
            break;
        }
        case SOH3DS_TAB_GEAR: {
            int type, value;
            if (Bs_GearAt(x, y, &type, &value) && value > 0) {
                Bs_EquipGear(play, type, value);
            }
            break;
        }
        case SOH3DS_TAB_TRACKER:
        case SOH3DS_TAB_SETTINGS: {
            int dx = x - sUi.downX, dy = y - sUi.downY;
            if (dx * dx + dy * dy <= SLOP_SQ) {
                Bs_SettingsTap(x, y);
            }
            break;
        }
        default:
            break;
    }
}

// --- drawing ---------------------------------------------------------------

static Gfx* Bs_FillState(Gfx* gfx) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    return gfx;
}

static Gfx* Bs_Fill(Gfx* gfx, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gDPFillRectangle(gfx++, x0, y0, x1, y1);
    return gfx;
}

static Gfx* Bs_IconState(Gfx* gfx) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, 255);
    return gfx;
}

static Gfx* Bs_Icon(Gfx* gfx, u16 item, int x, int y, u8 alpha) {
    if (item >= ARRAY_COUNT(gItemIcons)) {
        return gfx;
    }
    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, 255, 255, 255, alpha);
    gDPLoadTextureBlock(gfx++, gItemIcons[item], G_IM_FMT_RGBA, G_IM_SIZ_32b, ICON, ICON, 0,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    gSPTextureRectangle(gfx++, x << 2, y << 2, (x + ICON) << 2, (y + ICON) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10,
                        1 << 10);
    return gfx;
}

static Gfx* Bs_TextState(Gfx* gfx) {
    gDPPipeSync(gfx++);
    gDPSetCombineLERP(gfx++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE,
                      0);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    return gfx;
}

static void Bs_Text(PlayState* play, const char* s, int x, int y, u8 r, u8 g, u8 b, float scale) {
    Interface_DrawTextLine(play->state.gfxCtx, (char*)s, x, y, r, g, b, 255, scale, false);
}

static void Bs_SettingsText(PlayState* play, const char* s, int x, int y, u8 r, u8 g, u8 b, int maxWidth);

static void Bs_DrawTabs(PlayState* play, Gfx** gfxp) {
    static const char* names[SOH3DS_TAB_COUNT] = { "ITEMS", "GEAR", "MAP", "TRACKER", "SETTINGS" };
    Gfx* gfx = *gfxp;
    gfx = Bs_FillState(gfx);
    for (int i = 0; i < SOH3DS_TAB_COUNT; i++) {
        int x0 = i * TAB_W;
        if (i == sUi.tab) {
            gfx = Bs_Fill(gfx, x0, 0, x0 + TAB_W - 1, TAB_H - 1, 214, 206, 186, 255);
        } else {
            gfx = Bs_Fill(gfx, x0, 0, x0 + TAB_W - 1, TAB_H - 1, 38, 36, 32, 255);
        }
    }
    gfx = Bs_Fill(gfx, 0, TAB_H - 1, 319, TAB_H, 108, 102, 90, 255);
    *gfxp = Bs_TextState(gfx);
    for (int i = 0; i < SOH3DS_TAB_COUNT; i++) {
        if (i == sUi.tab) {
            Bs_SettingsText(play, names[i], i * TAB_W + 5, 5, 46, 36, 21, 56);
        } else {
            Bs_SettingsText(play, names[i], i * TAB_W + 5, 5, 240, 232, 208, 56);
        }
    }
}

// Equipment page: left column the row's upgrade (bullet bag/quiver, bomb
// bag, strength, scale), then the three owned/equipped pieces of that type.
static void Bs_DrawGear(PlayState* play, Gfx** gfxp) {
    static const char* rowNames[4] = { "Sword", "Shield", "Tunic", "Boots" };
    static const u8 childUpgrades[4] = { UPG_BULLET_BAG, UPG_BOMB_BAG, UPG_STRENGTH, UPG_SCALE };
    static const u8 adultUpgrades[4] = { UPG_QUIVER, UPG_BOMB_BAG, UPG_STRENGTH, UPG_SCALE };
    static const u8 upgradeItemBases[4] = { ITEM_BULLET_BAG_30, ITEM_BOMB_BAG_20, ITEM_BRACELET, ITEM_SCALE_SILVER };
    static const u8 adultUpgradeItemBases[4] = { ITEM_QUIVER_30, ITEM_BOMB_BAG_20, ITEM_BRACELET, ITEM_SCALE_SILVER };
    const int child = LINK_AGE_IN_YEARS == YEARS_CHILD;
    Gfx* gfx = *gfxp;
    gfx = Bs_FillState(gfx);
    for (int type = 0; type < 4; type++) {
        int y = GRID_Y + type * ROW_H;
        for (int col = 0; col < 4; col++) {
            int x = GEAR_X + col * CELL;
            int equipped = col > 0 && CUR_EQUIP_VALUE(type) == col;
            gfx = Bs_Fill(gfx, x + 1, y + 1, x + CELL - 3, y + ROW_H - 3, equipped ? 233 : 40, equipped ? 206 : 38,
                          equipped ? 142 : 34, equipped ? 255 : 230);
        }
    }
    gfx = Bs_IconState(gfx);
    for (int type = 0; type < 4; type++) {
        int y = GRID_Y + type * ROW_H + (ROW_H - 2 - ICON) / 2;
        int level = CUR_UPG_VALUE(child ? childUpgrades[type] : adultUpgrades[type]);
        if (level != 0) {
            u16 item = (child ? upgradeItemBases[type] : adultUpgradeItemBases[type]) + level - 1;
            gfx = Bs_Icon(gfx, item, GEAR_X + (CELL - 2 - ICON) / 2, y, 255);
        }
        for (int col = 1; col < 4; col++) {
            if (!CHECK_OWNED_EQUIP(type, col - 1)) {
                continue;
            }
            int x = GEAR_X + col * CELL + (CELL - 2 - ICON) / 2;
            u16 item = ITEM_SWORD_KOKIRI + type * 3 + col - 1;
            u8 alpha = CHECK_AGE_REQ_EQUIP(type, col) ? 255 : 90;
            if (type == EQUIP_TYPE_SWORD && col == 3 && gSaveContext.bgsFlag == 0 &&
                CHECK_OWNED_EQUIP_ALT(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_BROKENGIANTKNIFE)) {
                item = ITEM_SWORD_KNIFE; // the broken Giant's Knife, as the pause page shows it
            }
            gfx = Bs_Icon(gfx, item, x, y, alpha);
        }
    }
    *gfxp = Bs_TextState(gfx);
    for (int type = 0; type < 4; type++) {
        Bs_Text(play, rowNames[type], 10, GRID_Y + type * ROW_H + 14, 240, 232, 208, 0.7f);
    }
}

static void Bs_DrawItems(PlayState* play, Gfx** gfxp) {
    static const char* targetNames[3] = { "C<", "Cv", "C>" };
    Gfx* gfx = *gfxp;
    gfx = Bs_FillState(gfx);
    for (int slot = 0; slot < 24; slot++) {
        int x = GRID_X + (slot % 6) * CELL;
        int y = GRID_Y + (slot / 6) * ROW_H;
        int sel = slot == sUi.selSlot;
        gfx = Bs_Fill(gfx, x + 1, y + 1, x + CELL - 3, y + ROW_H - 3, sel ? 233 : 40, sel ? 206 : 38, sel ? 142 : 34,
                   sel ? 255 : 230);
    }
    for (int i = 0; i < 3; i++) {
        int x0 = TARGET_X0 + i * TARGET_PITCH;
        gfx = Bs_Fill(gfx, x0 - 2, TARGET_Y - 2, x0 + TARGET_W + 1, TARGET_Y + TARGET_W + 1, 108, 102, 90, 255);
        gfx = Bs_Fill(gfx, x0, TARGET_Y, x0 + TARGET_W - 1, TARGET_Y + TARGET_W - 1, 24, 24, 22, 255);
    }
    for (int i = 1; i <= 2; ++i) {
        int x = i == 1 ? 12 : 272;
        gfx = Bs_Fill(gfx, x, TARGET_Y, x + 35, TARGET_Y + 35, 65, 79, 85, 255);
    }
    gfx = Bs_IconState(gfx);
    for (int slot = 0; slot < 24; slot++) {
        u16 item = gSaveContext.inventory.items[slot];
        if (item == ITEM_NONE || (sUi.dragging && slot == sUi.dragSlot)) {
            continue;
        }
        int x = GRID_X + (slot % 6) * CELL + (CELL - 2 - ICON) / 2;
        int y = GRID_Y + (slot / 6) * ROW_H + (ROW_H - 2 - ICON) / 2;
        gfx = Bs_Icon(gfx, item, x, y, CHECK_AGE_REQ_SLOT(slot) ? 255 : 90);
    }
    for (int i = 0; i < 3; i++) {
        u16 item = gSaveContext.equips.buttonItems[i + 1];
        if (item != ITEM_NONE) {
            gfx = Bs_Icon(gfx, item, TARGET_X0 + i * TARGET_PITCH + 2, TARGET_Y + 2, 255);
        }
    }
    for (int i = 1; i <= 2; ++i) {
        int item = Soh3dsQuickItems_Item(i);
        if (item < ITEM_NONE_FE) gfx = Bs_Icon(gfx, item, (i == 1 ? 12 : 272) + 2, TARGET_Y + 2, 255);
    }
    if (sUi.dragging) {
        gfx = Bs_Icon(gfx, gSaveContext.inventory.items[sUi.dragSlot], sUi.x - ICON / 2, sUi.y - ICON / 2, 255);
    }
    *gfxp = Bs_TextState(gfx);
    for (int i = 0; i < 3; i++) {
        Bs_Text(play, targetNames[i], TARGET_X0 + i * TARGET_PITCH - 14, TARGET_Y + 12, 240, 232, 208, 0.6f);
    }
    Bs_SettingsText(play, "I", 14, TARGET_Y - 12, 166, 216, 228, 30);
    Bs_SettingsText(play, "II", 274, TARGET_Y - 12, 166, 216, 228, 30);
}

// Reuse the font already linked by libctru's boot console. Expanded glyphs
// have process lifetime so the texture cache never retains temporary pointers.
extern const u8 default_font_bin[];
static u8 sSettingsGlyphs[95][64] __attribute__((aligned(8)));
static u8 sSettingsAdvance[95];
static int sSettingsFontReady;

static void Bs_InitSettingsFont(void) {
    if (sSettingsFontReady) return;
    for (int c = 32; c < 127; ++c) {
        sSettingsAdvance[c - 32] = BsFont_Expand(default_font_bin + c * 8, sSettingsGlyphs[c - 32]);
    }
    sSettingsFontReady = 1;
}

// Unsupported UTF-8 is one replacement glyph, rather than several bytes
// indexing unrelated glyphs. The settings bridge currently supplies English.
static int Bs_FontCharacter(const char* s, int len, int* at) {
    unsigned char c = (unsigned char)s[(*at)++];
    if (c >= 128) {
        while (*at < len && ((unsigned char)s[*at] & 0xC0) == 0x80) ++*at;
        c = '?';
    }
    return c >= 32 && c < 127 ? c - 32 : '?' - 32;
}

static int Bs_TextWidth(const char* s, int len) {
    Bs_InitSettingsFont();
    int width = 0;
    for (int at = 0; at < len;) width += sSettingsAdvance[Bs_FontCharacter(s, len, &at)];
    return width;
}

static void Bs_SettingsText(PlayState* play, const char* s, int x, int y, u8 r, u8 g, u8 b, int maxWidth) {
    Bs_InitSettingsFont();
    int len = (int)strlen(s), at = 0, width = 0;
    const int clipped = Bs_TextWidth(s, len) > maxWidth;
    const int dotsWidth = 3 * sSettingsAdvance['.' - 32];
    const int available = clipped ? maxWidth - dotsWidth : maxWidth;
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetTextureFilter(POLY_OPA_DISP++, G_TF_POINT);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, 255);
    for (int dots = 0; at < len || (clipped && dots < 3);) {
        int glyph;
        if (at < len) {
            glyph = Bs_FontCharacter(s, len, &at);
            if (width + sSettingsAdvance[glyph] > available) { at = len; continue; }
        } else {
            glyph = '.' - 32;
            ++dots;
            if (width + sSettingsAdvance[glyph] > maxWidth) break;
        }
        if (glyph != 0) {
            gDPLoadTextureBlock(POLY_OPA_DISP++, sSettingsGlyphs[glyph], G_IM_FMT_I, G_IM_SIZ_8b, 8, 8, 0,
                                 G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPTextureRectangle(POLY_OPA_DISP++, (x + width) << 2, (y + 2) << 2,
                                (x + width + 8) << 2, (y + 10) << 2, G_TX_RENDERTILE, 0, 0, 1024, 1024);
        }
        width += sSettingsAdvance[glyph];
    }
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetTextureFilter(POLY_OPA_DISP++, G_TF_BILERP);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Greedy word wrap into `lines` rows of at most maxW px; the last row is
// cut with "..." when the text is longer. Rows are NUL-terminated copies.
static int Bs_Wrap(const char* text, int maxW, int lines, char out[][96]) {
    int n = 0;
    const char* p = text;
    while (*p != '\0' && n < lines) {
        while (*p == ' ') {
            p++;
        }
        const char* end = p;
        const char* lastSpace = NULL;
        while (*end != '\0' && *end != '\n' && Bs_TextWidth(p, (int)(end - p) + 1) <= maxW) {
            if (*end == ' ') {
                lastSpace = end;
            }
            end++;
        }
        if (*end != '\0' && *end != '\n' && lastSpace != NULL) {
            end = lastSpace;
        }
        int len = (int)(end - p);
        if (len > 95) {
            len = 95;
        }
        memcpy(out[n], p, len);
        out[n][len] = '\0';
        p = end;
        if (*p == '\n') {
            p++;
        }
        if (n == lines - 1 && *p != '\0' && len > 3) {
            memcpy(out[n] + len - 3, "...", 4);
        }
        n++;
    }
    return n;
}

static void Bs_DrawSettings(PlayState* play, Gfx** gfxp) {
    Soh3dsSettingsRow rows[SET_ROWS + 1];
    char labels[SET_ROWS + 1][96];
    char values[SET_ROWS + 1][48];
    int shown[SET_ROWS + 1];
    int count = Bs_RowCount(sUi.page);
    int max = Bs_ScrollMax();
    sUi.scroll = sUi.scroll > max ? max : sUi.scroll;
    int first = sUi.scroll / ROW_PITCH;
    int offset = sUi.scroll % ROW_PITCH;
    int n = 0;
    for (int i = 0; i < SET_ROWS + 1 && first + i < count; i++) {
        if (Bs_GetRow(sUi.page, first + i, &rows[n])) {
            // The bridge formats label/value into one shared buffer per call.
            snprintf(labels[n], sizeof(labels[n]), "%s", rows[n].label);
            snprintf(values[n], sizeof(values[n]), "%s", rows[n].value);
            rows[n].label = labels[n];
            rows[n].value = values[n];
            shown[n++] = first + i;
        }
    }

    Gfx* gfx = *gfxp;
    gfx = Bs_FillState(gfx);
    gfx = Bs_Fill(gfx, 0, SET_HEAD_Y, 319, SET_LIST_Y - 1, 38, 36, 32, 255);
    gfx = Bs_Fill(gfx, 0, SET_TIP_Y, 319, 239, 30, 29, 26, 255);
    for (int i = 0; i < n; i++) {
        int y = SET_LIST_Y + i * ROW_PITCH - offset;
        int y0 = y < SET_LIST_Y ? SET_LIST_Y : y;
        int y1 = y + ROW_PITCH - 3 > SET_TIP_Y - 1 ? SET_TIP_Y - 1 : y + ROW_PITCH - 3;
        if (y1 <= y0) {
            continue;
        }
        const Soh3dsSettingsRow* r = &rows[i];
        if (r->kind == SOH3DS_ROW_HEADING || r->kind == SOH3DS_ROW_TEXT) {
            continue;
        }
        int sel = shown[i] == sUi.selRow;
        gfx = Bs_Fill(gfx, 8, y0, 311, y1, sel ? 70 : 40, sel ? 64 : 38, sel ? 52 : 34, 230);
        if (r->kind == SOH3DS_ROW_TOGGLE && y + 3 >= SET_LIST_Y && y + ROW_PITCH - 6 < SET_TIP_Y) {
            int on = r->on && !r->disabled;
            gfx = Bs_Fill(gfx, 280, y + 3, 305, y + ROW_PITCH - 6, on ? 120 : 70, on ? 200 : 66, on ? 120 : 60,
                          r->disabled ? 120 : 255);
        }
    }
    *gfxp = Bs_TextState(gfx);

    char head[48];
    snprintf(head, sizeof(head), "%s  (%d/%d)", Bs_PageName(sUi.page), sUi.page + 1, Bs_PageCount());
    Bs_SettingsText(play, "<", 14, SET_HEAD_Y + 3, 233, 206, 142, 8);
    Bs_SettingsText(play, ">", 298, SET_HEAD_Y + 3, 233, 206, 142, 8);
    Bs_SettingsText(play, head, 160 - (Bs_TextWidth(head, (int)strlen(head)) < 240 ? Bs_TextWidth(head, (int)strlen(head)) : 240) / 2, SET_HEAD_Y + 4, 240, 232, 208, 240);
    for (int i = 0; i < n; i++) {
        int y = SET_LIST_Y + i * ROW_PITCH - offset + 2;
        if (y < SET_LIST_Y || y + 12 > SET_TIP_Y) {
            continue; // partially scrolled rows keep their plate, not their text
        }
        const Soh3dsSettingsRow* r = &rows[i];
        u8 dim = r->disabled ? 130 : 240;
        switch (r->kind) {
            case SOH3DS_ROW_HEADING:
                Bs_SettingsText(play, r->label, 10, y, 233, 206, 142, 300);
                break;
            case SOH3DS_ROW_TEXT:
                Bs_SettingsText(play, r->label, 14, y, 190, 184, 164, r->value[0] ? SET_VALUE_X - 28 : 292);
                if (r->value[0]) Bs_SettingsText(play, r->value, SET_VALUE_X, y, 233, 206, 142, 112);
                break;
            case SOH3DS_ROW_TOGGLE:
                Bs_SettingsText(play, r->label, 14, y, dim, dim, dim - 20, 260);
                break;
            default: {
                Bs_SettingsText(play, r->label, 14, y, dim, dim, dim - 20, SET_VALUE_X - 28);
                char value[56];
                snprintf(value, sizeof(value), "< %s >", r->value);
                Bs_SettingsText(play, value, 308 - (Bs_TextWidth(value, (int)strlen(value)) < 112 ? Bs_TextWidth(value, (int)strlen(value)) : 112), y, 233, 206, 142, 112);
                break;
            }
        }
    }
    Soh3dsSettingsRow selected;
    if (sUi.selRow >= 0 && sUi.selRow < count && Bs_GetRow(sUi.page, sUi.selRow, &selected) &&
        selected.tooltip != NULL && selected.tooltip[0] != '\0') {
        char lines[2][96];
        int m = Bs_Wrap(selected.tooltip, 300, 2, lines);
        for (int i = 0; i < m; i++) {
            Bs_SettingsText(play, lines[i], 10, SET_TIP_Y + 3 + i * 14, 220, 212, 192, 300);
        }
    }
}

static void Bs_DrawQuickItems(PlayState* play, Gfx** gfxp) {
    static const char* labels[] = { "Ocarina", "I", "II" };
    Gfx* gfx = Bs_FillState(*gfxp);
    gfx = Bs_Fill(gfx, 0, 200, 319, 239, 24, 24, 22, 255);
    for (int i = 0; i < 3; ++i) {
        int x = 10 + 102 * i;
        gfx = Bs_Fill(gfx, x, 202, x + 95, 237, 48, 51, 47, 255);
    }
    gfx = Bs_IconState(gfx);
    for (int i = 0; i < 3; ++i) {
        int item = Soh3dsQuickItems_Item(i);
        if (item < ITEM_NONE_FE) gfx = Bs_Icon(gfx, item, 12 + 102 * i, 204,
                                              Soh3dsQuickItems_CanUse(play, i) ? 255 : 90);
    }
    *gfxp = Bs_TextState(gfx);
    for (int i = 0; i < 3; ++i) {
        Bs_SettingsText(play, labels[i], 46 + 102 * i, 205, 233, 206, 142, 56);
        Bs_SettingsText(play, Soh3dsQuickItems_Item(i) < ITEM_NONE_FE ? "Hold/use" : "Empty",
                        46 + 102 * i, 220, 180, 180, 164, 56);
    }
}

void Soh3dsBottomScreen_Draw(PlayState* play) {
    if (gBottomScreenFrameBuffer == -1) {
        return;
    }
    Bs_Update(play);

    OPEN_DISPS(play->state.gfxCtx);
    gsSPSetFB(POLY_OPA_DISP++, gBottomScreenFrameBuffer);
    gSoh3dsBottomPass = 1;
    if (Soh3dsSetBottomPassZoom != NULL) {
        // The Map tab's minimap (Interface_Draw, zoom fb) sets its own zoom
        // per frame; reset here so a stale value cannot leak into it when the
        // map is not drawn (cutscene HUD-off frames).
        Soh3dsSetBottomPassZoom(1.0f, 0.0f, 0.0f);
    }
    Gfx_SetupDL_39Opa(play->state.gfxCtx);
    Gfx* gfx = POLY_OPA_DISP;
    gfx = Bs_FillState(gfx);
    gfx = Bs_Fill(gfx, 0, TAB_H, 319, 239, 24, 24, 22, 255);
    POLY_OPA_DISP = gfx;
    Bs_DrawTabs(play, &POLY_OPA_DISP);
    switch (sUi.tab) {
        case SOH3DS_TAB_ITEMS:
            Bs_DrawItems(play, &POLY_OPA_DISP);
            break;
        case SOH3DS_TAB_GEAR:
            Bs_DrawGear(play, &POLY_OPA_DISP);
            break;
        case SOH3DS_TAB_SETTINGS:
        case SOH3DS_TAB_TRACKER:
            Bs_DrawSettings(play, &POLY_OPA_DISP);
            break;
        default:
            break; // MAP: Interface_Draw puts the minimap on the zoom fb
    }
    if (sUi.tab == SOH3DS_TAB_MAP || sUi.tab == SOH3DS_TAB_GEAR) Bs_DrawQuickItems(play, &POLY_OPA_DISP);
    gDPPipeSync(POLY_OPA_DISP++);
    gSoh3dsBottomPass = 0;
    gsSPResetFB(POLY_OPA_DISP++);
    CLOSE_DISPS(play->state.gfxCtx);
}
#endif
