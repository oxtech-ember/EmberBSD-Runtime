/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef EQML_INPUT_H
#define EQML_INPUT_H

#include "eqml.h"

#define EQML_STRING_CAP 256

enum eqml_signal {
	EQML_ENTERED, EQML_EXITED, EQML_PRESSED, EQML_RELEASED, EQML_CLICKED,
	EQML_DOUBLE_CLICKED, EQML_PRESS_AND_HOLD, EQML_TEXT_CHANGED
};

#define EQML_BUTTON_LEFT (1U << 0)
#define EQML_BUTTON_RIGHT (1U << 1)
#define EQML_BUTTON_MIDDLE (1U << 2)
#define EQML_BUTTON_BACK (1U << 3)
#define EQML_BUTTON_FORWARD (1U << 4)
#define EQML_BUTTON_TASK (1U << 5)
#define EQML_BUTTON_EXTRA4 (1U << 6)
#define EQML_BUTTON_EXTRA5 (1U << 7)
#define EQML_BUTTON_EXTRA6 (1U << 8)
#define EQML_BUTTON_EXTRA7 (1U << 9)
#define EQML_BUTTON_EXTRA8 (1U << 10)
#define EQML_BUTTON_EXTRA9 (1U << 11)
#define EQML_BUTTON_EXTRA10 (1U << 12)
#define EQML_BUTTON_EXTRA11 (1U << 13)
#define EQML_BUTTON_EXTRA12 (1U << 14)
#define EQML_BUTTON_EXTRA13 (1U << 15)
#define EQML_BUTTON_EXTRA14 (1U << 16)
#define EQML_BUTTON_EXTRA15 (1U << 17)
#define EQML_BUTTON_EXTRA16 (1U << 18)
#define EQML_BUTTON_EXTRA17 (1U << 19)
#define EQML_BUTTON_EXTRA18 (1U << 20)
#define EQML_BUTTON_EXTRA19 (1U << 21)
#define EQML_BUTTON_EXTRA20 (1U << 22)
#define EQML_BUTTON_EXTRA21 (1U << 23)
#define EQML_BUTTON_EXTRA22 (1U << 24)
#define EQML_BUTTON_EXTRA23 (1U << 25)
#define EQML_BUTTON_EXTRA24 (1U << 26)
#define EQML_BUTTON_ALL 0x07ffffffU
#define EQML_MODIFIER_SHIFT (1U << 25)

enum eqml_phase { EQML_DOWN, EQML_MOVE, EQML_UP, EQML_CANCEL };
enum eqml_axis { EQML_AXIS_X, EQML_AXIS_Y };
enum eqml_edge { EQML_EDGE_NONE, EQML_EDGE_TOP, EQML_EDGE_BOTTOM };
enum eqml_node_kind { EQML_NODE_RECT, EQML_NODE_TEXT, EQML_NODE_AREA };

struct eqml_point { int32_t x, y; };

struct eqml_mouse {
	int32_t x, y;
	uint32_t button, modifiers;
	int is_click, was_held;
};

/* Parent-first nodes in paint order. -1 denotes an absent parent,
 * text slot, opacity source or drag target. x/y are parent-relative.
 * A bottom anchor may refer forward to a sibling. String indices address
 * per-instance storage shared with the generated handler.
 */
struct eqml_node {
	enum eqml_node_kind kind;
	int parent;
	int32_t x, y;
	uint16_t width, height, color;
	uint8_t pixel_size, anchors;
	enum eqml_edge bottom_edge;
	int bottom_target;
	int32_t bottom_margin;
	const char *text;
	int text_slot, opacity_area;
	uint8_t pressed_transparency;
	uint32_t buttons, signals;
	uint8_t hover, drag_axes;
	int drag_target;
	int32_t min_x, max_x, min_y, max_y;
};

struct eqml_ui;
struct eqml_program {
	const struct eqml_node *nodes;
	size_t node_count, string_count;
	int (*handler)(struct eqml_ui *, size_t, enum eqml_signal,
	    const struct eqml_mouse *);
};

/* Immutable compiled input is borrowed for the lifetime of the instance.
 * The caller presents the borrowed flat frame before its next UI update.
 */
struct eqml_ui *eqml_ui_create(const struct eqml_scene *,
    void (*)(void *, const char *), void *);
void eqml_ui_destroy(struct eqml_ui *);
const struct eqml_scene *eqml_ui_frame(struct eqml_ui *);
int eqml_ui_pointer(struct eqml_ui *, enum eqml_phase, int32_t, int32_t,
    uint32_t, uint32_t, uint64_t);
int eqml_ui_tick(struct eqml_ui *, uint64_t);
int eqml_ui_timeout(const struct eqml_ui *, uint64_t);
int32_t eqml_ui_coordinate(const struct eqml_ui *, size_t, enum eqml_axis);
int eqml_ui_set_coordinate(struct eqml_ui *, size_t, enum eqml_axis, int32_t,
    uint32_t);
int eqml_ui_contains_press(const struct eqml_ui *, size_t);
int eqml_ui_pressed(const struct eqml_ui *, size_t);
const char *eqml_ui_string(const struct eqml_ui *, size_t);
int eqml_string_copy(char dest[EQML_STRING_CAP], const char *src);
int eqml_ui_set(struct eqml_ui *, size_t, const char *);
void eqml_ui_log(struct eqml_ui *, const char *);
struct eqml_point eqml_ui_map(const struct eqml_ui *, size_t, size_t,
    int32_t, int32_t);

#endif
