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
