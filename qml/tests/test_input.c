/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <string.h>

#include "eqml_input.h"

static enum eqml_signal signals[16];
static int count;
static int
record(struct eqml_ui *ui, size_t node, enum eqml_signal s, const struct eqml_mouse *m)
{
	if (s == EQML_PRESSED) {
		assert(node == 2 && eqml_ui_pressed(ui, node));
		assert(m->x == 2 && m->y == 3);
	} else if (s == EQML_RELEASED) {
		assert(!eqml_ui_pressed(ui, node));
	} else if (s == EQML_PRESS_AND_HOLD) {
		assert(m->was_held);
	}
	assert(count < 16);
	signals[count++] = s;
	return 0;
}

static enum eqml_signal hide_on;
static unsigned int clicks, hidden_exits;

static int
hide_parent(struct eqml_ui *ui, size_t node, enum eqml_signal signal,
    const struct eqml_mouse *mouse)
{
	(void)node;
	(void)mouse;
	if (signal == EQML_CLICKED)
		clicks++;
	if (signal == EQML_EXITED)
		hidden_exits++;
	return signal == hide_on ? eqml_ui_set_visible(ui, 0, 0) : 0;
}

static void
test_hide_during_click(void)
{
	struct eqml_node nodes[] = {
		{ .kind = EQML_NODE_ITEM, .parent = -1, .opacity_area = -1 },
		{ .kind = EQML_NODE_AREA, .parent = 0, .width = 10, .height = 10,
		  .buttons = 1, .drag_target = -1, .opacity_area = -1 }
	};
	const struct eqml_program program = { nodes, 2, 0, hide_parent };
	const struct eqml_scene scene = { .width = 10, .height = 10, .program = &program };
	struct eqml_ui *ui = eqml_ui_create(&scene, NULL, NULL);

	assert(ui != NULL);
	hide_on = EQML_PRESSED;
	assert(eqml_ui_pointer(ui, EQML_DOWN, 2, 2, 1, 0, 0) == 1);
	assert(!eqml_ui_pressed(ui, 1));
	assert(eqml_ui_pointer(ui, EQML_UP, 2, 2, 1, 0, 10) >= 0);
	assert(clicks == 0 && hidden_exits == 0);
	assert(eqml_ui_set_visible(ui, 0, 1) == 0);
	hide_on = EQML_RELEASED;
	assert(eqml_ui_pointer(ui, EQML_DOWN, 2, 2, 1, 0, 100) == 1);
	assert(eqml_ui_pointer(ui, EQML_UP, 2, 2, 1, 0, 110) == 1);
	assert(!eqml_ui_visible(ui, 1) && clicks == 1 && hidden_exits == 0);
	eqml_ui_destroy(ui);

	nodes[1].hover = 1;
	ui = eqml_ui_create(&scene, NULL, NULL);
	assert(ui != NULL);
	hide_on = EQML_ENTERED;
	assert(eqml_ui_pointer(ui, EQML_MOVE, 2, 2, 0, 0, 200) == 1);
	assert(eqml_ui_tick(ui, 201) == 1);
	assert(!eqml_ui_visible(ui, 1) && hidden_exits == 1);
	eqml_ui_destroy(ui);
}

