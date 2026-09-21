# Opt-in HD Mode 7

HD Mode 7 samples the original tilemap at fractional screen coordinates. It
reveals texture detail lost when the PPU samples just once per screen pixel;
it does not replace the artwork. This is a presentation enhancement, separate
from CPU execution, DSP-1 HLE, widescreen and temporal interpolation.

The implementation is original project code based on our existing Mode 7
register interpretation and affine mathematics. The feature described by
[bsnes-hd](https://github.com/DerKoun/bsnes-hd#settings) is a behavioral reference.
No bsnes/bsnes-hd implementation was imported or translated. Those projects
use GPLv3 licenses, while this framework's original code uses PolyForm
Noncommercial; a direct code port would require a different licensing solution.

## Shared PPU integration

The normal frame remains available unchanged. Bind a separate, non-overlapping
host-owned XRGB8888 surface before scanout:

```c
unsigned width = 256 + 2 * ppu->extraLeftRight;
unsigned height = 224, scale = 2;
size_t pitch = (size_t)width * scale * sizeof(uint32_t);
size_t capacity = pitch * height * scale;
uint32_t *hd = malloc(capacity);
if (!hd || !PpuBindMode7HdSurface(ppu, hd, capacity, pitch,
                                  width, height, scale)) {
  free(hd);
  hd = NULL;
}
/* Render normally with PpuBeginDrawing / ppu_runLine. Present hd at
 * width*scale by height*scale, with the same display aspect ratio. */
/* Before freeing the surface: */
PpuBindMode7HdSurface(ppu, NULL, 0, 0, 0, 0, 0);
free(hd);
```

The API accepts scale 1..4, native heights 1..240, padded row strides and
native widths 256..448. It validates capacity and alignment. The host must
rebind when changing the native width. A mismatched width clears the bound
rows instead of reading beyond the native image. Bindings survive reset and
are excluded from savestates. A NULL binding is the default and adds no HD
rendering or allocation. Scale 1 copies the native image exactly.

Mode 7 uses the live scanline's matrix, picture memory, palette, visibility,
windows, main/subscreen selection, sprite priorities, colour math and brightness.
Sprites remain on the native grid. Fractional transparent background samples
can reveal sprites even where the native background sample was opaque.

Other modes are nearest-neighbour scaled. Mosaic, EXTBG, direct colour,
interlace, pseudo-hires, active host overlays and custom widescreen line
enhancers also retain the native composition. This is an explicit fallback,
not a claim of HD coverage for every PPU combination. No new imagery is
invented for unpopulated widescreen margins.

## Custom game renderers

`runner/src/snes/mode7_hd.h` contains a renderer-independent transform and
sampler. It retains signed scroll/centre interpretation, flips, wrapping and
out-of-map fill while removing intermediate hardware truncation. The generic
PPU uses each scanline's own affine matrix; it does not infer perspective or
smooth across raster boundaries.

F-Zero's integration additionally interpolates transforms between adjacent
scanlines of the same camera band, using its existing published frame data.
It retains that game's course-streaming, sprites, HUD layout and independent
presentation FPS. Other titles still need their own frontend surface binding
or custom renderer integration and game validation.

## Validation

`tests/ppu/ppu_mode7_hd_test.c` exercises analytic detail in both dimensions,
disabled/native output and state isolation, guarded sizes/strides, fallback
modes, transparency behind sprites, raster changes, host picture memory and
512 combinations of colour math/windows/priority/overflow. It runs in
`tests/ppu/run.ps1`. The composition regression's disabled-path digest matches
the original renderer (`436319d369c4a1e3` at base `74be148`).

Rendering work grows with scale squared. This implementation uses the CPU;
profile the selected game and host before choosing 4x or a high presentation
rate. Resolution does not change simulation speed.
