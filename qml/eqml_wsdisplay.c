/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dev/wscons/wsconsio.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "eqml_wsdisplay.h"

static int
valid_fb(const struct wsdisplay_fbinfo *info,
    const struct wsdisplayio_fbinfo *fbinfo, u_int stride,
    uint16_t expected_width, uint16_t expected_height)
{
	const struct _fbi_rgbmasks *rgb = &fbinfo->fbi_subtype.fbi_rgbmasks;

	return info->depth == 16 && info->width != 0 && info->height != 0 &&
	    info->width <= 4096 && info->height <= 4096 &&
	    (expected_width == 0 || info->width == expected_width) &&
	    (expected_height == 0 || info->height == expected_height) &&
	    stride >= (size_t)info->width * sizeof(uint16_t) &&
	    stride <= SIZE_MAX / info->height &&
	    fbinfo->fbi_width == info->width &&
	    fbinfo->fbi_height == info->height &&
	    fbinfo->fbi_stride == stride && fbinfo->fbi_bitsperpixel == 16 &&
	    fbinfo->fbi_pixeltype == WSFB_RGB && fbinfo->fbi_fboffset == 0 &&
	    fbinfo->fbi_fbsize >= (uint64_t)stride * info->height &&
	    rgb->red_offset == 11 && rgb->red_size == 5 &&
	    rgb->green_offset == 5 && rgb->green_size == 6 &&
	    rgb->blue_offset == 0 && rgb->blue_size == 5 &&
	    rgb->alpha_size == 0;
}

int
eqml_wsdisplay_open(struct eqml_wsdisplay *display, const char *device,
    uint16_t expected_width, uint16_t expected_height)
{
	struct wsdisplay_fbinfo info;
	struct wsdisplayio_fbinfo fbinfo = {0};
	u_int stride, mode;
	int saved_errno;

	if (display == NULL || device == NULL) {
		errno = EINVAL;
		return -1;
	}
	memset(display, 0, sizeof(*display));
	display->fd = -1;
	display->fd = open(device, O_RDWR);
	if (display->fd == -1)
		return -1;
	if (ioctl(display->fd, WSDISPLAYIO_GINFO, &info) == -1 ||
	    ioctl(display->fd, WSDISPLAYIO_GET_FBINFO, &fbinfo) == -1 ||
	    ioctl(display->fd, WSDISPLAYIO_GMODE, &display->old_mode) == -1 ||
	    ioctl(display->fd, WSDISPLAYIO_LINEBYTES, &stride) == -1)
		goto fail;
	if (!valid_fb(&info, &fbinfo, stride, expected_width, expected_height)) {
		errno = ENOTSUP;
		goto fail;
	}
	display->width = (uint16_t)info.width;
	display->height = (uint16_t)info.height;
	display->stride = stride;
	display->map_size = display->stride * display->height;
	mode = WSDISPLAYIO_MODE_DUMBFB;
	if (ioctl(display->fd, WSDISPLAYIO_SMODE, &mode) == -1)
		goto fail;
	display->mode_changed = 1;
	display->framebuffer = mmap(NULL, display->map_size,
	    PROT_READ | PROT_WRITE, MAP_SHARED, display->fd, 0);
	if (display->framebuffer == MAP_FAILED) {
		display->framebuffer = NULL;
		goto fail;
	}
	return 0;

fail:
	saved_errno = errno;
	(void)eqml_wsdisplay_close(display);
	errno = saved_errno;
	return -1;
}

struct viewport {
	struct eqml_wsdisplay *display;
	uint16_t x, y;
};

static int
present_row(void *cookie, uint16_t y, const uint16_t *pixels, uint16_t width)
{
	const struct viewport *v = cookie;
	struct eqml_wsdisplay *d = v->display;
	uint8_t *row = d->framebuffer + (size_t)(y + v->y) * d->stride;

	memcpy(row + (size_t)v->x * sizeof(*pixels), pixels, (size_t)width * sizeof(*pixels));
	return 0;
}

int
eqml_wsdisplay_present_at(struct eqml_wsdisplay *display,
    const struct eqml_scene *scene, const int32_t *values, uint16_t x, uint16_t y,
    uint16_t *line, size_t line_pixels)
{
	struct viewport viewport = { display, x, y };
	if (display == NULL || display->framebuffer == NULL || scene == NULL ||
	    x > display->width || scene->width > display->width - x ||
	    y > display->height || scene->height > display->height - y) {
		errno = EINVAL;
		return -1;
	}
	return eqml_render(scene, values, line, line_pixels, present_row, &viewport);
}

int
eqml_wsdisplay_present(struct eqml_wsdisplay *display,
    const struct eqml_scene *scene, const int32_t *values, uint16_t *line,
    size_t line_pixels)
{
	if (display == NULL || scene == NULL || scene->width != display->width ||
	    scene->height != display->height) {
		errno = EINVAL;
		return -1;
	}
	return eqml_wsdisplay_present_at(display, scene, values, 0, 0, line, line_pixels);
}

int
eqml_wsdisplay_close(struct eqml_wsdisplay *display)
{
	int error = 0;

	if (display == NULL) {
		errno = EINVAL;
		return -1;
	}
	if (display->framebuffer != NULL &&
	    munmap(display->framebuffer, display->map_size) == -1)
		error = errno;
	if (display->mode_changed && display->fd != -1 &&
	    ioctl(display->fd, WSDISPLAYIO_SMODE, &display->old_mode) == -1 &&
	    error == 0)
		error = errno;
	if (display->fd != -1 && close(display->fd) == -1 && error == 0)
		error = errno;
	display->framebuffer = NULL;
	display->fd = -1;
	display->mode_changed = 0;
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}
