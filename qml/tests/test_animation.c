/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include "eqml_input.h"

static int held, clicks, entered, exited;

static int
handle(struct eqml_ui *ui, size_t node, enum eqml_signal signal,
    const struct eqml_mouse *mouse)
{
	if (signal == EQML_ENTERED)
		++entered;
	if (signal == EQML_EXITED)
		++exited;
	if (signal == EQML_PRESS_AND_HOLD)
		++held;
	if (signal == EQML_CLICKED) {
		assert(node == 2 && mouse != NULL);
		++clicks;
		return eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X,
		    eqml_ui_coordinate(ui, 1, EQML_AXIS_X) + 50, 1000);
	}
	return 0;
}

static const struct eqml_node nodes[] = {
	{ .kind = EQML_NODE_RECT, .parent = -1, .width = 200, .height = 80,
	  .opacity_area = -1 },
	{ .kind = EQML_NODE_RECT, .parent = 0, .width = 20, .height = 20,
	  .opacity_area = -1 },
	{ .kind = EQML_NODE_AREA, .parent = 1, .width = 20, .height = 20,
	  .buttons = EQML_BUTTON_LEFT, .signals = 1U << EQML_PRESS_AND_HOLD, .hover = 1,
	  .drag_target = -1, .opacity_area = -1 },
	{ .kind = EQML_NODE_RECT, .parent = 1, .width = 2, .height = 2,
	  .anchors = EQML_HCENTER, .opacity_area = -1 }
};
static const struct eqml_program program = { nodes, 4, 0, handle };
static const struct eqml_scene scene = { .width = 200, .height = 80, .program = &program };

static int32_t
x(struct eqml_ui *ui)
{
	return eqml_ui_coordinate(ui, 1, EQML_AXIS_X);
}

static void
click(struct eqml_ui *ui, int32_t px, uint64_t now)
{
	assert(eqml_ui_pointer(ui, EQML_DOWN, px, 5, EQML_BUTTON_LEFT, 0, now) == 1);
	assert(eqml_ui_pointer(ui, EQML_UP, px, 5, EQML_BUTTON_LEFT, 0, now) == 1);
}

static void
stationary_hover(void)
{
	struct eqml_ui *ui = eqml_ui_create(&scene, NULL, NULL);

	assert(ui != NULL);
	entered = exited = 0;
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 2, 50) == 0);
	assert(eqml_ui_tick(ui, 50) == 1 && entered == 0 && exited == 0);
	assert(eqml_ui_pointer(ui, EQML_MOVE, 5, 5, 0, 0, 50) == 1 && entered == 1);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 50, 100) == 0);
	assert(eqml_ui_tick(ui, 150) == 1 && entered == 1 && exited == 1);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 0, 100) == 0);
	assert(eqml_ui_tick(ui, 250) == 1 && entered == 2 && exited == 1);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 50, 0) == 0);
	assert(eqml_ui_timeout(ui, 250) == 0);
	assert(eqml_ui_tick(ui, 250) == 1 && exited == 2);
	assert(eqml_ui_pointer(ui, EQML_CANCEL, 5, 5, 0, 0, 250) == 0);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 0, 100) == 0);
	assert(eqml_ui_tick(ui, 350) == 1 && entered == 2);
	click(ui, 5, 350);
	assert(entered == 3 && exited == 3);
	assert(eqml_ui_tick(ui, 1350) == 1);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 0, 100) == 0);
	assert(eqml_ui_tick(ui, 1450) == 1 && entered == 3 && exited == 3);
	eqml_ui_destroy(ui);
}

