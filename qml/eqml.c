/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "eqml.h"
#include "eqml_font8x8.h"

#define EQML_MAX_TEXT_BYTES 4096

struct text_run {
	const char *prefix;
	const char *suffix;
	char number[11];
	size_t prefix_length;
	size_t number_length;
	size_t suffix_length;
	size_t length;
};


static int
string_length(const char *string, size_t *length)
{
	size_t i;

	if (string == NULL) {
		*length = 0;
		return 0;
	}
	for (i = 0; i <= EQML_MAX_TEXT_BYTES; i++) {
		unsigned char c = (unsigned char)string[i];

		if (c == '\0') {
			*length = i;
			return 0;
		}
		if (c < 0x20 || c > 0x7e) {
			errno = EILSEQ;
			return -1;
		}
	}
	errno = E2BIG;
	return -1;
}

static void
format_integer(int32_t value, char number[11], size_t *length)
{
	uint32_t magnitude;
	size_t used = 0;
	int negative = value < 0;

	/* Widen before negation so INT32_MIN is representable. */
	magnitude = negative ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
	do {
		number[used++] = (char)('0' + magnitude % 10);
		magnitude /= 10;
	} while (magnitude != 0);
	if (negative)
		number[used++] = '-';
	*length = used;
}

static int
prepare_run(const struct eqml_scene *scene, const struct eqml_text *text,
    const int32_t *values, struct text_run *run)
{
	size_t i;

	memset(run, 0, sizeof(*run));
	run->prefix = text->prefix;
	if (string_length(text->prefix, &run->prefix_length) == -1)
		return -1;
	if (text->binding == -1) {
		run->length = run->prefix_length;
		return 0;
	}
	if (text->binding < 0 || (size_t)text->binding >= scene->property_count ||
	    values == NULL) {
		errno = EINVAL;
		return -1;
	}
	run->suffix = text->suffix;
	if (string_length(text->suffix, &run->suffix_length) == -1)
		return -1;
	format_integer(values[text->binding], run->number, &run->number_length);
	if (run->prefix_length > SIZE_MAX - run->number_length ||
	    run->prefix_length + run->number_length >
	    SIZE_MAX - run->suffix_length) {
		errno = EOVERFLOW;
		return -1;
	}
	run->length = run->prefix_length + run->number_length +
	    run->suffix_length;
	for (i = 0; i < run->number_length / 2; i++) {
		char c = run->number[i];

		run->number[i] = run->number[run->number_length - i - 1];
		run->number[run->number_length - i - 1] = c;
	}
	return 0;
}

static uint8_t
run_character(const struct text_run *run, size_t index)
{
	if (index < run->prefix_length)
		return (uint8_t)run->prefix[index];
	index -= run->prefix_length;
	if (index < run->number_length)
		return (uint8_t)run->number[index];
	index -= run->number_length;
	return (uint8_t)run->suffix[index];
}

static int
validate_scene(const struct eqml_scene *scene, const int32_t *values,
	const uint16_t *line, size_t line_pixels, eqml_write_row_fn write_row)
{
	size_t i;

	errno = EINVAL;
	if (scene == NULL || line == NULL || write_row == NULL ||
	    scene->width == 0 || scene->height == 0 || scene->program != NULL ||
	    line_pixels < scene->width ||
	    (scene->item_count != 0 && scene->items == NULL) ||
	    (scene->property_count != 0 &&
	    (scene->properties == NULL || values == NULL))) {
		errno = EINVAL;
		return -1;
	}
	for (i = 0; i < scene->item_count; i++) {
		const struct eqml_item *item = &scene->items[i];
		const struct eqml_text *text;
		struct text_run run;

		if (item->kind == EQML_RECT)
			continue;
		if (item->kind != EQML_TEXT) {
			errno = EINVAL;
			return -1;
		}
		text = &item->data.text;
		if (text->pixel_size < 7 || text->pixel_size > 64 || text->glyph_width > 64 ||
		    (text->anchors & ~(EQML_HCENTER | EQML_VCENTER)) != 0) {
			errno = EINVAL;
			return -1;
		}
		if (prepare_run(scene, text, values, &run) == -1)
			return -1;
	}
	return 0;
}

static int64_t
text_width(const struct text_run *run, const struct eqml_text *text)
{
	int64_t cell = text->glyph_width ? text->glyph_width : text->pixel_size;
	int64_t spacing = (cell + 7) / 8;

	if (run->length == 0)
		return 0;
	return (int64_t)run->length * (cell + spacing) - spacing;
}

static int64_t
text_top(const struct eqml_text *text)
{
	int64_t top = text->y;

	if ((text->anchors & EQML_VCENTER) != 0)
		top += ((int64_t)text->parent_height - text->pixel_size) / 2;
	return top;
}

static int64_t
text_left(const struct eqml_text *text, const struct text_run *run)
{
	int64_t left = text->x;

	if ((text->anchors & EQML_HCENTER) != 0)
		left += ((int64_t)text->parent_width - text_width(run, text)) / 2;
	return left;
}

