# Third-Party Attribution

snesrecomp incorporates the following third-party software.

## Native analyzer performance inspiration

Derrick Gold's independent Go port of the snesrecomp recompiler demonstrated
that moving the Python pipeline to a compiled implementation could deliver an
approximately 25x speedup. That result prompted the production native-analyzer
work in this repository.

- Project: https://github.com/DerrickGold/ar-recomp
- Go migration commit: https://github.com/DerrickGold/ar-recomp/commit/ae3d2d1ffaa87281241bb1a2822d5b3dde35ca96

No source from the Go implementation is incorporated into `recompiler-rs/`.
The Rust code's implementation foundation and provenance are documented below.

## Native analyzer foundation

The Rust instruction decoder, cfg parser, and ROM mapping foundation under
`recompiler-rs/` originated in Colin Curtin's `perplexes/snesrecomp`
`feat/superfx-gsu/recompiler-rs` work and has since been reduced to the native
analysis boundary, updated for the current Python semantics, and extended with
the whole-program fixed point and production integration.

- Upstream: https://github.com/perplexes/snesrecomp
- Original branch: `feat/superfx-gsu/recompiler-rs`
- License declared by the upstream crate: MIT

## ares — Capcom Cx4 / Hitachi HG51B S169 coprocessor

`runner/src/snes/cx4.{c,h}` is an instruction-level emulation of the Hitachi
HG51B S169 DSP that Capcom packaged as the Cx4 (used only by Mega Man X2 and
Mega Man X3). It is ported from **ares**, which is **ISC-licensed** —
permissive, notice-only, no field-of-use restriction.

- Upstream: https://github.com/ares-emulator/ares
- Files of origin: `ares/component/processor/hg51b/*` (core: registers,
  instruction decode, instruction semantics, cache/DMA/bus control) and
  `ares/sfc/coprocessor/hitachidsp/memory.cpp` (SNES-side address decode and
  IO register map)
- License: ISC (see below)

This is the faithful LLE floor: the DSP fetches and executes the game's own Cx4
program out of cartridge ROM. Any future host-side Cx4 shortcut must be a
**gated optimization layered on top of it**, authored from this core's observed
behavior — never a substitute for it.

### Derivation / modifications

- C++ → C11. ares is written against nall's bit-precise integer types
  (`n1`/`n5`/`n8`/`n15`/`n24`/`n48`) where every assignment silently truncates
  to the declared width. That masking is load-bearing arithmetic — the
  accumulator is 24 bits, the program counter is 8 bits and *wraps* to advance
  the instruction cache page, the multiplier is 48 bits. C has no such types, so
  every width is enforced by an explicit named mask (`M24`, `M15`, …) and the
  porting hazard is documented at the top of `cx4.c`.
- ares' 65536-entry `std::function` dispatch table (built at construction) is
  replaced by a two-level `switch` on the opcode's top 6 bits, sub-switching on
  the top 8 where the encoding requires it. Same mapping, expressed directly.
- Arithmetic shift right and sign-extension are written out explicitly rather
  than relying on implementation-defined signed `>>`.
- `ROR` by 0 or 24 is special-cased; the reference only survives that shift
  width by relying on 32-bit truncation.
- ares runs the DSP as a scheduler `Thread` synchronized against the CPU. Here
  it is a pull model: `cx4_sync(master_clock)` converts elapsed SNES master
  cycles into 20 MHz Cx4 cycles (carrying the remainder so the ratio does not
  drift) and runs the core until the budget is spent, wired into the engine's
  existing `cart_sync_coprocessors` seam. An idle fast path collapses the
  budget when the core is halted with nothing pending — exactly equivalent,
  minus millions of no-op calls.
- Added observability in keeping with this project's ring-buffer policy: an
  always-on ring of DSP program starts, a retired-instruction count, an
  `RDROM` hit counter, and a loud one-shot diagnostic when `RDROM` executes
  without firmware loaded. Exposed as the debug-server `cx4_state` command.
- The `disassembler.cpp` / `debugger.cpp` components were not ported.

### Firmware (NOT included, and not ours to ship)

The HG51B S169 carries a 1024-entry × 24-bit internal data ROM — a
reciprocal/division table — which is **not** part of the game ROM. `cx4.rom`,
3072 bytes. It is Capcom/Hitachi data; this project does not redistribute it,
and `.gitignore` refuses it. `cx4_load_firmware()` searches
`$SNESRECOMP_CX4_ROM`, `./cx4.rom`, `./firmware/cx4.rom`, and the game ROM's
directory, and reports loudly rather than silently computing on zeros.

