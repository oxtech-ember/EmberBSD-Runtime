/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef EQML_H
#define EQML_H

#include <stddef.h>
#include <stdint.h>

#define EQML_SCENE_ABI 5

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
	uint8_t glyph_width; /* zero selects the square bitmap cell */
};

enum eqml_item_kind {
	EQML_TEXT,
	EQML_RECT,
	EQML_IMAGE
};

struct eqml_rect {
	int32_t x;
	int32_t y;
	uint16_t width;
	uint16_t height;
	uint16_t color;
};

/* Packed, row-major RGB565 and optional straight (0..255) alpha.
 * Counts describe the backing arrays in elements. The scene borrows both. */
struct eqml_bitmap {
	uint16_t width, height;
	const uint16_t *pixels;
	size_t pixel_count;
	const uint8_t *alpha;
	size_t alpha_count;
};

struct eqml_image {
	int32_t x, y;
	const struct eqml_bitmap *bitmap;
};

struct eqml_item {
	enum eqml_item_kind kind;
	uint8_t transparency; /* 0 = opaque, 255 = invisible */
	union {
		struct eqml_rect rect;
		struct eqml_text text;
		struct eqml_image image;
	} data;
};

/* Return nonzero for an image whose storage covers its entire extent. */
static inline int
eqml_bitmap_valid(const struct eqml_bitmap *bm)
{
	size_t count;

	if (bm == NULL || bm->width == 0 || bm->height == 0 || bm->pixels == NULL)
		return 0;
	count = (size_t)bm->width * bm->height;
	return count <= SIZE_MAX / sizeof(*bm->pixels) &&
	    bm->pixel_count == count &&
	    bm->alpha_count == (bm->alpha == NULL ? 0 : count);
}

struct eqml_program;
struct eqml_region { uint16_t x, y, width, height; };

struct eqml_scene {
	uint16_t width;
	uint16_t height;
	uint16_t background;
	const struct eqml_item *items;
	size_t item_count;
	const struct eqml_int *properties;
	size_t property_count;
	const struct eqml_program *program;
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

/* The callback receives the scene y and region.width pixels at region.x.
 * The scratch line remains full-width. */
int eqml_render_region(const struct eqml_scene *, const int32_t *values,
    const struct eqml_region *, uint16_t *line, size_t line_pixels,
    eqml_write_row_fn, void *);

#endif /* EQML_H */
