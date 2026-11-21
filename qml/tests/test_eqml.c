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
	static const struct eqml_item items[] = {
		{ .kind = EQML_TEXT, .data.text = {
			.prefix = "T=", .suffix = " C", .binding = 0,
			.x = 0, .y = 0, .parent_width = WIDTH,
			.parent_height = HEIGHT, .color = 0xffff,
			.pixel_size = 14,
			.anchors = EQML_HCENTER | EQML_VCENTER
		} }
	};
	const struct eqml_scene scene = {
		.width = WIDTH, .height = HEIGHT, .background = 0x1234,
		.items = items, .item_count = 1,
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
	static const struct eqml_item items[] = {
		{ .kind = EQML_TEXT, .data.text = {
			.prefix = "", .suffix = "", .binding = 0,
			.x = 0, .y = 0, .parent_width = WIDTH,
			.parent_height = HEIGHT, .color = 0xffff,
			.pixel_size = 7
		} }
	};
	const struct eqml_scene scene = {
		.width = WIDTH, .height = HEIGHT, .background = 0,
		.items = items, .item_count = 1,
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
	static const struct eqml_item items[] = {
		{ .kind = EQML_TEXT, .data.text = {
			.prefix = "ok", .binding = -1, .pixel_size = 7,
			.parent_width = WIDTH, .parent_height = HEIGHT,
			.color = 0xffff
		} }
	};
	const struct eqml_scene scene = {
		.width = WIDTH, .height = HEIGHT, .background = 0,
		.items = items, .item_count = 1
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
	struct eqml_item item = {
		.kind = EQML_TEXT, .data.text = {
			.prefix = printable, .binding = -1, .color = 0xffff,
			.pixel_size = 7, .parent_width = 760, .parent_height = 8
		}
	};
	const struct eqml_scene scene = {
		.width = 760, .height = 8, .background = 0,
		.items = &item, .item_count = 1
	};
	struct ascii_capture capture = {0};
	uint16_t line[760];
	unsigned int i, row;
	int case_differs = 0;

	for (i = 0; i < 95; i++)
		printable[i] = (char)(0x20 + i);
	printable[95] = '\0';
	assert(printable[95] == '\0');
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

static void
test_mixed_items_order_and_clipping(void)
{
	static const struct eqml_item items[] = {
		{ .kind = EQML_RECT, .data.rect = {
			.x = -2, .y = -3, .width = 5, .height = 6,
			.color = 0x2222
		} },
		{ .kind = EQML_RECT, .data.rect = {
			.x = WIDTH - 2, .y = HEIGHT - 2, .width = 5, .height = 5,
			.color = 0x3333
		} },
		{ .kind = EQML_RECT, .data.rect = {
			.x = 8, .y = 7, .width = 0, .height = 5,
			.color = 0x4444
		} },
		{ .kind = EQML_RECT, .data.rect = {
			.x = 8, .y = 7, .width = 5, .height = 0,
			.color = 0x5555
		} },
		{ .kind = EQML_RECT, .data.rect = {
			.x = INT32_MAX - 4, .y = 0, .width = UINT16_MAX,
			.height = 2, .color = 0xaaaa
		} },
		{ .kind = EQML_RECT, .data.rect = {
			.x = 30, .y = 10, .width = 8, .height = 8,
			.color = 0x6666
		} },
		{ .kind = EQML_TEXT, .data.text = {
			.prefix = "A", .binding = -1, .x = 30, .y = 10,
			.parent_width = 8, .parent_height = 8,
			.color = 0x7777, .pixel_size = 8
		} },
		{ .kind = EQML_TEXT, .data.text = {
			.prefix = "A", .binding = -1, .x = 50, .y = 10,
			.parent_width = 8, .parent_height = 8,
			.color = 0x8888, .pixel_size = 8
		} },
		{ .kind = EQML_RECT, .data.rect = {
			.x = 50, .y = 10, .width = 8, .height = 8,
			.color = 0x9999
		} },
		{ .kind = EQML_TEXT, .data.text = {
			.prefix = "A", .binding = -1, .x = INT32_MAX, .y = 0,
			.parent_width = 0, .parent_height = 0,
			.color = 0xbbbb, .pixel_size = 8
		} }
	};
	const struct eqml_scene scene = {
		.width = WIDTH, .height = HEIGHT, .background = 0x1111,
		.items = items, .item_count = sizeof(items) / sizeof(items[0])
	};
	struct capture capture = { .fail_at = -1 };
	uint16_t line[WIDTH];

	assert(eqml_render(&scene, NULL, line, WIDTH, capture_row,
	    &capture) == 0);
	assert(count_color(&capture, 0, 0, 3, 3, 0x2222) == 9);
	assert(count_color(&capture, WIDTH - 2, HEIGHT - 2, WIDTH, HEIGHT,
	    0x3333) == 4);
	assert(count_color(&capture, 8, 7, 13, 12, 0x4444) == 0);
	assert(count_color(&capture, 8, 7, 13, 12, 0x5555) == 0);
	assert(count_color(&capture, 0, 0, WIDTH, 2, 0xaaaa) == 0);
	assert(count_color(&capture, 30, 10, 38, 18, 0x7777) > 0);
	assert(count_color(&capture, 30, 10, 38, 18, 0x6666) > 0);
	assert(count_color(&capture, 50, 10, 58, 18, 0x8888) == 0);
	assert(count_color(&capture, 50, 10, 58, 18, 0x9999) == 64);
	assert(count_color(&capture, 0, 0, WIDTH, HEIGHT, 0xbbbb) == 0);
	assert(count_color(&capture, 18, 20, 26, 28, 0x7777) == 0);
}

static void
test_text_center_uses_parent_extent(void)
{
	static const struct eqml_item items[] = {
		{ .kind = EQML_TEXT, .data.text = {
			.prefix = "A", .binding = -1,
			.x = 10, .y = 12, .parent_width = 24,
			.parent_height = 24, .color = 0xffff,
			.pixel_size = 8, .anchors = EQML_HCENTER | EQML_VCENTER
		} },
		{ .kind = EQML_TEXT, .data.text = {
			.prefix = "A", .binding = -1,
			.x = 70, .y = 40, .parent_width = 0,
			.parent_height = 0, .color = 0xeeee,
			.pixel_size = 8, .anchors = EQML_HCENTER | EQML_VCENTER
		} }
	};
	const struct eqml_scene scene = {
		.width = WIDTH, .height = HEIGHT, .background = 0,
		.items = items, .item_count = 2
	};
	struct capture capture = { .fail_at = -1 };
	uint16_t line[WIDTH];

	assert(eqml_render(&scene, NULL, line, WIDTH, capture_row,
	    &capture) == 0);
	assert(count_color(&capture, 18, 20, 26, 28, 0xffff) > 0);
	assert(count_color(&capture, 54, 32, 62, 40, 0xffff) == 0);
	assert(count_color(&capture, 66, 36, 74, 44, 0xeeee) > 0);
}

static void
test_unknown_item_kind(void)
{
	const struct eqml_item item = {
		.kind = (enum eqml_item_kind)99
	};
	const struct eqml_scene scene = {
		.width = WIDTH, .height = HEIGHT, .background = 0,
		.items = &item, .item_count = 1
	};
	struct capture capture = { .fail_at = -1 };
	uint16_t line[WIDTH];

	assert(eqml_render(&scene, NULL, line, WIDTH, capture_row,
	    &capture) == -1 && errno == EINVAL);
	assert(capture.rows == 0);
}

int
main(void)
{
	test_bound_text_and_values();
	test_int32_min();
	test_errors();
	test_ascii_font();
	test_mixed_items_order_and_clipping();
	test_text_center_uses_parent_extent();
	test_unknown_item_kind();
	puts("eqml renderer: 7 tests passed");
	return 0;
}
