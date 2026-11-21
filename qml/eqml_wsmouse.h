/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef EQML_WSMOUSE_H
#define EQML_WSMOUSE_H

#include <stddef.h>
#include <stdint.h>
#include <dev/wscons/wsconsio.h>

#include "eqml_input.h"

struct eqml_wsmouse {
	int fd;
	int32_t x;
	int32_t y;
	uint32_t buttons;
	struct wscons_event pending_raw;
	struct timespec move_time;
	uint64_t wall_anchor_ms;
	uint64_t mono_anchor_ms;
	uint64_t last_event_ms;
	uint64_t move_time_ms;
	int32_t move_x;
	int32_t move_y;
	int clock_valid;
	int raw_pending;
	int move_pending;
	int move_changed;
};

struct eqml_wsmouse_event {
	/* Absolute device pixels after relative deltas; app subtracts viewport. */
	enum eqml_phase phase;
	int32_t x;
	int32_t y;
	uint32_t button; /* Changed button bit on DOWN/UP; zero on MOVE. */
	uint32_t modifiers; /* Always zero for wscons. */
	uint64_t now_ms; /* CLOCK_MONOTONIC milliseconds. */
};

/* Initialize before open. The adapter owns the descriptor. Reads are
 * nonblocking and return an event count (0 when drained) or -1 with errno.
 */
void eqml_wsmouse_init(struct eqml_wsmouse *);
int eqml_wsmouse_open(struct eqml_wsmouse *, const char *device);
int eqml_wsmouse_read(struct eqml_wsmouse *, struct eqml_wsmouse_event *,
	size_t event_capacity);
int eqml_wsmouse_close(struct eqml_wsmouse *);

#endif /* EQML_WSMOUSE_H */
