#include "dsp1_hle.h"

#include <math.h>
#include <stddef.h>

static const double kPi = 3.14159265358979323846264338327950288;

/* Synthesize the firmware's high-byte sine grid and low-byte interpolation. */
static int16_t q15_sin_grid(uint16_t angle) {
  int16_t grid_angle = (int16_t)(angle & 0xff00u);
  double value = sin((double)grid_angle * (kPi / 32768.0)) * 32768.0;
  if (value >= 32767.0) return 32767;
  if (value <= -32768.0) return -32768;
  return (int16_t)value;
}

static int32_t q15_subangle(uint8_t fraction) {
  return ((int32_t)fraction * 4 * 0x6488) >> 15;
}

static int16_t dsp1_sin(uint16_t angle) {
  int16_t signed_angle = (int16_t)angle;
  if (signed_angle < 0) {
    if (signed_angle == INT16_MIN) return 0;
    return (int16_t)-dsp1_sin((uint16_t)-signed_angle);
  }
  int32_t sine = q15_sin_grid(angle);
  int32_t cosine = q15_sin_grid((uint16_t)(angle + 0x4000u));
  int32_t fraction = q15_subangle((uint8_t)angle);
  int32_t result = sine + ((fraction * cosine) >> 15);
  if (result > 32767) result = 32767;
  return (int16_t)result;
}

static int16_t dsp1_cos(uint16_t angle) {
  int16_t signed_angle = (int16_t)angle;
  if (signed_angle < 0) {
    if (signed_angle == INT16_MIN) return INT16_MIN;
    angle = (uint16_t)-signed_angle;
  }
  int32_t sine = q15_sin_grid(angle);
  int32_t cosine = q15_sin_grid((uint16_t)(angle + 0x4000u));
  int32_t fraction = q15_subangle((uint8_t)angle);
  int32_t result = cosine - ((fraction * sine) >> 15);
  if (result < -32768) result = -32767;
  return (int16_t)result;
}

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
    case 0x10:
      inputs = 2;
      outputs = 2;
      break;
    case 0x18:
      inputs = 4;
      outputs = 1;
      break;
    case 0x08:
      inputs = 3;
      outputs = 2;
      break;
    case 0x0c:
      inputs = 3;
      outputs = 2;
      break;
    case 0x04:
      inputs = 2;
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
    case 0x04: {
      int32_t radius = input[1];
      output[0] = (int16_t)(
          (radius * (int32_t)dsp1_sin((uint16_t)input[0])) >> 15);
      output[1] = (int16_t)(
          (radius * (int32_t)dsp1_cos((uint16_t)input[0])) >> 15);
      break;
    }
    case 0x0c: {
      int32_t sine = dsp1_sin((uint16_t)input[0]);
      int32_t cosine = dsp1_cos((uint16_t)input[0]);
      output[0] = (int16_t)(((int32_t)input[2] * sine >> 15) +
                            ((int32_t)input[1] * cosine >> 15));
      output[1] = (int16_t)(((int32_t)input[2] * cosine >> 15) -
                            ((int32_t)input[1] * sine >> 15));
      break;
    }
    case 0x10: {
      int32_t coefficient = input[0];
      int16_t exponent = input[1];
      if (!coefficient) {
        output[0] = 0x7fff;
        output[1] = 0x002f;
        break;
      }
      int32_t sign = 1;
      if (coefficient < 0) {
        if (coefficient == INT16_MIN) coefficient = 32767;
        else coefficient = -coefficient;
        sign = -1;
      }
      while (coefficient < 0x4000) {
        coefficient <<= 1;
        exponent--;
      }
      if (coefficient == 0x4000) {
        if (sign > 0) output[0] = 0x7fff;
        else {
          output[0] = -0x4000;
          exponent--;
        }
      } else {
        /* The 128 firmware seeds are nearest(2^29 / bucket coefficient). */
        uint32_t seed_coefficient =
            0x4000u + (((uint32_t)coefficient - 0x4000u) & ~0x7fu);
        int32_t seed =
            (int32_t)(((1u << 29) + seed_coefficient / 2) / seed_coefficient);
        int16_t estimate = (int16_t)(seed > 0x7fff ? 0x7fff : seed);
        for (unsigned iteration = 0; iteration < 2; iteration++) {
          int32_t product = (coefficient * (int32_t)estimate) >> 15;
          int32_t correction = (-(int32_t)estimate * product) >> 15;
          estimate = (int16_t)((estimate + correction) * 2);
        }
        output[0] = (int16_t)(sign * estimate);
      }
      output[1] = (int16_t)(1 - exponent);
      break;
    }
    case 0x18: {
      int64_t range =
          (int64_t)input[0] * input[0] + (int64_t)input[1] * input[1] +
          (int64_t)input[2] * input[2] - (int64_t)input[3] * input[3];
      output[0] = (int16_t)(range >> 15);
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
