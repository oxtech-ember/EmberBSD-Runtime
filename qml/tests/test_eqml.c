/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "eqml.h"

#define WIDTH 96
#define HEIGHT 48

struct capture {
	uint16_t pixels[WIDTH * HEIGHT];
	unsigned int rows;
	int fail_at;
};

static int
capture_row(void *cookie, uint16_t y, const uint16_t *pixels, uint16_t width)
{
	struct capture *capture = cookie;

	if ((int)y == capture->fail_at) {
		errno = EIO;
		return -1;
	}
	assert(width == WIDTH && y < HEIGHT);
	memcpy(capture->pixels + (size_t)y * WIDTH, pixels,
	    (size_t)width * sizeof(*pixels));
	capture->rows++;
	return 0;
}

static unsigned int
count_color(const struct capture *capture, unsigned int x0, unsigned int y0,
    unsigned int x1, unsigned int y1, uint16_t color)
{
	unsigned int x, y, count = 0;

	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++)
			if (capture->pixels[y * WIDTH + x] == color)
				count++;
	return count;
}

static int
region_differs(const struct capture *a, const struct capture *b,
    unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1)
{
	unsigned int x, y;

	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++)
			if (a->pixels[y * WIDTH + x] != b->pixels[y * WIDTH + x])
				return 1;
	return 0;
}

static void
test_bound_text_and_values(void)
{
	static const struct eqml_int properties[] = {
		{ .name = "temperature", .initial = 21 }
	};
	static const struct eqml_text texts[] = {
		{ .prefix = "T=", .suffix = " C", .binding = 0,
		  .x = 0, .y = 0, .color = 0xffff, .pixel_size = 14,
		  .anchors = EQML_HCENTER | EQML_VCENTER }
	};
	const struct eqml_scene scene = {
		.width = WIDTH, .height = HEIGHT, .background = 0x1234,
		.texts = texts, .text_count = 1,
		.properties = properties, .property_count = 1
	};
	struct capture positive = { .fail_at = -1 }, negative = { .fail_at = -1 };
	uint16_t line[WIDTH];
	int32_t value = 21;

	assert(eqml_render(&scene, &value, line, WIDTH, capture_row,
	    &positive) == 0);
	assert(positive.rows == HEIGHT);
	assert(positive.pixels[0] == scene.background);
	assert(count_color(&positive, 0, 0, WIDTH, HEIGHT, 0xffff) > 30);
	assert(count_color(&positive, 0, 0, WIDTH, HEIGHT, scene.background) > 1000);

	value = -8;
	assert(eqml_render(&scene, &value, line, WIDTH, capture_row,
	    &negative) == 0);
	assert(negative.rows == HEIGHT);
	assert(memcmp(positive.pixels, negative.pixels,
	    sizeof(positive.pixels)) != 0);
	/* Centered run width is 94 pixels; the bound number occupies x=33..65. */
	assert(region_differs(&positive, &negative, 32, 0, 66, HEIGHT));
}

static void
test_int32_min(void)
{
	static const struct eqml_int properties[] = {
		{ .name = "n", .initial = INT32_MIN }
	};
	static const struct eqml_text texts[] = {
		{ .prefix = "", .suffix = "", .binding = 0,
		  .x = 0, .y = 0, .color = 0xffff, .pixel_size = 7 }
	};
	const struct eqml_scene scene = {
		.width = WIDTH, .height = HEIGHT, .background = 0,
		.texts = texts, .text_count = 1,
		.properties = properties, .property_count = 1
	};
	struct capture capture = { .fail_at = -1 };
	uint16_t line[WIDTH];
	int32_t value = INT32_MIN;

	assert(eqml_render(&scene, &value, line, WIDTH, capture_row,
	    &capture) == 0);
	assert(count_color(&capture, 0, 0, WIDTH, HEIGHT, 0xffff) > 10);
}

static void
test_errors(void)
{
	static const struct eqml_text text[] = {
		{ .prefix = "ok", .binding = -1, .pixel_size = 7,
		  .color = 0xffff }
	};
	const struct eqml_scene scene = {
		.width = WIDTH, .height = HEIGHT, .background = 0,
		.texts = text, .text_count = 1
	};
	struct capture capture = { .fail_at = 2 };
	uint16_t line[WIDTH];

	assert(eqml_render(&scene, NULL, line, WIDTH - 1, capture_row,
	    &capture) == -1 && errno == EINVAL);
	assert(eqml_render(&scene, NULL, line, WIDTH, capture_row,
	    &capture) == -1 && errno == EIO);
	assert(capture.rows == 2);
}

struct ascii_capture {
	uint8_t visible[95];
	uint8_t pixels[8][760];
	unsigned int rows;
};

static int
capture_ascii_row(void *cookie, uint16_t y, const uint16_t *pixels,
    uint16_t width)
{
	struct ascii_capture *capture = cookie;
	unsigned int i, x;

	(void)y;
	assert(width == 760);
	for (i = 0; i < 95; i++)
		for (x = i * 8; x < i * 8 + 7; x++) {
			capture->pixels[y][x] = pixels[x] != 0;
			if (pixels[x] != 0)
				capture->visible[i] = 1;
		}
	capture->rows++;
	return 0;
}

static void
test_ascii_font(void)
{
	char printable[96];
	struct eqml_text text = {
		.prefix = printable, .binding = -1, .color = 0xffff,
		.pixel_size = 7
	};
	const struct eqml_scene scene = {
		.width = 760, .height = 8, .background = 0,
		.texts = &text, .text_count = 1
	};
	struct ascii_capture capture = {0};
	uint16_t line[760];
	unsigned int i, row;
	int case_differs = 0;

	for (i = 0; i < 95; i++)
		printable[i] = (char)(0x20 + i);
	printable[95] = '\0';
	assert(eqml_render(&scene, NULL, line, 760, capture_ascii_row,
	    &capture) == 0);
	assert(capture.rows == 8);
	for (i = 1; i < 95; i++) {
		if (!capture.visible[i])
			fprintf(stderr, "blank printable ASCII: 0x%02x\n", 0x20 + i);
		assert(capture.visible[i]);
	}
	for (row = 0; row < 8; row++)
		case_differs |= memcmp(
		    &capture.pixels[row][(unsigned int)('A' - 0x20) * 8],
		    &capture.pixels[row][(unsigned int)('a' - 0x20) * 8], 7) != 0;
	assert(case_differs);
}

int
main(void)
{
	test_bound_text_and_values();
	test_int32_min();
	test_errors();
	test_ascii_font();
	puts("eqml renderer: 4 tests passed");
	return 0;
}
