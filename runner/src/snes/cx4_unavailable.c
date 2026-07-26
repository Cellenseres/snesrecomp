/* Cx4 coprocessor — "not present" implementation.
 *
 * Compiled only when runner/src/snes/cx4.c is absent. cx4.c IS committed
 * (ares, ISC), so this is a safety net for an incomplete checkout rather
 * than the normal path. See CX4_NOTES.md in this directory.
 *
 * Effect per game:
 *
 *   The six non-Cx4 titles (SMW, Zelda, MMX1, Super Metroid, Star Fox,
 *   DKC2, Metal Warriors) are completely unaffected — nothing ever
 *   constructs a Cx4, so every code path is byte-identical to before Cx4
 *   support existed.
 *
 *   Mega Man X2 / X3 detect as CART_CX4, get a NULL device, and therefore
 *   spin on the Cx4 status register at $7F5E exactly as they did before any
 *   Cx4 work — but now with a one-time explanation on stderr instead of a
 *   silent black screen. That is a deliberate loud failure, not a
 *   regression: an unexplained hang is the thing we are preventing.
 *
 * This file contains no snes9x code and is safe to commit and release.
 */
#include "cx4.h"

#include <stdio.h>

struct Cx4 { int unused; };

Cx4 *cx4_create(const uint8_t *rom, uint32_t rom_size) {
  (void)rom;
  (void)rom_size;
  fprintf(stderr,
      "\n"
      "[cx4] ================================================================\n"
      "[cx4] This ROM needs the Capcom Cx4 coprocessor, which this build does\n"
      "[cx4] NOT have. The game will hang polling the Cx4 status register at\n"
      "[cx4] $7F5E and never lift forced blank (black screen).\n"
      "[cx4]\n"
      "[cx4] Why: runner/src/snes/cx4.c is missing from this checkout, so\n"
      "[cx4] cx4_unavailable.c was built instead.\n"
      "[cx4] See runner/src/snes/CX4_NOTES.md.\n"
      "[cx4] ================================================================\n"
      "\n");
  return NULL;
}

void cx4_destroy(Cx4 *cx4) { (void)cx4; }
void cx4_reset(Cx4 *cx4) { (void)cx4; }

uint8_t cx4_read(Cx4 *cx4, uint16_t addr) {
  (void)cx4;
  (void)addr;
  return 0;
}

void cx4_write(Cx4 *cx4, uint16_t addr, uint8_t val) {
  (void)cx4;
  (void)addr;
  (void)val;
}

uint8_t *cx4_ram_ptr(Cx4 *cx4, uint16_t addr) {
  (void)cx4;
  (void)addr;
  return NULL;
}

void cx4_saveload(Cx4 *cx4, struct SaveLoadInfo *sli) {
  (void)cx4;
  (void)sli;
}

uint32_t cx4_cmd_ring_count(const Cx4 *cx4) { (void)cx4; return 0; }
uint32_t cx4_unknown_cmd_count(const Cx4 *cx4) { (void)cx4; return 0; }

uint32_t cx4_cmd_ring_copy(const Cx4 *cx4, Cx4CmdEvent *out, uint32_t max) {
  (void)cx4;
  (void)out;
  (void)max;
  return 0;
}