Measured on Mega Man X2's boot self-test: the Cx4 program reads **all 1024**
data-ROM entries. The firmware is genuinely required, not optional.

### ISC License

```
ares

Copyright (c) 2004-2025 ares team, Near et al

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

## ares — Nintendo DSP-1 / NEC uPD7725 coprocessor

`runner/src/snes/dsp1.{c,h}` is an instruction-level emulation of the NEC
uPD7725 DSP as used by Nintendo DSP-1/DSP-1B cartridges, ported from **ares**,
which is **ISC-licensed**.

- Upstream: https://github.com/ares-emulator/ares
- Files of origin: `ares/component/processor/upd96050/*` (shared uPD7725 /
  uPD96050 core: registers, data-register protocol, instruction decode, and
  instruction semantics) and `ares/sfc/coprocessor/necdsp/memory.cpp`
  (SNES-side host port behavior)
- License: ISC (see the ares license text above)

### Derivation / modifications

- C++/nall bit-precise integer types were rewritten as C11 with explicit
  masking for the uPD7725 register widths: 11-bit PC, 10-bit RP, 8-bit DP, and
  4-bit stack pointer.
- The uPD96050-only geometry was omitted. The runner core models the uPD7725
  shape used by DSP-1/DSP-1B: 2048 x 24-bit program ROM, 1024 x 16-bit data
  ROM, and 256 x 16-bit data RAM.
- ares' scheduler-thread model is a pull model here: `dsp1_sync(master_clock)`
  converts elapsed SNES master cycles into 7.6 MHz DSP cycles and executes the
  firmware until the budget is spent.
- SNES board mapping is wired through the existing cart layer instead of a BML
  mapper. Super Mario Kart's SHVC-1K1X-compatible windows are supported:
  DSP host port at `$00-$1F/$80-$9F:6000-$7FFF` and battery SRAM at
  `$20-$3F/$A0-$BF:6000-$7FFF`. The board mask reduces the host address so
  `$6000-$6FFF` selects DR and `$7000-$7FFF` selects SR. The uPD7725 data RAM
  remains internal.
- The disassembler/debugger pieces were not ported.

### Firmware (NOT included, and not ours to ship)

DSP-1/DSP-1B firmware is used by the LLE core. It is Nintendo/NEC data; this
project does not redistribute it. `dsp1_load_firmware()` searches
`$SNESRECOMP_DSP1_ROM`, `./dsp1b.rom`, `./dsp1.rom`, `./dsp1.bin`,
`./firmware/dsp1b.rom`, `./firmware/dsp1.rom`, and `./firmware/dsp1.bin`.
Both ares-style firmware layout and common word-reversed `.bin` dumps are
accepted.

When no firmware is available, the independently derived
`runner/src/snes/dsp1_hle.{c,h}` command model handles the firmware-verified
SMK command set (`00`, `02`, `04`, `08`, `0a`, `0c`, `10`, `18`, `20`, and
`80`). The runtime keeps LLE as the preferred backend and stops without
fabricating output if HLE encounters an unverified command.

## LakeSnes — 65816 CPU core

`runner/src/snes/interp816.{c,h}`, the 65816 interpreter backing the
interpreter-fallback tier (see `docs/MULTI_TIER.md`), is derived from the CPU
core of **LakeSnes** by angelo_wf.

- Upstream: https://github.com/angelo-wf/lakesnes
- License: MIT

### Derivation / modifications

Recovered from `runner/src/snes/cpu.c` as it existed at commit `9de9855^` —
the snesrecomp tree's original LakeSnes adaptation, before the unused
interpreter was ripped on 2026-04-20 — then re-vendored for the
interpreter-fallback tier with:

- symbols namespaced `cpu_*` / `Cpu` → `interp816_*` / `Interp816` so the core
  coexists with the legacy `Cpu` debug shadow (`runner/src/snes/cpu.{c,h}`);
- the hardwired `snes_cpuRead` / `snes_cpuWrite` bus replaced with a
  caller-supplied callback bus (so the production adapter can route memory
  through the AOT `cpu_read8` / `cpu_write8` HLE bus);
- snesrecomp debug instrumentation removed (`pc_hist` / `DumpCpuHistory` and
  the top-of-`doOpcode` assert tripwire);
- `WAI` restored to stock behavior (`waiting = true`, was an assert);
- `BRK` routed to the `interp816_opcode_hook` bridge seam.

The exact transform is reproducible: `git show 9de9855^:runner/src/snes/cpu.c`,
then the renames + seam edits described above. The vendored core is validated
by `tests/interp816/` (directed opcode harness).

### MIT License

```
Copyright (c) 2021-2023 angelo_wf and contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
