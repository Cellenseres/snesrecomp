#include <stdint.h>
#include <stdio.h>

#include "dsp1_hle.h"

static int check(int condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
  }
  return 0;
}

int main(void) {
  int fails = 0;
  int16_t input[3] = {0x4000, 0x4000, 0};
  int16_t output[2] = {0, 0};
  uint8_t output_words = 0;

  fails += check(dsp1_hle_execute(0x00, input, 2, output, 2, &output_words),
                 "multiply command is implemented");
  fails += check(output_words == 1 && output[0] == 0x2000,
                 "multiply fixed-point result");

  fails += check(dsp1_hle_execute(0x20, input, 2, output, 2, &output_words),
                 "multiply-plus-one command is implemented");
  fails += check(output_words == 1 && output[0] == 0x2001,
                 "multiply-plus-one fixed-point result");

  input[0] = 0x4ae5;
  input[1] = 0x3a4f;
  fails += check(dsp1_hle_execute(0x20, input, 2, output, 2, &output_words),
                 "odd multiply-plus-one executes");
  fails += check(output_words == 1 && output[0] == 0x221d,
                 "multiply-plus-one sets rather than increments low bit");

  input[0] = 0x1000;
  input[1] = 0;
  input[2] = 0;
  fails += check(dsp1_hle_execute(0x08, input, 3, output, 2, &output_words),
                 "vector-size command is implemented");
  fails += check(output_words == 2 && output[0] == 0 &&
                     output[1] == 0x0200,
                 "vector-size 32-bit result");

  input[0] = 0;
  input[1] = 0x4000;
  fails += check(dsp1_hle_execute(0x04, input, 2, output, 2, &output_words),
                 "sin/cos command is implemented");
  fails += check(output_words == 2 && output[0] == 0 &&
                     output[1] == 0x3fff,
                 "sin/cos axis result");

  input[0] = 0;
  input[1] = 0x4000;
  input[2] = 0x2000;
  fails += check(dsp1_hle_execute(0x0c, input, 3, output, 2, &output_words),
                 "2D rotate command is implemented");
  fails += check(output_words == 2 && output[0] == 0x3fff &&
                     output[1] == 0x1fff,
                 "2D identity rotation result");

  input[0] = 0x4000;
  input[1] = 0;
  fails += check(dsp1_hle_execute(0x10, input, 2, output, 2, &output_words),
                 "inverse command is implemented");
  fails += check(output_words == 2 && output[0] == 0x7fff &&
                     output[1] == 1,
                 "inverse normalized result");

  input[0] = 0x1000;
  input[1] = 0;
  input[2] = 0;
  int16_t range_input[4] = {0x1000, 0, 0, 0x0800};
  fails += check(
      dsp1_hle_execute(0x18, range_input, 4, output, 2, &output_words),
      "range command is implemented");
  fails += check(output_words == 1 && output[0] == 0x0180,
                 "range comparison result");
  fails += check(dsp1_hle_execute(0x80, NULL, 0, NULL, 0, &output_words) &&
                     output_words == 0,
                 "stream terminator is a supported no-op");
  fails += check(!dsp1_hle_execute(0x02, input, 3, output, 2, &output_words),
                 "projection command remains unavailable until verified");
  fails += check(output_words == 0,
                 "failed command clears the reported output size");
  fails += check(!dsp1_hle_execute(0x08, input, 2, output, 2, &output_words),
                 "incorrect input size is rejected");

  if (fails) return 1;
  puts("dsp1_hle_test: PASS");
  return 0;
}
