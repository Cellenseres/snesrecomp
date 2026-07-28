/* Synthetic regression for SNES OBJ range/fetch ordering.
 * No game ROM, generated data, or platform frontend is required. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "snes/ppu.h"
#include "snes/snes.h"

Snes *g_snes;

void ppu_draw_whole_line_legacy(Ppu *ppu, int line) {
    (void)ppu;
    (void)line;
}

uint16_t WsShadowTile(int layer, int screen_x, uint32_t wrapped_y,
                      uint16_t real_tile) {
    (void)layer;
    (void)screen_x;
    (void)wrapped_y;
    return real_tile;
}

bool WsShadowLayerActive(int layer) {
    (void)layer;
    return false;
}

uint32_t WsShadowWorldX(int layer) {
    (void)layer;
    return 0;
}

uint32_t WsShadowPresentWorldY(int layer, int screen_x) {
    (void)layer;
    (void)screen_x;
    return 0;
}

uint32_t WsShadowScrollY(int layer) {
    (void)layer;
    return 0;
}

void WsShadowOnVramWrite(uint16_t word_adr, uint16_t value) {
    (void)word_adr;
    (void)value;
}

static int check(bool condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition ? 0 : 1;
}

static void no_op_line_enhancer(Ppu *ppu, uint y, bool sub, void *context) {
    (void)ppu;
    (void)y;
    (void)sub;
    (void)context;
}

int main(void) {
    enum { kPitch = kPpuXPixels * 4 };
    uint8_t pixels[kPitch];
    Ppu *ppu = ppu_init();
    int failures = 0;
    if (!ppu) return 2;
    memset(pixels, 0, sizeof pixels);
    ppu_reset(ppu);
    PpuBeginDrawing(ppu, pixels, kPitch, kPpuRenderFlags_NewRenderer);
    ppu->inidisp = 0x0f;

    /* Keep unused OAM entries off this line. Slot 0 is one 8x8 sprite at x=0;
     * slots 1..5 are 64x64 sprites at x=64. Reverse tile fetch reaches the
     * 34-sliver limit before slot 0, while a forward one-pass implementation
     * incorrectly renders it. */
    for (int slot = 0; slot < 128; slot++)
        ppu->oam[slot * 2] = 0xf000;
    ppu->obsel = 2 << 5;  /* size pair 8x8 / 64x64 */
    ppu->oam[0] = 0x0000;
    for (int slot = 1; slot <= 5; slot++) {
        ppu->oam[slot * 2] = 0x0040;
        int high_byte = slot >> 2;
        int size_bit = ((slot & 3) * 2) + 1;
        ppu->highOam[high_byte] |= (uint8_t)(1u << size_bit);
    }
    for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
        ppu->vram[i] = 0xffff;

    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check(ppu->timeOver, "34-sliver overflow is reported");
    failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) == 0,
                      "reverse fetch drops low slot after sliver overflow");

    PpuBeginDrawing(ppu, pixels, kPitch,
                    kPpuRenderFlags_NewRenderer |
                    kPpuRenderFlags_NoSpriteLimits);
    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) != 0,
                      "disabling sprite limits renders the low slot");

    /* Existing line-enhancer users rely on BG1 staying inside its authentic
     * destination viewport. New title-specific source insets must be opt-in
     * and must not relax that legacy fallback. */
    {
        enum { kExtra = 8, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLineEnhancer(ppu, no_op_line_enhancer, NULL);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->screenEnabled[0] = 1;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] == 0 &&
                              wide_pixels[kExtra - 1] == 0 &&
                              wide_pixels[kExtra + kPpuXPixels] == 0,
                          "legacy line enhancer keeps BG1 out of margins");
        failures += check(wide_pixels[kExtra] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels - 1] != 0,
                          "legacy line enhancer retains native BG1");

        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerViewportInset(ppu, 0, 16, 16);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[kExtra] == 0 &&
                              wide_pixels[kExtra + 15] == 0 &&
                              wide_pixels[kExtra + 240] == 0,
                          "explicit BG1 viewport inset hides native padding");
        failures += check(wide_pixels[kExtra + 16] != 0 &&
                              wide_pixels[kExtra + 239] != 0,
                          "explicit BG1 viewport inset retains visible span");
    }

    ppu_free(ppu);
    if (failures) return 1;
    puts("ppu_sprite_limit_test: PASS");
    return 0;
}
