# EmberBSD QML scene renderer

This directory implements `EQML_SCENE_ABI 2`, the small target-side runtime
contract for generated flat Rectangle/Text scenes. The SDK compiler owns QML
parsing and emits a C translation unit defining `const struct eqml_scene
eqml_app` (or another selected public symbol). Runtime does not parse QML or
allocate the application's model values.

## Scene and model

`struct eqml_scene` contains a fixed RGB565 background, an ordered array of
`struct eqml_item`, and integer properties. Each item is `EQML_RECT` or
`EQML_TEXT`. The array is already in final paint order from the compiler; the
renderer visits it in order and later items cover earlier ones. Rectangle and
text items can be mixed.

The SDK compiler emits items with designated initializers such as:

```c
{ .kind = EQML_RECT, .data.rect = {
    .x = 12, .y = 8, .width = 80, .height = 24, .color = 0x1234
} }
```

and:

```c
{ .kind = EQML_TEXT, .data.text = {
    .prefix = "Temperature: ", .suffix = " C", .binding = 0,
    .x = 16, .y = 12, .parent_width = 80, .parent_height = 24,
    .color = 0xffff, .pixel_size = 14, .anchors = EQML_HCENTER
} }
```

Coordinates are signed absolute pixel coordinates. Rectangle extents use
unsigned width and height; pixels outside the scene are clipped, and a
zero-width or zero-height rectangle is a valid no-op. The compiler flattens
nested item origins and QML paint order. For each centered text axis, `x` or
`y` is the absolute origin of its anchor parent and the renderer adds the
center offset calculated from `parent_width` or `parent_height`. On an
uncentered axis the coordinate is the absolute top-left position. Parent
extents may be zero.

The application allocates one `int32_t` value per property and initializes it
from each property's `.initial` value. Pass that array as a `const int32_t *`
to render; to update a bound label, change the application value and render
again. A text item with `binding == -1` displays its `prefix` as a literal
(`suffix` is ignored). A nonnegative binding is a property index; the renderer
displays `prefix`, the signed base-10 value, then `suffix`. `INT32_MIN` is
supported.

The renderer supports one-line printable ASCII text (bytes 0x20 through 0x7e).
Each literal part (`prefix` or `suffix`) is limited to 4096 bytes. It provides
no alpha blending and uses a solid scene background. Items paint in array
order, so later items cover earlier ones. Pixels outside the scene are clipped.
There is no parser, scene graph, timer, input system or implicit heap allocation.

Text `pixel_size` is the square glyph height and width, from 7 through 64
pixels. The 8×8 source bitmap is scaled with center-sampled nearest-neighbour
mapping for sizes 8–64. At size 7 each output pixel ORs the source pixel(s) in
its proportional 8-to-7 interval, preserving narrow marks and all source
edges. Character spacing is `ceil(pixel_size / 8)` pixels. On an uncentered
axis, `x` or `y` is the absolute top-left position. On a centered axis it is
the absolute parent origin plus the compiler-flattened offset, and the renderer
adds the center offset based on the corresponding parent extent. Center
positions use C integer division (truncation toward zero). `EQML_HCENTER` and
`EQML_VCENTER` may be combined.

`eqml_render()` validates the scene and item kinds before its first callback
and returns 0 on success or -1 with `errno` set on failure. It calls the
supplied row writer once per scene row in increasing order. The callback
borrows the RGB565 row only until returning; it returns 0 or -1 with `errno`
set. The renderer uses a caller-owned line buffer and fixed-size per-text
formatting state. It formats bound text only for rows intersecting that item
and does not allocate a framebuffer or heap memory.

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
failure propagation, all printable glyphs, ordered mixed Rectangle/Text
painting, parent-relative text centering, rectangle clipping and zero-size
rectangles. Compile the EmberBSD backend
with the target compiler and the matching target sysroot, for example:

```sh
clang --target=riscv32--netbsd --sysroot=/path/to/target-sysroot \
  -march=rv32imac -mabi=ilp32 -std=c11 -Wall -Wextra -Werror \
  -Iqml -c qml/eqml_wsdisplay.c -o eqml_wsdisplay.o
```

Host fixtures exercise the portable renderer, not a physical framebuffer.
