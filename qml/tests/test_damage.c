/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "eqml_input.h"

enum { WIDTH = 64, HEIGHT = 40, STRIDE = WIDTH + 3 };
struct surface {
	uint16_t pixels[HEIGHT][STRIDE];
	struct eqml_region region;
	unsigned calls, rows;
	int fail_y;
};

static int
row(void *cookie, uint16_t y, const uint16_t *pixels, uint16_t width)
{
	struct surface *s = cookie;

	assert(width == s->region.width && y >= s->region.y &&
	    y < s->region.y + s->region.height);
	if (y == s->fail_y) {
		errno = EIO;
		return -1;
	}
	memcpy(s->pixels[y] + s->region.x, pixels, width * sizeof(*pixels));
	s->rows++;
	return 0;
}

static int
present(void *cookie, const struct eqml_scene *frame, const struct eqml_region *region)
{
	struct surface *s = cookie;
	uint16_t line[WIDTH];

	s->region = *region;
	s->calls++;
	s->rows = 0;
	return eqml_render_region(frame, NULL, region, line, WIDTH, row, s);
}

static void
check(struct eqml_ui *ui, struct surface *s)
{
	const struct eqml_scene *frame;
	struct surface expected = { .region = {0, 0, WIDTH, HEIGHT}, .fail_y = -1 };
	uint16_t line[WIDTH];

	assert(eqml_ui_present(ui, present, s) == 0);
	frame = eqml_ui_frame(ui);
	assert(eqml_render(frame, NULL, line, WIDTH, row, &expected) == 0);
	assert(memcmp(s->pixels, expected.pixels, sizeof(s->pixels)) == 0);
}

static void
test_visibility(void)
{
	const struct eqml_node nodes[] = {
		{ .kind = EQML_NODE_ITEM, .parent = -1, .width = WIDTH,
		  .height = HEIGHT, .opacity_area = -1 },
		{ .kind = EQML_NODE_RECT, .parent = 0, .x = 1, .y = 2,
		  .width = 5, .height = 4, .color = 0xf800, .opacity_area = -1 },
		{ .kind = EQML_NODE_ITEM, .parent = 0, .x = 3, .y = 2,
		  .transparency = 128, .opacity_area = -1 },
		{ .kind = EQML_NODE_RECT, .parent = 2, .width = 5, .height = 4,
		  .color = 0x001f, .opacity_area = -1, .hidden = 1 },
		{ .kind = EQML_NODE_AREA, .parent = 2, .width = 5, .height = 4,
		  .transparency = 255, .buttons = 1, .opacity_area = -1,
		  .drag_target = -1 }
	};
	const struct eqml_program program = { nodes, 5, 0, NULL };
	const struct eqml_scene scene = { .width = WIDTH, .height = HEIGHT,
	    .program = &program };
	struct eqml_ui *ui = eqml_ui_create(&scene, NULL, NULL);
	struct surface s = { .fail_y = -1 };
	unsigned int calls;

	assert(ui != NULL);
	check(ui, &s);
	assert(eqml_ui_frame(ui)->item_count == 2);
	assert(!eqml_ui_visible(ui, 3) && s.pixels[2][4] == 0xf800);
	assert(eqml_ui_set_visible(ui, 3, 1) == 0);
	check(ui, &s);
	assert(s.region.x == 3 && s.region.y == 2 && s.region.width == 5 && s.rows == 4);
	assert(eqml_ui_frame(ui)->items[1].transparency == 128);
	assert(s.pixels[2][4] != 0xf800 && s.pixels[2][4] != 0x001f);
	assert(eqml_ui_pointer(ui, EQML_DOWN, 4, 3, 1, 0, 0) == 1);
	assert(eqml_ui_pressed(ui, 4));
	assert(eqml_ui_set_visible(ui, 2, 0) == 0);
	check(ui, &s);
	assert(!eqml_ui_pressed(ui, 4) && !eqml_ui_visible(ui, 3));
	assert(s.pixels[2][4] == 0xf800);
	assert(eqml_ui_pointer(ui, EQML_UP, 4, 3, 1, 0, 1) >= 0);
	assert(eqml_ui_pointer(ui, EQML_DOWN, 4, 3, 1, 0, 2) == 0);
	assert(eqml_ui_set_visible(ui, 3, 0) == 0);
	assert(eqml_ui_set_visible(ui, 2, 1) == 0);
	calls = s.calls;
	check(ui, &s);
	assert(s.calls == calls && !eqml_ui_visible(ui, 3));
	assert(eqml_ui_visible(ui, 4));
	assert(eqml_ui_set_visible(ui, 5, 1) == -1 && errno == EINVAL);
	eqml_ui_destroy(ui);
}