int
main(void)
{
	struct eqml_ui *ui = eqml_ui_create(&scene, NULL, NULL);
	struct eqml_ui *other = eqml_ui_create(&scene, NULL, NULL);
	const struct eqml_scene *frame;

	assert(ui != NULL && other != NULL);
	assert(eqml_ui_timeout(ui, 100) == -1);
	click(ui, 5, 100);
	assert(clicks == 1 && x(ui) == 0 && eqml_ui_timeout(ui, 100) == 16);
	assert(eqml_ui_timeout(ui, 116) == 0);
	assert(eqml_ui_tick(ui, 600) == 1 && x(ui) == 25);
	assert(eqml_ui_tick(ui, 599) == 0 && x(ui) == 25);
	frame = eqml_ui_frame(ui);
	assert(frame->items[1].data.rect.x == 25 && frame->items[2].data.rect.x == 34);
	assert(eqml_ui_map(ui, 2, 0, 0, 0).x == 25);
	assert(eqml_ui_pointer(ui, EQML_DOWN, 5, 5, EQML_BUTTON_LEFT, 0, 600) == 0);
	assert(!eqml_ui_pressed(ui, 2));
	click(ui, 30, 600);
	assert(clicks == 2 && x(ui) == 25);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_Y, 40, 500) == 0);
	assert(eqml_ui_tick(ui, 850) == 1 && x(ui) == 38);
	assert(eqml_ui_coordinate(ui, 1, EQML_AXIS_Y) == 20);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 75, 1000) == 0);
	assert(eqml_ui_tick(ui, 1600) == 1 && x(ui) == 75);
	assert(eqml_ui_coordinate(ui, 1, EQML_AXIS_Y) == 40);
	assert(eqml_ui_timeout(ui, 1600) == -1 && eqml_ui_tick(ui, 4000) == 0);
	assert(x(other) == 0 && eqml_ui_timeout(other, 4000) == -1);
	/* Two timers expire together, even after a late first sample. */
	assert(eqml_ui_pointer(ui, EQML_DOWN, 80, 45, EQML_BUTTON_LEFT, 0, 4000) == 1);
	assert(eqml_ui_set_coordinate(ui, 3, EQML_AXIS_Y, 5, 800) == 0);
	assert(eqml_ui_tick(ui, 4800) == 1 && held == 1);
	assert(eqml_ui_coordinate(ui, 3, EQML_AXIS_Y) == 5);
	assert(eqml_ui_timeout(ui, 4800) == -1);
	assert(eqml_ui_pointer(ui, EQML_CANCEL, 0, 0, 0, 0, 4800) == 1);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 76, 100) == 0);
	assert(eqml_ui_tick(ui, 4899) == 1 && x(ui) == 76);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_X, 76, 100) == 0);
	assert(eqml_ui_timeout(ui, 4899) == 1);
	assert(eqml_ui_tick(ui, 4900) == 0 && eqml_ui_timeout(ui, 4900) == -1);

	/* A moving area leaves the stationary finger before the hold expires. */
	assert(eqml_ui_pointer(other, EQML_DOWN, 5, 5, EQML_BUTTON_LEFT, 0, 50) == 1);
	assert(eqml_ui_set_coordinate(other, 1, EQML_AXIS_X, 100, 810) == 0);
	assert(eqml_ui_tick(other, 849) == 1 && !eqml_ui_contains_press(other, 2));
	assert(eqml_ui_timeout(other, 849) == 1);
	assert(eqml_ui_tick(other, 850) >= 0 && held == 1);
	assert(eqml_ui_timeout(other, 859) == 1);
	assert(eqml_ui_tick(other, 860) >= 0 && eqml_ui_timeout(other, 860) == -1);
	assert(eqml_ui_pointer(other, EQML_CANCEL, 0, 0, 0, 0, 900) == 1);

	assert(eqml_ui_set_coordinate(other, 1, EQML_AXIS_X, INT32_MIN, 0) == 0);
	assert(eqml_ui_set_coordinate(other, 1, EQML_AXIS_X, INT32_MAX, UINT32_MAX) == 0);
	assert(eqml_ui_tick(other, UINT64_C(900) + INT32_MAX) == 1 && x(other) == -1);
	assert(eqml_ui_tick(other, UINT64_C(900) + UINT32_MAX) == 1 && x(other) == INT32_MAX);
	assert(eqml_ui_set_coordinate(other, 1, EQML_AXIS_X, INT32_MIN, 100) == 0);
	assert(eqml_ui_set_coordinate(other, 1, EQML_AXIS_X, -7, 0) == 0);
	assert(x(other) == -7 && eqml_ui_timeout(other, UINT64_MAX) == -1);
	assert(eqml_ui_tick(other, UINT64_MAX - 3) == 0);
	assert(eqml_ui_set_coordinate(other, 1, EQML_AXIS_X, -5, 3) == 0);
	assert(eqml_ui_timeout(other, UINT64_MAX - 1) == 1);
	assert(eqml_ui_tick(other, UINT64_MAX) == 1 && x(other) == -5);
	assert(eqml_ui_timeout(other, UINT64_MAX) == -1);
	assert(eqml_ui_coordinate(other, 3, EQML_AXIS_X) == 9);
	assert(eqml_ui_set_coordinate(other, 3, EQML_AXIS_X, 6, 1) == -1 && errno == EINVAL);
	assert(eqml_ui_set_coordinate(other, 4, EQML_AXIS_Y, 6, 1) == -1 && errno == EINVAL);
	assert(eqml_ui_set_coordinate(ui, 1, EQML_AXIS_Y, 15, 20) == 0);
	eqml_ui_destroy(ui);
	eqml_ui_destroy(other);
	stationary_hover();
	return 0;
}