int
main(void)
{
	struct eqml_node nodes[] = {
		{ .kind=EQML_NODE_RECT, .parent=-1, .width=60, .height=60, .opacity_area=-1 },
		{ .kind=EQML_NODE_RECT, .parent=0, .x=10, .y=10, .width=20, .height=20,
		  .opacity_area=2, .pressed_transparency=102 },
		{ .kind=EQML_NODE_AREA, .parent=1, .width=20, .height=20, .buttons=1,
		  .drag_target=-1, .opacity_area=-1,
		  .signals=(1U << EQML_DOUBLE_CLICKED) | (1U << EQML_PRESS_AND_HOLD) },
		{ .kind=EQML_NODE_TEXT, .parent=0, .bottom_edge=EQML_EDGE_TOP, .bottom_target=4,
		  .pixel_size=12, .text_slot=0, .anchors=EQML_HCENTER, .opacity_area=-1 },
		{ .kind=EQML_NODE_TEXT, .parent=0, .bottom_edge=EQML_EDGE_BOTTOM, .bottom_target=0,
		  .pixel_size=10, .text_slot=-1, .opacity_area=-1 }
	};
	const struct eqml_program pg = { nodes, 5, 1, record };
	struct eqml_scene scene = { .width=60, .height=60, .program=&pg };
	struct eqml_ui *ui = eqml_ui_create(&scene, NULL, NULL);
	const struct eqml_scene *frame;
	struct eqml_point at;

	test_hide_during_click();
	assert(ui != NULL);
	assert(eqml_ui_set(ui, 0, "one") == 0 && count == 1 && signals[0] == EQML_TEXT_CHANGED);
	assert(eqml_ui_set(ui, 0, "one") == 0 && count == 1);
	assert(eqml_ui_set(ui, 1, "outside") == -1 && errno == EINVAL);
	assert(eqml_ui_set(ui, 0, "\001") == -1 && errno == EILSEQ);
	frame = eqml_ui_frame(ui);
	assert(frame->items[2].data.text.y == 38 && frame->items[2].data.text.x == 20);
	at = eqml_ui_map(ui, 2, 0, 2, 3);
	assert(at.x == 12 && at.y == 13);
	assert(eqml_ui_pointer(ui, EQML_DOWN, 12, 13, 2, 0, 0) == 0);
	count = 0;
	assert(eqml_ui_pointer(ui, EQML_DOWN, 12, 13, 1, 0, 1) == 1);
	assert(signals[0] == EQML_ENTERED && signals[1] == EQML_PRESSED);
	assert(eqml_ui_frame(ui)->items[1].transparency == 102);
	assert(eqml_ui_pointer(ui, EQML_MOVE, -5, -5, 0, 0, 4) == 1);
	assert(eqml_ui_pressed(ui, 2) && !eqml_ui_contains_press(ui, 2));
	assert(eqml_ui_frame(ui)->items[1].transparency == 0);
	assert(eqml_ui_pointer(ui, EQML_MOVE, 12, 13, 0, 0, 6) == 1);
	assert(eqml_ui_timeout(ui, 800) == 1 && eqml_ui_tick(ui, 800) == 0);
	assert(eqml_ui_tick(ui, 801) == 1 && eqml_ui_tick(ui, 802) == 0);
	assert(signals[count-1] == EQML_PRESS_AND_HOLD);
	assert(eqml_ui_pointer(ui, EQML_UP, 12, 13, 1, 0, 805) == 1);
	assert(signals[count-2] == EQML_RELEASED && signals[count-1] == EQML_EXITED);
	count = 0;
	assert(eqml_ui_pointer(ui, EQML_DOWN, 12, 13, 1, 0, 1000) == 1);
	assert(eqml_ui_pointer(ui, EQML_UP, 12, 13, 1, 0, 1020) == 1);
	assert(signals[count-2] == EQML_CLICKED);
	assert(eqml_ui_pointer(ui, EQML_DOWN, 12, 13, 1, 0, 1090) == 1);
	assert(signals[count-1] == EQML_DOUBLE_CLICKED);
	assert(eqml_ui_pointer(ui, EQML_CANCEL, 12, 13, 0, 0, 1100) == 1);
	assert(eqml_ui_timeout(ui, 5000) == -1 && !eqml_ui_pressed(ui, 2));
	count = 0;
	assert(eqml_ui_pointer(ui, EQML_DOWN, 12, 13, 1, 0, 6000) == 1);
	assert(eqml_ui_pointer(ui, EQML_MOVE, 50, 50, 0, 0, 6100) == 1);
	assert(eqml_ui_tick(ui, 6800) == 0 && eqml_ui_timeout(ui, 6800) == -1);
	assert(eqml_ui_pointer(ui, EQML_MOVE, 12, 13, 0, 0, 6900) == 1);
	assert(eqml_ui_pointer(ui, EQML_UP, 12, 13, 1, 0, 6901) == 1);
	assert(signals[count-2] == EQML_CLICKED);
	eqml_ui_destroy(ui);
	nodes[4].bottom_target = 3;
	assert(eqml_ui_create(&scene, NULL, NULL) == NULL && errno == EINVAL);
	return 0;
}
