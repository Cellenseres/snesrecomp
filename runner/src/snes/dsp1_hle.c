#include "dsp1_hle.h"

#include <stddef.h>

bool dsp1_hle_command_shape(uint8_t command, uint8_t *input_words,
                            uint8_t *output_words) {
  uint8_t inputs;
  uint8_t outputs;
  switch (command) {
    case 0x00:
    case 0x20:
      inputs = 2;
      outputs = 1;
      break;
    case 0x08:
      inputs = 3;
      outputs = 2;
      break;
    case 0x80:
      inputs = 0;
      outputs = 0;
      break;
    default:
      return false;
  }
  if (input_words) *input_words = inputs;
  if (output_words) *output_words = outputs;
  return true;
}

bool dsp1_hle_execute(uint8_t command, const int16_t *input,
                      uint8_t input_words, int16_t *output,
                      uint8_t output_capacity, uint8_t *output_words) {
  uint8_t expected_inputs;
  uint8_t expected_outputs;
  if (output_words) *output_words = 0;
  if (!dsp1_hle_command_shape(command, &expected_inputs, &expected_outputs) ||
      input_words != expected_inputs || output_capacity < expected_outputs ||
      (expected_inputs && !input) || (expected_outputs && !output))
    return false;

  switch (command) {
    case 0x00:
    case 0x20: {
      int32_t product = (int32_t)input[0] * (int32_t)input[1];
      uint16_t result = (uint16_t)(product >> 15);
      /* Firmware uses OR M,#1, which differs from M+1 when M is already odd. */
      if (command == 0x20) result |= 1;
      output[0] = (int16_t)result;
      break;
    }
    case 0x08: {
      uint64_t radius =
          (uint64_t)((int32_t)input[0] * (int32_t)input[0]) +
          (uint64_t)((int32_t)input[1] * (int32_t)input[1]) +
          (uint64_t)((int32_t)input[2] * (int32_t)input[2]);
      uint32_t doubled = (uint32_t)(radius << 1);
      output[0] = (int16_t)(doubled & 0xffffu);
      output[1] = (int16_t)(doubled >> 16);
      break;
    }
    case 0x80:
      break;
    default:
      return false;
  }

  if (output_words) *output_words = expected_outputs;
  return true;
}
