#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "dsp1.h"

enum {
  kRqm = 0x80,
  kDrc = 0x04,
};

static uint64_t master_clock;

static int check(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
  }
  return 0;
}

static int wait_rqm(Dsp1 *d, int wanted) {
  for (unsigned i = 0; i < 100000; i++) {
    uint8_t sr = dsp1_read(d, 1);
    if (!!(sr & kRqm) == wanted) return 1;
    master_clock += 128;
    dsp1_sync(d, master_clock);
  }
  return 0;
}

static int write_command(Dsp1 *d, uint8_t command) {
  if (!wait_rqm(d, 1)) return 0;
  if (!(dsp1_read(d, 1) & kDrc)) return 0;
  dsp1_write(d, 0, command);
  return wait_rqm(d, 1);
}

static int write_word(Dsp1 *d, uint16_t value) {
  if (!wait_rqm(d, 1)) return 0;
  if (dsp1_read(d, 1) & kDrc) return 0;
  dsp1_write(d, 0, (uint8_t)value);
  dsp1_write(d, 0, (uint8_t)(value >> 8));
  return 1;
}

static int read_word(Dsp1 *d, uint16_t *value) {
  if (!wait_rqm(d, 1)) return 0;
  if (dsp1_read(d, 1) & kDrc) return 0;
  uint8_t lo = dsp1_read(d, 0);
  uint8_t hi = dsp1_read(d, 0);
  *value = (uint16_t)(lo | ((uint16_t)hi << 8));
  return 1;
}

int main(void) {
  const char *firmware = getenv("SNESRECOMP_DSP1_ROM");
  if (!firmware || !firmware[0]) {
    puts("dsp1_firmware_test: SKIP (SNESRECOMP_DSP1_ROM is unset)");
    return 0;
  }

  Dsp1 *d = dsp1_create();
  int fails = 0;
  uint16_t result = 0;
  uint16_t result2 = 0;

  fails += check(d != NULL, "dsp1_create");
  if (!d) return 1;
  fails += check(dsp1_load_firmware(d, NULL), "load DSP-1 firmware");
  fails += check(dsp1_firmware_loaded(d), "firmware reports loaded");

  master_clock = 100000;
  dsp1_sync(d, master_clock);
  fails += check(wait_rqm(d, 1), "firmware reaches command-ready state");
  fails += check((dsp1_read(d, 1) & kDrc) != 0,
                 "command-ready state selects 8-bit DR");

  fails += check(write_command(d, 0x00), "submit multiply command");
  fails += check(write_word(d, 0x4000), "submit multiply operand 1");
  fails += check(wait_rqm(d, 1), "firmware requests multiply operand 2");
  fails += check(write_word(d, 0x4000), "submit multiply operand 2");
  fails += check(read_word(d, &result), "read multiply result");
  fails += check(result == 0x2000, "DSP-1 multiply result");

  fails += check(write_command(d, 0x20), "submit multiply-plus-one command");
  fails += check(write_word(d, 0x4000), "submit multiply-plus-one operand 1");
  fails += check(write_word(d, 0x4000), "submit multiply-plus-one operand 2");
  fails += check(read_word(d, &result), "read multiply-plus-one result");
  fails += check(result == 0x2001, "DSP-1 multiply-plus-one result");

  fails += check(write_command(d, 0x04), "submit sin/cos command");
  fails += check(write_word(d, 0x0000), "submit sin/cos angle");
  fails += check(write_word(d, 0x4000), "submit sin/cos radius");
  fails += check(read_word(d, &result), "read sine result");
  fails += check(read_word(d, &result2), "read cosine result");
  fails += check(result == 0x0000 && result2 == 0x3fff,
                 "DSP-1 sin/cos axis result");

  fails += check(write_command(d, 0x08), "submit vector-size command");
  fails += check(write_word(d, 0x1000), "submit vector-size X");
  fails += check(write_word(d, 0x0000), "submit vector-size Y");
  fails += check(write_word(d, 0x0000), "submit vector-size Z");
  fails += check(read_word(d, &result), "read vector-size low word");
  fails += check(read_word(d, &result2), "read vector-size high word");
  fails += check(result == 0x0000 && result2 == 0x0200,
                 "DSP-1 vector-size result");

  fails += check(write_command(d, 0x0c), "submit 2D rotate command");
  fails += check(write_word(d, 0x0000), "submit 2D rotate angle");
  fails += check(write_word(d, 0x4000), "submit 2D rotate X");
  fails += check(write_word(d, 0x2000), "submit 2D rotate Y");
  fails += check(read_word(d, &result), "read 2D rotate X");
  fails += check(read_word(d, &result2), "read 2D rotate Y");
  fails += check(result == 0x3fff && result2 == 0x1fff,
                 "DSP-1 2D identity rotation");

  fails += check(dsp1_instructions_executed(d) > 0,
                 "firmware executed instructions");

  dsp1_destroy(d);
  if (fails) return 1;
  puts("dsp1_firmware_test: PASS");
  return 0;
}
