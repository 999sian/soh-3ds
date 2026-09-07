#ifdef __3DS__

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "soh/framebuffer_effects.h"
#include "BitmapFont3DS.h"
#include "RandomizerMenu3DS.h"

#define RANDO_ROW_Y 30
#define RANDO_ROW_H 26

extern const u8 default_font_bin[];
static u8 sRandoGlyphs[95][64] __attribute__((aligned(8)));
static u8 sRandoAdvance[95];
static int sRandoFontReady;

static void Rm_InitFont(void) {
    int c;
    if (sRandoFontReady) {
        return;
    }
    for (c = 32; c < 127; ++c) {
        sRandoAdvance[c - 32] = BsFont_Expand(default_font_bin + c * 8, sRandoGlyphs[c - 32]);
    }
    sRandoFontReady = 1;
}

static int Rm_Glyph(const char* text, int length, int* at) {
    unsigned char c = (unsigned char)text[(*at)++];
    if (c >= 128) {
        while (*at < length && ((unsigned char)text[*at] & 0xC0) == 0x80) {
            ++*at;
        }
        c = '?';
    }
    return c >= 32 && c < 127 ? c - 32 : '?' - 32;
}

static int Rm_TextWidth(const char* text) {
    int at = 0;
    int width = 0;
    int length = strlen(text);
    Rm_InitFont();
    while (at < length) {
        width += sRandoAdvance[Rm_Glyph(text, length, &at)];
    }
    return width;
}

static void Rm_Text(GameState* state, const char* text, int x, int y, u8 r, u8 g, u8 b, int maxWidth) {
    int at = 0;
    int width = 0;
    int dots = 0;
    int length = strlen(text);
    int clipped;
    int available;
    Rm_InitFont();
    clipped = Rm_TextWidth(text) > maxWidth;
    available = clipped ? maxWidth - 3 * sRandoAdvance['.' - 32] : maxWidth;
    OPEN_DISPS(state->gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCombineLERP(POLY_OPA_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0,
                      PRIMITIVE, 0);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetTextureFilter(POLY_OPA_DISP++, G_TF_POINT);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, 255);
    while (at < length || (clipped && dots < 3)) {
        int glyph;
        if (at < length) {
            glyph = Rm_Glyph(text, length, &at);
            if (width + sRandoAdvance[glyph] > available) {
                at = length;
                continue;
            }
        } else {
            glyph = '.' - 32;
            ++dots;
        }
        if (glyph != 0) {
            gDPLoadTextureBlock(POLY_OPA_DISP++, sRandoGlyphs[glyph], G_IM_FMT_I, G_IM_SIZ_8b, 8, 8, 0, G_TX_CLAMP,
                                G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            gSPTextureRectangle(POLY_OPA_DISP++, (x + width) << 2, y << 2, (x + width + 8) << 2, (y + 8) << 2,
                                G_TX_RENDERTILE, 0, 0, 1024, 1024);
        }
        width += sRandoAdvance[glyph];
    }
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetTextureFilter(POLY_OPA_DISP++, G_TF_BILERP);
    CLOSE_DISPS(state->gfxCtx);
}

static Gfx* Rm_FillState(Gfx* gfx) {
    gDPPipeSync(gfx++);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetRenderMode(gfx++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    return gfx;
}

static Gfx* Rm_Fill(Gfx* gfx, int x0, int y0, int x1, int y1, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync(gfx++);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gDPFillRectangle(gfx++, x0, y0, x1, y1);
    return gfx;
}

void Soh3dsRandoMenu_Draw(GameState* state) {
    Soh3dsRandoMenuView view;
    char pageText[24];
    Gfx* gfx;
    int i;
    if (!Soh3dsRandoMenu_IsOpen() || gBottomScreenFrameBuffer == -1) {
        return;
    }
    Soh3dsRandoMenu_GetView(&view);
    OPEN_DISPS(state->gfxCtx);
    gsSPSetFB(POLY_OPA_DISP++, gBottomScreenFrameBuffer);
    gSoh3dsBottomPass = 1;
    Gfx_SetupDL_39Opa(state->gfxCtx);
    gfx = Rm_FillState(POLY_OPA_DISP);
    gfx = Rm_Fill(gfx, 0, 0, 319, 239, 24, 24, 22, 255);
    gfx = Rm_Fill(gfx, 0, 0, 319, 25, 38, 36, 32, 255);
    gfx = Rm_Fill(gfx, 0, 26, 319, 27, 108, 102, 90, 255);
    gfx = Rm_Fill(gfx, 0, 214, 319, 239, 30, 29, 26, 255);
    for (i = 0; i < view.rowCount; ++i) {
        int y = RANDO_ROW_Y + i * RANDO_ROW_H;
        int selected = i == view.selectedRow;
        gfx = Rm_Fill(gfx, 8, y, 311, y + RANDO_ROW_H - 3, selected ? 70 : 40, selected ? 64 : 38,
                      selected ? 52 : 34, 240);
    }
    POLY_OPA_DISP = gfx;
    if (view.picker) {
        int width;
        snprintf(pageText, sizeof(pageText), "Page %d/%d", view.pageCount > 0 ? view.pageIndex + 1 : 0,
                 view.pageCount);
        Rm_Text(state, "Back", 10, 4, 240, 232, 208, 58);
        Rm_Text(state, view.title, 82, 4, 240, 232, 208, 150);
        Rm_Text(state, "Prev", 244, 4, view.pageIndex > 0 ? 240 : 125, view.pageIndex > 0 ? 232 : 125,
                view.pageIndex > 0 ? 208 : 125, 34);
        Rm_Text(state, "Next", 282, 4, view.pageIndex + 1 < view.pageCount ? 240 : 125,
                view.pageIndex + 1 < view.pageCount ? 232 : 125,
                view.pageIndex + 1 < view.pageCount ? 208 : 125, 36);
        width = Rm_TextWidth(pageText);
        Rm_Text(state, pageText, (320 - width) / 2, 14, 205, 198, 180, width);
    } else {
        Rm_Text(state, "Back", 10, 9, 240, 232, 208, 58);
        Rm_Text(state, view.title, 82, 9, 240, 232, 208, 228);
    }
    for (i = 0; i < view.rowCount; ++i) {
        int y = RANDO_ROW_Y + i * RANDO_ROW_H + 8;
        u8 color = view.rows[i].disabled ? 125 : 240;
        Rm_Text(state, view.rows[i].label, 14, view.settings ? y - 5 : y,
                color, color, color > 20 ? color - 20 : 0,
                view.settings || !view.rows[i].value[0] ? 292 : 154);
        if (view.rows[i].value[0] != '\0') {
            int width = Rm_TextWidth(view.rows[i].value);
            if (view.settings) {
                Rm_Text(state, "<", 178, y + 6, color, color, color, 10);
                Rm_Text(state, view.rows[i].value, 26, y + 6, color, color, color, 144);
                Rm_Text(state, ">", 284, y + 6, color, color, color, 10);
            } else {
                Rm_Text(state, view.rows[i].value, 306 - (width < 134 ? width : 134), y, 233, 206, 142, 134);
            }
        }
    }
    Rm_Text(state, view.status, 10, 223, view.busy ? 233 : 205, view.busy ? 206 : 198, view.busy ? 142 : 180, 300);
    OPEN_DISPS(state->gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gSoh3dsBottomPass = 0;
    gsSPResetFB(POLY_OPA_DISP++);
    CLOSE_DISPS(state->gfxCtx);
    CLOSE_DISPS(state->gfxCtx);
}

#endif
