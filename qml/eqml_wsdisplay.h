/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef EQML_WSDISPLAY_H
#define EQML_WSDISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "eqml.h"

struct eqml_wsdisplay {
	int fd;
	uint8_t *framebuffer;
	size_t map_size;
	size_t stride;
	uint16_t width;
	uint16_t height;
	unsigned int old_mode;
	int mode_changed;
};

/* expected_width/height may be zero to accept the device's reported size. */
int eqml_wsdisplay_open(struct eqml_wsdisplay *, const char *device,
	uint16_t expected_width, uint16_t expected_height);

/* Presents all rows with a single caller-owned RGB565 line buffer. */
int eqml_wsdisplay_present(struct eqml_wsdisplay *,
	const struct eqml_scene *, const int32_t *values, uint16_t *line,
	size_t line_pixels);

/* Present inside a rectangular viewport. Pixels outside it are preserved. */
int eqml_wsdisplay_present_at(struct eqml_wsdisplay *, const struct eqml_scene *,
	const int32_t *values, uint16_t x, uint16_t y, uint16_t *line,
	size_t line_pixels);

int eqml_wsdisplay_present_region_at(struct eqml_wsdisplay *, const struct eqml_scene *,
    const int32_t *values, uint16_t x, uint16_t y, const struct eqml_region *,
    uint16_t *line, size_t line_pixels);

/* Restores the previous wsdisplay mode, unmaps and closes. */
int eqml_wsdisplay_close(struct eqml_wsdisplay *);

#endif /* EQML_WSDISPLAY_H */
