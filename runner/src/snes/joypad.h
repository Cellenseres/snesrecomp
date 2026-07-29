#ifndef SNES_JOYPAD_H
#define SNES_JOYPAD_H

#include <stdint.h>

struct Snes;

void joypad_write_strobe(struct Snes *snes, uint8_t value);
uint8_t joypad_read_serial(struct Snes *snes, unsigned port);

#endif /* SNES_JOYPAD_H */