static uint16_t
blend565(uint16_t dst, uint16_t src, unsigned int transparency)
{
	unsigned int a = 255 - transparency;
	unsigned int r, g, b;

	if (transparency == 0)
		return src;
	r = ((src >> 11) * a + (dst >> 11) * transparency + 127) / 255;
	g = (((src >> 5) & 63U) * a + ((dst >> 5) & 63U) * transparency + 127) / 255;
	b = ((src & 31U) * a + (dst & 31U) * transparency + 127) / 255;
	return (uint16_t)((r << 11) | (g << 5) | b);
}

static void
draw_text_row(const struct eqml_region *region, const struct eqml_text *text,
	const struct text_run *run, int64_t left, int64_t top, uint16_t y,
	uint16_t *line, uint8_t transparency)
{
	int64_t local_y = (int64_t)y - top;
	int64_t advance, spacing;
	size_t character;
	unsigned int pixel_size = text->pixel_size;
	unsigned int cell = text->glyph_width ? text->glyph_width : pixel_size;

	if (local_y < 0 || local_y >= pixel_size || run->length == 0)
		return;
	spacing = (cell + 7) / 8;
	advance = cell + spacing;
	for (character = 0; character < run->length; character++) {
		uint8_t glyph = run_character(run, character);
		unsigned int x, source_y, source_y_end;

		if (pixel_size == 7) {
			source_y = (unsigned int)local_y * 8 / 7;
			source_y_end = ((unsigned int)local_y + 1) * 8 / 7;
		} else {
			source_y = (unsigned int)((local_y * 2 + 1) * 8 /
			    ((int64_t)pixel_size * 2));
			source_y_end = source_y + 1;
		}
		for (x = 0; x < cell; x++) {
			int64_t target_x = left +
			    (int64_t)character * advance + x;
			unsigned int source_x, source_y_index, source_x_end;
			int set = 0;

			if (target_x < region->x || target_x >= region->x + region->width)
				continue;
			if (cell < 8) {
				source_x = x * 8 / cell;
				source_x_end = (x + 1) * 8 / cell;
			} else {
				source_x = (x * 2 + 1) * 8 / (cell * 2);
				source_x_end = source_x + 1;
			}
			for (source_y_index = source_y;
			    source_y_index < source_y_end; source_y_index++) {
				unsigned int source_x_index;

				for (source_x_index = source_x;
				    source_x_index < source_x_end; source_x_index++)
					if ((eqml_font8x8[glyph - 0x20][source_y_index] &
					    (1u << source_x_index)) != 0)
						set = 1;
			}
			if (set)
				line[target_x] = blend565(line[target_x],
				    text->color, transparency);
		}
	}
}

int
eqml_render_region(const struct eqml_scene *scene, const int32_t *values,
    const struct eqml_region *region,
    uint16_t *line, size_t line_pixels, eqml_write_row_fn write_row,
    void *cookie)
{
	uint16_t y;

	if (validate_scene(scene, values, line, line_pixels, write_row) == -1)
		return -1;
	if (region == NULL || region->x + region->width > scene->width ||
	    region->y + region->height > scene->height) {
		errno = EINVAL;
		return -1;
	}
	if (region->width == 0 || region->height == 0)
		return 0;
	for (y = region->y; y < region->y + region->height; y++) {
		size_t i;

		for (i = region->x; i < (size_t)region->x + region->width; i++)
			line[i] = scene->background;
		for (i = 0; i < scene->item_count; i++) {
			const struct eqml_item *item = &scene->items[i];

			if (item->kind == EQML_RECT) {
				const struct eqml_rect *rect = &item->data.rect;
				int64_t left = rect->x;
				int64_t top = rect->y;
				int64_t right = left + rect->width;
				int64_t bottom = top + rect->height;
				int64_t x;

				if (rect->width == 0 || rect->height == 0 ||
				    (int64_t)y < top || (int64_t)y >= bottom)
					continue;
				if (left < region->x)
					left = region->x;
				if (right > region->x + region->width)
					right = region->x + region->width;
				for (x = left; x < right; x++)
					line[x] = blend565(line[x], rect->color,
					    item->transparency);
				continue;
			}
			if (item->kind == EQML_TEXT) {
				const struct eqml_text *text = &item->data.text;
				struct text_run run;
				int64_t top = text_top(text);

				if ((int64_t)y < top ||
				    (int64_t)y >= top + text->pixel_size)
					continue;
				/* Format only on rows touched by this glyph. */
				if (prepare_run(scene, text, values, &run) == -1)
					return -1;
				draw_text_row(region, text, &run,
				    text_left(text, &run), top, y, line, item->transparency);
			}
		}
		errno = 0;
		if (write_row(cookie, y, line + region->x, region->width) == -1) {
			if (errno == 0)
				errno = EIO;
			return -1;
		}
	}
	return 0;
}

int
eqml_render(const struct eqml_scene *scene, const int32_t *values,
    uint16_t *line, size_t line_pixels, eqml_write_row_fn write_row,
    void *cookie)
{
	if (scene == NULL) {
		errno = EINVAL;
		return -1;
	}
	const struct eqml_region full = { 0, 0, scene->width, scene->height };
	return eqml_render_region(scene, values, &full, line, line_pixels, write_row, cookie);
}
