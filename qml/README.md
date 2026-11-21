# EmberBSD QML scene renderer

This directory implements `EQML_SCENE_ABI 5`, the small target-side runtime
contract for compiled static scenes and opt-in interactive scenes. The SDK compiler owns QML
parsing and emits a C translation unit defining `const struct eqml_scene
eqml_app` (or another selected public symbol). Runtime does not parse QML or
allocate the application's model values.

## Scene and model

`struct eqml_scene` contains a fixed RGB565 background, an ordered array of
`struct eqml_item`, and integer properties. Items can be rectangles, text,
or images. The compiler supplies them in paint order. Later items cover
earlier ones according to their opacity.

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
Each literal part (`prefix` or `suffix`) is limited to 4096 bytes. Per-item transparency blends the source into the RGB565 scanline. The scene background is solid. Items paint in array
order, so later items cover earlier ones. Pixels outside the scene are clipped.
The static rendering path requires only caller-owned scratch space.

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

### Compiled images

An `EQML_IMAGE` places an immutable bitmap at a signed scene position.
The renderer copies row-major RGB565 pixels at their native size, clipping to
the requested region. An optional byte per pixel supplies straight alpha,
from 0 (transparent) to 255 (opaque), which multiplies the item's opacity.
The descriptor carries both array lengths. Keep the arrays alive for the
scene's lifetime. Interactive `EQML_NODE_IMAGE` nodes use the same bitmap,
with matching node and bitmap dimensions, and participate in placement,
visibility and damage tracking. Asset decoding belongs to the host compiler.

## Interactive scenes

The compiler emits parent-first node descriptors and C callbacks for the
supported QML handlers. Link `eqml_input.c` when the scene has a `program`.
`eqml_ui_create()` allocates one instance, its node placements, draw list and
string slots. Nodes and strings are bounded to 256 each. Each mutable slot
holds up to 255 printable ASCII bytes. Destroy the instance with
`eqml_ui_destroy()`. It owns neither the input device nor the display.

An `EQML_NODE_ITEM` groups children and contributes geometry without painting.
Opacity multiplies through the parent chain. A fully transparent MouseArea
can still receive input. Explicit visibility is per instance.
`eqml_ui_visible()` reads effective visibility, including ancestors.
`eqml_ui_set_visible()` changes the local property. Hiding a subtree repaints
its old bounds and releases its pointer grab. Children keep their own visibility
when an ancestor is shown again.

Feed single-contact press, move, release and cancellation to `eqml_ui_pointer()`
in viewport pixels. Use a single monotonic clock in milliseconds for that
operation, `eqml_ui_timeout()` and `eqml_ui_tick()`. A timeout of -1 means no
timer is pending. A pointer or tick result of 1 requests redraw, 0 leaves
the frame as is, and -1 reports an error through `errno`. The single-contact
profile ends hover on lift. It supports bounded, smoothed drag, a 10-pixel
motion threshold, double tap within 400 ms, and an 800-ms hold. The program
selects which handlers receive those gestures. Map physical screen coordinates
to the viewport before dispatch, including while dragging off its edge.

`eqml_ui_frame()` resolves anchors and text properties and returns an
instance-owned draw list for `eqml_render(frame, NULL, ...)`. Consume it
before the next update. The RGB565 renderer is shared with static scenes.
The input instance stores geometry and labels; it allocates no pixel
framebuffer. The text uses a compact bitmap cell and the SDK translates
the supported event expressions to C at build time. This profile handles
one primary contact, fixed viewport dimensions and untransformed items.

### Animated coordinates

For a compiled linear `Behavior on x` or `Behavior on y`, a handler reads
the current local integer position with `eqml_ui_coordinate()`. It writes
the target and the declared duration in milliseconds with
`eqml_ui_set_coordinate()`. A duration of zero applies the value immediately.
An anchor controls its own axis, and a write to that axis fails with `EINVAL`.
An invalid read returns zero and sets `errno` to `EINVAL`.

Each UI instance owns at most one animation per node and axis. A new target
starts at the current position. Repeating the target preserves the finish
time. A write of the current position stops motion. Starting an animation
allocates one record. Completion, cancellation of that coordinate or UI
destruction frees it. Reading, presenting and sampling need no new storage.

The event loop calls `eqml_ui_tick(ui, now_ms)` to sample elapsed time.
Pointer dispatch also samples before hit testing. For a write made outside
a handler, first pass the current monotonic time to `eqml_ui_tick()`.
Sampling interpolates each coordinate to the nearest integer, rounds a tie
away from zero and sets the exact target at the end. The same placements
drive the renderer, coordinate mapping and pointer hit testing.
Moving a hover-enabled area under or away from a stationary pointer
delivers the corresponding enter or exit signal. Touch release and
cancellation end the pointer position used for hover. An immediate coordinate
write with a live pointer schedules this check for the next tick.
`eqml_ui_timeout()` returns the time to the earliest animation sample or
press-and-hold deadline. The animation requests samples at 16 ms intervals
and at the exact final deadline. A late sample advances directly to the
current time. The timeout returns to -1 once all pending work completes.

### Partial presentation

For a retained framebuffer at a fixed viewport, call `eqml_ui_present()`
with a synchronous presentation callback. The first call covers the whole
scene. Later calls cover the accumulated union of the old and new painted
bounds, clipped to the scene. The union includes descendant text which can
extend beyond its parent. Changes to a string invalidate the whole scene.
The UI stores one pending rectangle and reuses its existing flat draw list.

The callback can pass the borrowed frame and region to
`eqml_wsdisplay_present_region_at()` with the same viewport offset and
full-width line buffer as a full presentation. The renderer paints the
background and intersecting items in their original order, including items
that overlap a previous position. A failed callback leaves the full region
pending for retry. A successful call with no pending damage skips the
callback. Do not update or destroy the UI from its callback. When replacing
the framebuffer or changing its viewport, paint the full frame at the new
location and restore the old viewport as needed before resuming partial
presentation.

## wsdisplay backend

`eqml_wsdisplay_open()` validates the device's reported geometry, depth,
RGB565 channel masks, stride and framebuffer size before changing the display
to `WSDISPLAYIO_MODE_DUMBFB`. Expected dimensions may be zero to accept the
reported size. `eqml_wsdisplay_present()` renders directly into the mapped
framebuffer one row at a time. `eqml_wsdisplay_present_at()` also accepts a smaller
scene at a caller-selected position and preserves pixels outside that viewport. `eqml_wsdisplay_close()` unmaps, restores the
previous display mode and closes the device. The backend is specific to the
EmberBSD/NetBSD wsdisplay ABI; the portable renderer itself uses no OS APIs.

All three public operations return 0 on success, -1 on failure and set `errno`.
They do not install signal handlers or own process lifetime; applications
choose their event loop and close the backend during normal shutdown.

## wscons pointer input

`eqml_wsmouse_open()` opens the selected wsmouse unit in nonblocking mode and
negotiates the current wscons event version. Poll the file descriptor, drain
`eqml_wsmouse_read()` until it returns zero, and then process the UI timer.
X and Y reported together produce a single pointer position, including when
the caller's event buffer fills. The adapter translates absolute or relative
motion and the changed button. Event timestamps are in the same monotonic
clock domain as the UI timer. Keyboard modifiers are zero on this input path.

The native regression uses constants from the selected EmberBSD OS checkout:

```sh
"$HOST_PREFIX/bin/python3.15" qml/tests/run-wsmouse.py \
  --host-prefix "$HOST_PREFIX" --wscons-header "$OS_SOURCE/sys/dev/wscons/wsconsio.h" \
  --out "$OUT/test-wsmouse"
```

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
