
#ifndef SNES_H
#define SNES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct Snes Snes;

#include "cpu.h"
#include "apu.h"
#include "dma.h"
#include "ppu.h"
#include "cart.h"
#include "saveload.h"

struct Snes {
  Cpu* cpu;
  Apu* apu;
  Ppu* ppu;
  Dma* dma;
  Cart* cart;
  uint16 input1_currentState;
  uint16 input2_currentState;
  bool joypadStrobe;
  uint8_t joypad1Index;
  uint8_t joypad2Index;
  uint16_t joypad1Latched;
  uint16_t joypad2Latched;
  bool disableRender;

  // ram data port ($2180-$2183)
  uint32_t ramAdr;
  uint8_t *ram;

  // Host timing anchor; excluded from savestates and reconciled after load.
  uint64_t beamMasterLast;

  // --- saveload blob starts here (hPos .. divideResult) ---
  uint16_t hPos;
  uint16_t vPos;
  double apuCatchupCycles;
  // nmi / irq
  bool hIrqEnabled;
  bool vIrqEnabled;
  bool nmiEnabled;
  uint16_t hTimer;
  uint16_t vTimer;
  bool inNmi;
  bool inIrq;
  bool inVblank;
  // joypad
  bool autoJoyRead;
  uint16_t autoJoyTimer;
  bool ppuLatch;
  // multiplication/division
  uint8_t multiplyA;
  uint16_t multiplyResult;
  uint16_t divideA;
  uint16_t divideResult;
};

Snes* snes_init(uint8_t *ram);
void snes_free(Snes* snes);
void snes_reset(Snes* snes, bool hard);
// used by dma, cpu
uint8_t snes_readBBus(Snes* snes, uint8_t adr);
void snes_writeBBus(Snes* snes, uint8_t adr, uint8_t val);
uint8_t snes_read(Snes* snes, uint32_t adr);
void snes_write(Snes* snes, uint32_t adr, uint8_t val);
uint8_t snes_readReg(Snes* snes, uint16_t adr);
void snes_writeReg(Snes* snes, uint16_t adr, uint8_t val);
uint16_t SwapInputBits(uint16_t x);


// snes_other.c functions:

bool snes_loadRom(Snes* snes, const uint8_t* data, int length);
/* Savestate format version for snes_saveload layout (RTLS header). */
void snes_saveload_set_version(uint32_t version);
void snes_saveload(Snes *snes, SaveLoadInfo *sli);
void snes_catchupApu(Snes *snes);
void snes_advance_master_cycles(Snes *snes, uint32_t clocks);
void snes_sync_master_clock(Snes *snes, uint64_t master_clock);
/* Master clock at which the enabled H/V IRQ comparator next matches, starting
 * from the live beam position (`now` is that position's master clock, normally
 * g_cpu.master_cycles). Returns false when no comparator is armed.
 *
 * `snes->inIrq` is a single latch, so a host that lets the CPU run a whole
 * frame per slice coalesces every raster IRQ in that frame into one delivery.
 * That silently breaks chained raster splits - a handler that re-arms vTimer
 * for the next band never sees its band, because the beam is already past it
 * when the host next looks. (F-Zero chains four splits per frame at lines
 * 18/28/47/86; only line 18 survived.) A frame-model host should cap each
 * execution slice at this clock so every comparator edge is delivered at its
 * own beam position. See docs/FRAME_MODEL_HOSTS.md. */
bool snes_next_irq_master(const Snes *snes, uint64_t now, uint64_t *out);

extern int snes_frame_counter;
#endif
