# Cx4 — Hitachi HG51B S169

**Status: LLE floor, permissively licensed, no release blocker.**

`cx4.c` is an instruction-level emulation of the HG51B S169 DSP that Capcom
packaged as the Cx4, ported from **ares** (**ISC** — permissive, notice-only,
no field-of-use restriction). It is committed and shippable. Full derivation
notes and the licence text are in `THIRD_PARTY_ATTRIBUTION.md`.

Used by exactly two commercial titles: Mega Man X2 and Mega Man X3.

## History — why this is not the snes9x model

Cx4 support was first stood up (2026-07-26) by porting snes9x's `c4emu.cpp`,
a **command-level** model that reimplements ~20 operations on the host rather
than running the chip. That got X2/X3 booting, but the Snes9x licence is
freeware/non-commercial-only, which conflicted with keeping this project
unencumbered. It was replaced the same day by the ares core, which is both
permissively licensed **and** a strictly better model.

The snes9x version survives locally as `*.reference` (gitignored, not compiled)
purely as a cross-check while the ares port is validated. Delete it once
validation is done. **Do not reintroduce it**, and do not author a future Cx4
HLE from it — author from this LLE core's observed behavior instead, which is
both cleaner legally and the correct methodology.

## What the LLE core proved that the command model hid

Measured on X2's boot self-test via `dbgprobe.py cx4`:

```
{"runs":2,"insns":21704,"rdrom_hits":1024,"firmware":1,"locked":0}
```

- **21,704 DSP instructions retired.** The Cx4 program is real code in
  cartridge ROM at `base $028000`, entry `PB $000E`.
- **All 1024 data-ROM entries read.** snes9x's model has no data ROM at all,
  so it was fabricating every one of those values.
- **`$7F4F` is not a command register — it is the program counter.** The values
  snes9x treated as command ids (`$5C`, `$89`) are entry PCs. Writing `$7F4F`
  while the core is halted starts it.

## Firmware — user-supplied, deliberately

The chip's internal 1024 × 24-bit reciprocal table is **not** in the game ROM.
`cx4.rom`, 3072 bytes, Capcom/Hitachi data. This project does not redistribute
it and `.gitignore` refuses it.

`cx4_load_firmware()` searches `$SNESRECOMP_CX4_ROM`, `./cx4.rom`,
`./firmware/cx4.rom`, then the game ROM's directory. Missing firmware is
reported loudly at load, and again the first time `RDROM` executes — because
zeros there silently corrupt every division the Cx4 program performs.

This is the expected posture for an LLE floor: the faithful path needs the real
firmware. A future HLE layer could remove that requirement, but only as a gated
optimization on top, with the LLE path forceable.

## Editing hazard

ares is written against nall's bit-precise integers (`n8`/`n15`/`n24`/`n48`)
where **every assignment silently truncates**. That masking is arithmetic, not
decoration: the accumulator is 24 bits, the program counter is 8 bits and
*wraps* to advance the instruction cache page, the multiplier is 48 bits. C has
no such types, so `cx4.c` enforces every width with a named mask (`M24`, `M15`,
…). A dropped mask compiles fine, never crashes, and computes the wrong number.

Two real bugs of exactly this kind were caught during the port: register-sourced
shift counts must be masked to 5 bits (unmasked, every count > 24 collapsed to
a no-op shift via the `s > 24` clamp), and `ROR` by 0 or 24 needed an explicit
case because the reference relies on 32-bit overflow truncation.

## Observability

`cx4_state` on the debug server (`dbgprobe.py cx4`):

| field | meaning when it looks wrong |
|---|---|
| `firmware` | `0` ⇒ `cx4.rom` missing; every `RDROM` result is zeros |
| `rdrom_hits` | `>0` with `firmware:0` ⇒ the missing blob is actively corrupting results |
| `insns` | `0` ⇒ the DSP never ran at all |
| `locked` | `1` ⇒ the core wedged its bus (same-space DMA); needs a reset |
| `ring` | DSP program starts: entry `pb`/`pc` and cache `base` |

## Oracle note

Unlike the previous model, this core is *not* derived from snes9x — so snes9x
is now a genuinely **independent** cross-check for Cx4 output, and ares itself
is the same-lineage reference. That is the right way round: the thing under
test and the thing arbitrating it no longer share an implementation.
