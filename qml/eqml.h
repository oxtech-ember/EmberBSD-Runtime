/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef EQML_H
#define EQML_H

#include <stddef.h>
#include <stdint.h>

#define EQML_SCENE_ABI 2

enum {
	EQML_HCENTER = 1,
	EQML_VCENTER = 2
};

struct eqml_int {
	const char *name;
	int32_t initial;
};

struct eqml_text {
	const char *prefix;
	const char *suffix;
	int32_t binding;
	int32_t x;
	int32_t y;
	uint16_t parent_width;
	uint16_t parent_height;
	uint16_t color;
	uint8_t pixel_size;
	uint8_t anchors;
};

enum eqml_item_kind {
	EQML_TEXT,
	EQML_RECT
};

struct eqml_rect {
	int32_t x;
	int32_t y;
	uint16_t width;
	uint16_t height;
	uint16_t color;
};

struct eqml_item {
	enum eqml_item_kind kind;
	union {
		struct eqml_rect rect;
		struct eqml_text text;
	} data;
};

struct eqml_scene {
	uint16_t width;
	uint16_t height;
	uint16_t background;
	const struct eqml_item *items;
	size_t item_count;
	const struct eqml_int *properties;
	size_t property_count;
};

/* A row callback borrows pixels until it returns. Return 0 or -1 and set
 * errno on failure. The callback is called once per scene row, in order.
 */
typedef int (*eqml_write_row_fn)(void *, uint16_t, const uint16_t *,
	uint16_t);

/* Render the whole scene into caller-owned line storage. values contains one
 * mutable model value per scene property and is read-only to the renderer.
 * Return 0 on success; otherwise return -1 with errno set. line_pixels must be
 * at least scene->width. No frame-sized pixel buffer is allocated.
 */
int eqml_render(const struct eqml_scene *, const int32_t *values,
	uint16_t *line, size_t line_pixels, eqml_write_row_fn, void *);

#endif /* EQML_H */
