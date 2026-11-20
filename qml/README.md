# EmberBSD QML scene renderer

This directory implements `EQML_SCENE_ABI 1`, the small target-side runtime
contract for generated flat Rectangle/Text scenes. The SDK compiler owns QML
parsing and emits a C translation unit defining `const struct eqml_scene
eqml_app` (or another selected public symbol). Runtime does not parse QML or
allocate the application's model values.

## Scene and model

`struct eqml_scene` contains a fixed RGB565 background, text items and integer
properties. The application allocates one `int32_t` value per property and
initializes it from each property's `.initial` value. Pass that array as a
`const int32_t *` to render; to update a bound label, change the application
value and render again. A text item with `binding == -1` displays its `prefix`
as a literal (`suffix` is ignored). A nonnegative binding is a property index;
the renderer displays `prefix`, the signed base-10 value, then `suffix`.
`INT32_MIN` is supported.

The renderer supports one-line printable ASCII text (bytes 0x20 through 0x7e).
Each literal part (`prefix` or `suffix`) is limited to 4096 bytes. It provides
no alpha blending and uses a solid scene background. Text items paint in array
order, so later items cover earlier ones. Pixels outside the scene are clipped.
There is no parser, scene graph, timer, input system or implicit heap allocation.

Text `pixel_size` is the square glyph height and width, from 7 through 64
pixels. The 8×8 source bitmap is scaled with center-sampled nearest-neighbour
mapping for sizes 8–64. At size 7 each output pixel ORs the source pixel(s) in
its proportional 8-to-7 interval, preserving narrow marks and all source
edges. Character spacing is `ceil(pixel_size / 8)` pixels. `x`/`y` are top-left
coordinates unless the matching center anchor is set; when centered they are
signed offsets from the centered origin. Center positions use C integer
division (truncation toward zero). `EQML_HCENTER` and `EQML_VCENTER` may be
combined.

`eqml_render()` validates the scene before its first callback and returns 0 on
success or -1 with `errno` set on failure. It calls the supplied row writer
once per scene row in increasing order. The callback borrows the RGB565 row
only until returning; it returns 0 or -1 with `errno` set. The renderer uses a
caller-owned line buffer and fixed-size per-text formatting state. It formats
bound text only for rows intersecting that item and does not allocate a
framebuffer or heap memory.

## wsdisplay backend

`eqml_wsdisplay_open()` validates the device's reported geometry, depth,
RGB565 channel masks, stride and framebuffer size before changing the display
to `WSDISPLAYIO_MODE_DUMBFB`. Expected dimensions may be zero to accept the
reported size. `eqml_wsdisplay_present()` renders directly into the mapped
framebuffer one row at a time. `eqml_wsdisplay_close()` unmaps, restores the
previous display mode and closes the device. The backend is specific to the
EmberBSD/NetBSD wsdisplay ABI; the portable renderer itself uses no OS APIs.

All three public operations return 0 on success, -1 on failure and set `errno`.
They do not install signal handlers or own process lifetime; applications
choose their event loop and close the backend during normal shutdown.

## Font provenance and licenses

The 8×8 bitmap table is the printable ASCII subset of
[`font8x8_basic`](https://github.com/dhepper/font8x8/blob/master/font8x8_basic.h)
by Daniel Hepper, derived from public-domain IBM VGA bitmap font data by Marcel
Sondaar. The source identifies the font data as Public Domain. Attribution
and the source encoding are retained in [`eqml_font8x8.h`](eqml_font8x8.h).
The renderer and backend are BSD-2-Clause; see the repository [`LICENSE`](../LICENSE).

## Host fixture and target compile

Run the renderer fixture with the selected native Ports compiler:

```sh
make -C qml/tests test \
  CC=/path/to/host-pkg-llvm/bin/clang \
  OUT=/tmp/eqml-test
```

The fixture checks changed pixels for values `21` and `-8`, `INT32_MIN`,
failure propagation, and all printable glyphs. Compile the EmberBSD backend
with the target compiler and the matching target sysroot, for example:

```sh
clang --target=riscv32--netbsd --sysroot=/path/to/target-sysroot \
  -march=rv32imac -mabi=ilp32 -std=c11 -Wall -Wextra -Werror \
  -Iqml -c qml/eqml_wsdisplay.c -o eqml_wsdisplay.o
```

Host fixtures exercise the portable renderer, not a physical framebuffer.