static void
test_images(void)
{
	const uint16_t colors[] = {0x001f, 0xf800, 0xffff, 0x07e0, 0xffff, 0x001f};
	const uint8_t alpha[] = {255, 0, 128, 0, 255, 255};
	const struct eqml_bitmap opaque = {3, 2, colors, 6, NULL, 0};
	const struct eqml_bitmap overlay = {3, 2, colors, 6, alpha, 6};
	struct eqml_node nodes[] = {
		{ .kind = EQML_NODE_ITEM, .parent = -1, .width = WIDTH,
		  .height = HEIGHT, .opacity_area = -1 },
		{ .kind = EQML_NODE_IMAGE, .parent = 0, .x = WIDTH - 2, .y = 0,
		  .width = 3, .height = 2, .bitmap = &opaque, .opacity_area = -1 },
		{ .kind = EQML_NODE_IMAGE, .parent = 0, .x = -1, .y = 1,
		  .width = 3, .height = 2, .bitmap = &overlay,
		  .transparency = 127, .opacity_area = -1 }
	};
	const struct eqml_program program = { nodes, 3, 0, NULL };
	const struct eqml_scene scene = { .width = WIDTH, .height = HEIGHT,
	    .program = &program };
	struct eqml_ui *ui = eqml_ui_create(&scene, NULL, NULL);
	struct surface s = { .fail_y = -1 };
	struct eqml_bitmap bad = overlay;

	assert(ui != NULL);
	check(ui, &s);
	assert(s.pixels[0][WIDTH - 2] == 0x001f && s.pixels[0][WIDTH - 1] == 0xf800);
	assert(s.pixels[1][0] == 0 && s.pixels[1][1] == 0x4208);
	assert(s.pixels[2][0] == 0x8410 && s.pixels[2][1] == 0x0010);
	assert(eqml_ui_set_coordinate(ui, 2, EQML_AXIS_X, 5, 0) == 0);
	check(ui, &s);
	assert(s.region.x == 0 && s.region.width == 8 && s.rows == 2);
	assert(s.pixels[2][0] == 0 && s.pixels[2][6] == 0x8410);
	assert(eqml_ui_set_visible(ui, 2, 0) == 0);
	check(ui, &s);
	assert(s.region.x == 5 && s.region.width == 3 && s.pixels[2][6] == 0);
	eqml_ui_destroy(ui);

	nodes[2].bitmap = &bad;
	bad.alpha_count--;
	assert(eqml_ui_create(&scene, NULL, NULL) == NULL && errno == EINVAL);
	bad = overlay;
	bad.pixel_count--;
	assert(eqml_ui_create(&scene, NULL, NULL) == NULL && errno == EINVAL);
	nodes[2].bitmap = &overlay;
	nodes[2].width--;
	assert(eqml_ui_create(&scene, NULL, NULL) == NULL && errno == EINVAL);
}

int
main(void)
{
	static const struct eqml_node nodes[] = {
		{ .kind = EQML_NODE_RECT, .parent = -1, .width = WIDTH, .height = HEIGHT,
		  .color = 0xffff, .opacity_area = -1 },
		{ .kind = EQML_NODE_RECT, .parent = 0, .x = 4, .y = 6, .width = 8,
		  .height = 9, .color = 0xf800, .opacity_area = -1 },
		{ .kind = EQML_NODE_TEXT, .parent = 1, .x = 5, .y = 1, .width = 1,
		  .height = 1, .pixel_size = 14, .text = "AB", .text_slot = 0, .opacity_area = -1 },
		{ .kind = EQML_NODE_RECT, .parent = 0, .x = 9, .y = 9, .width = 10,
		  .height = 8, .color = 0x001f, .opacity_area = 4, .pressed_transparency = 128 },
		{ .kind = EQML_NODE_AREA, .parent = 3, .width = 10, .height = 8,
		  .buttons = 1, .drag_target = -1, .opacity_area = -1 }
	};
	static const struct eqml_program program = { nodes, 5, 1, NULL };
	static const struct eqml_scene scene = { .width = WIDTH, .height = HEIGHT, .program = &program };
	struct eqml_ui *ui = eqml_ui_create(&scene, NULL, NULL);
	struct surface s = { .fail_y = -1 };
	unsigned calls;

	test_visibility();
	test_images();
	assert(ui != NULL);
	check(ui, &s);
	assert(s.calls == 1 && s.region.width == WIDTH && s.rows == HEIGHT);
	check(ui, &s);
	assert(s.calls == 1);
	assert(eqml_ui_set(ui, 0, "\001") == -1 && errno == EILSEQ);
	check(ui, &s);
	assert(s.calls == 1);

	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 7, 0) == 0);
	check(ui, &s);
	assert(s.region.x == 4 && s.region.y == 6 && s.region.width == 23 && s.rows == 15);
	assert(eqml_ui_pointer(ui, EQML_DOWN, 11, 10, 1, 0, 0) == 1);
	check(ui, &s);
	assert(s.region.x == 9 && s.region.y == 9 && s.region.width == 10 && s.rows == 8);

	/* Several updates and frame inspections, followed by a partial write failure. */
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 40, 0) == 0);
	assert(eqml_ui_frame(ui) != NULL);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 15, 0) == 0);
	s.fail_y = 10;
	assert(eqml_ui_present(ui, present, &s) == -1 && errno == EIO && s.rows == 4);
	s.fail_y = -1;
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_Y, -6, 0) == 0);
	check(ui, &s);
	assert(s.region.x == 7 && s.region.y == 0 && s.region.width == 53 && s.rows == 21);
	assert(eqml_ui_pointer(ui, EQML_CANCEL, 0, 0, 0, 0, 1) == 1);
	check(ui, &s);
	assert(eqml_ui_set(ui, 0, "Longer text") == 0);
	check(ui, &s);
	assert(s.region.width == WIDTH && s.rows == HEIGHT);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, INT32_MAX, 0) == 0);
	check(ui, &s);
	calls = s.calls;
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, INT32_MIN, 0) == 0);
	check(ui, &s);
	assert(s.calls == calls);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, -5, 0) == 0);
	check(ui, &s);
	assert(s.region.x == 0 && s.region.width == WIDTH);

	uint16_t line[WIDTH];
	struct eqml_region bad = { WIDTH - 1, 0, 2, 1 };
	assert(eqml_render_region(eqml_ui_frame(ui), NULL, &bad, line, WIDTH, row, &s) == -1);
	assert(errno == EINVAL);
	eqml_ui_destroy(ui);
	return 0;
}
