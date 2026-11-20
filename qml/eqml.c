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
	uint16_t *line, size_t line_pixels, eqml_write_row_fn write_row)
{
	size_t i;

	errno = EINVAL;
	if (scene == NULL || line == NULL || write_row == NULL ||
	    scene->width == 0 || scene->height == 0 ||
	    line_pixels < scene->width ||
	    (scene->text_count != 0 && scene->texts == NULL) ||
	    (scene->property_count != 0 &&
	    (scene->properties == NULL || values == NULL))) {
		errno = EINVAL;
		return -1;
	}
	for (i = 0; i < scene->text_count; i++) {
		const struct eqml_text *text = &scene->texts[i];
		struct text_run run;

		if (text->pixel_size < 7 || text->pixel_size > 64 ||
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
text_width(const struct text_run *run, unsigned int pixel_size)
{
	int64_t spacing = (pixel_size + 7) / 8;

	if (run->length == 0)
		return 0;
	return (int64_t)run->length * (pixel_size + spacing) - spacing;
}

static int64_t
text_top(const struct eqml_scene *scene, const struct eqml_text *text)
{
	int64_t top = text->y;

	if ((text->anchors & EQML_VCENTER) != 0)
		top += ((int64_t)scene->height - text->pixel_size) / 2;
	return top;
}

static int64_t
text_left(const struct eqml_scene *scene, const struct eqml_text *text,
	const struct text_run *run)
{
	int64_t left = text->x;

	if ((text->anchors & EQML_HCENTER) != 0)
		left += ((int64_t)scene->width - text_width(run,
		    text->pixel_size)) / 2;
	return left;
}

static void
draw_text_row(const struct eqml_scene *scene, const struct eqml_text *text,
	const struct text_run *run, int64_t left, int64_t top, uint16_t y,
	uint16_t *line)
{
	int64_t local_y = (int64_t)y - top;
	int64_t advance, spacing;
	size_t character;
	unsigned int pixel_size = text->pixel_size;

	if (local_y < 0 || local_y >= pixel_size || run->length == 0)
		return;
	spacing = (pixel_size + 7) / 8;
	advance = pixel_size + spacing;
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
		for (x = 0; x < pixel_size; x++) {
			int64_t target_x = left +
			    (int64_t)character * advance + x;
			unsigned int source_x, source_y_index, source_x_end;
			int set = 0;

			if (pixel_size == 7) {
				source_x = x * 8 / 7;
				source_x_end = (x + 1) * 8 / 7;
			} else {
				source_x = (x * 2 + 1) * 8 / (pixel_size * 2);
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
			if (set && target_x >= 0 && target_x < scene->width)
				line[target_x] = text->color;
		}
	}
}

int
eqml_render(const struct eqml_scene *scene, const int32_t *values,
    uint16_t *line, size_t line_pixels, eqml_write_row_fn write_row,
    void *cookie)
{
	uint16_t y;

	if (validate_scene(scene, values, line, line_pixels, write_row) == -1)
		return -1;
	for (y = 0; y < scene->height; y++) {
		size_t i;

		for (i = 0; i < scene->width; i++)
			line[i] = scene->background;
		for (i = 0; i < scene->text_count; i++) {
			const struct eqml_text *text = &scene->texts[i];
			struct text_run run;
			int64_t top = text_top(scene, text);

			if ((int64_t)y < top ||
			    (int64_t)y >= top + text->pixel_size)
				continue;
			/* This is the only repeated model formatting. It runs only for
			 * rows touched by this glyph, not for the full scene height.
			 */
			if (prepare_run(scene, text, values, &run) == -1)
				return -1;
			draw_text_row(scene, text, &run, text_left(scene, text, &run),
			    top, y, line);
		}
		errno = 0;
		if (write_row(cookie, y, line, scene->width) == -1) {
			if (errno == 0)
				errno = EIO;
			return -1;
		}
	}
	return 0;
}
