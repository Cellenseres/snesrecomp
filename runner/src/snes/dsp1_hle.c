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

static int16_t q15_multiply(int16_t lhs, int16_t rhs) {
  return (int16_t)(((int32_t)lhs * (int32_t)rhs) >> 15);
}

static void normalize(int16_t value, int16_t *coefficient, int16_t *exponent) {
  uint16_t bits = value < 0 ? (uint16_t)~value : (uint16_t)value;
  unsigned shift = 0;
  while (shift < 15 && !(bits & (uint16_t)(0x4000u >> shift))) shift++;
  *coefficient = (int16_t)((uint16_t)value << shift);
  *exponent = (int16_t)(*exponent - (int16_t)shift);
}

static int16_t truncate_coefficient(int16_t coefficient, int16_t exponent) {
  if (exponent > 0)
    return coefficient > 0 ? 32767 : coefficient < 0 ? -32767 : 0;
  if (exponent < 0) {
    unsigned shift = (unsigned)-exponent;
    if (shift >= 16) return coefficient < 0 ? -1 : 0;
    return (int16_t)(coefficient >> shift);
  }
  return coefficient;
}

static void inverse(int16_t input_coefficient, int16_t input_exponent,
                    int16_t *output_coefficient, int16_t *output_exponent) {
  int32_t coefficient = input_coefficient;
  int16_t exponent = input_exponent;
  if (!coefficient) {
    *output_coefficient = 0x7fff;
    *output_exponent = 0x002f;
    return;
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
    if (sign > 0) *output_coefficient = 0x7fff;
    else {
      *output_coefficient = -0x4000;
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
    *output_coefficient = (int16_t)(sign * estimate);
  }
  *output_exponent = (int16_t)(1 - exponent);
}

void dsp1_hle_state_reset(Dsp1HleState *state) {
  if (!state) return;
  *state = (Dsp1HleState){0};
}

bool dsp1_hle_command_shape(uint8_t command, uint8_t *input_words,
                            uint8_t *output_words) {
  uint8_t inputs;
  uint8_t outputs;
  switch (command) {
    case 0x02:
      inputs = 7;
      outputs = 4;
      break;
    case 0x0a:
      inputs = 1;
      outputs = 4;
      break;
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

bool dsp1_hle_execute_state(Dsp1HleState *state, uint8_t command,
                            const int16_t *input, uint8_t input_words,
                            int16_t *output, uint8_t output_capacity,
                            uint8_t *output_words) {
  uint8_t expected_inputs;
  uint8_t expected_outputs;
  if (output_words) *output_words = 0;
  if (!dsp1_hle_command_shape(command, &expected_inputs, &expected_outputs) ||
      input_words != expected_inputs || output_capacity < expected_outputs ||
      (expected_inputs && !input) || (expected_outputs && !output))
    return false;

  switch (command) {
    case 0x02: {
      if (!state) return false;
      state->projection_valid = false;
      if (input[6] < -0x3800 || input[6] > 0x3800) return false;
      state->sin_aas = dsp1_sin((uint16_t)input[5]);
      state->cos_aas = dsp1_cos((uint16_t)input[5]);
      state->sin_azs = dsp1_sin((uint16_t)input[6]);
      state->cos_azs = dsp1_cos((uint16_t)input[6]);

      int16_t nx =
          q15_multiply(state->sin_azs, (int16_t)-(int32_t)state->sin_aas);
      int16_t ny = q15_multiply(state->sin_azs, state->cos_aas);
      int16_t nz = q15_multiply(state->cos_azs, 0x7fff);
      int16_t center_x =
          (int16_t)(input[0] + q15_multiply(input[3], nx));
      int16_t center_y =
          (int16_t)(input[1] + q15_multiply(input[3], ny));
      int16_t center_z =
          (int16_t)(input[2] + q15_multiply(input[3], nz));

      int16_t coefficient;
      int16_t exponent = 0;
      normalize(center_z, &coefficient, &exponent);
      state->vplane_coefficient = coefficient;
      state->vplane_exponent = exponent;

      int16_t secant_coefficient;
      int16_t secant_exponent;
      inverse(state->cos_azs, 0, &secant_coefficient, &secant_exponent);
      coefficient = q15_multiply(coefficient, secant_coefficient);
      normalize(coefficient, &coefficient, &exponent);
      exponent = (int16_t)(exponent + secant_exponent);
      int16_t center_offset = q15_multiply(
          truncate_coefficient(coefficient, exponent), state->sin_azs);
      center_x = (int16_t)(
          center_x + q15_multiply(center_offset, state->sin_aas));
      center_y = (int16_t)(
          center_y - q15_multiply(center_offset, state->cos_aas));

      state->v_offset = q15_multiply(input[4], state->cos_azs);
      inverse(state->sin_azs, 0, &secant_coefficient, &exponent);
      coefficient = state->v_offset;
      normalize(coefficient, &coefficient, &exponent);
      coefficient = q15_multiply(coefficient, secant_coefficient);
      normalize(coefficient, &coefficient, &exponent);

      output[0] = 0;
      output[1] =
          truncate_coefficient((int16_t)-(int32_t)coefficient, exponent);
      output[2] = center_x;
      output[3] = center_y;
      inverse(state->cos_azs, 0, &state->sec_azs_coefficient,
              &state->sec_azs_exponent);
      state->projection_valid = true;
      break;
    }
    case 0x0a: {
      if (!state || !state->projection_valid) return false;
      int16_t denominator =
          (int16_t)(q15_multiply(input[0], state->sin_azs) + state->v_offset);
      int16_t coefficient;
      int16_t exponent;
      inverse(denominator, 7, &coefficient, &exponent);
      exponent = (int16_t)(exponent + state->vplane_exponent);
      int16_t scale =
          q15_multiply(coefficient, state->vplane_coefficient);
      int16_t vertical_exponent =
          (int16_t)(exponent + state->sec_azs_exponent);

      normalize(scale, &coefficient, &exponent);
      coefficient = truncate_coefficient(coefficient, exponent);
      output[0] = q15_multiply(coefficient, state->cos_aas);
      output[2] = q15_multiply(coefficient, state->sin_aas);

      coefficient = q15_multiply(scale, state->sec_azs_coefficient);
      normalize(coefficient, &coefficient, &vertical_exponent);
      coefficient = truncate_coefficient(coefficient, vertical_exponent);
      output[1] = q15_multiply(
          coefficient, (int16_t)-(int32_t)state->sin_aas);
      output[3] = q15_multiply(coefficient, state->cos_aas);
      break;
    }
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
      inverse(input[0], input[1], &output[0], &output[1]);
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

bool dsp1_hle_execute(uint8_t command, const int16_t *input,
                      uint8_t input_words, int16_t *output,
                      uint8_t output_capacity, uint8_t *output_words) {
  return dsp1_hle_execute_state(NULL, command, input, input_words, output,
                                output_capacity, output_words);
}
