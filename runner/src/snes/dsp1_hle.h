#ifndef SNES_DSP1_HLE_H
#define SNES_DSP1_HLE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Firmware-free DSP-1 command model. This interface is deliberately separate
 * from the host DR/SR protocol until every command needed by a target has
 * passed differential tests against the instruction-level core.
 */
bool dsp1_hle_command_shape(uint8_t command, uint8_t *input_words,
                            uint8_t *output_words);
bool dsp1_hle_execute(uint8_t command, const int16_t *input,
                      uint8_t input_words, int16_t *output,
                      uint8_t output_capacity, uint8_t *output_words);

#endif /* SNES_DSP1_HLE_H */
