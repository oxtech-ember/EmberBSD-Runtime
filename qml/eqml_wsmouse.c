/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <dev/wscons/wsconsio.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "eqml_wsmouse.h"

static int
invalid(void)
{
	errno = EINVAL;
	return -1;
}

static int
clock_ms(clockid_t id, uint64_t *result)
{
	struct timespec ts;
	uint64_t seconds, millis;

	if (clock_gettime(id, &ts) == -1)
		return -1;
	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L) {
		errno = EIO;
		return -1;
	}
	seconds = (uint64_t)ts.tv_sec;
	if (seconds > (UINT64_MAX - (uint64_t)ts.tv_nsec / 1000000U) / 1000U) {
		errno = EOVERFLOW;
		return -1;
	}
	millis = seconds * 1000U + (uint64_t)ts.tv_nsec / 1000000U;
	*result = millis;
	return 0;
}

static int
valid_timestamp(const struct timespec *ts)
{
	return ts->tv_sec >= 0 && ts->tv_nsec >= 0 &&
	    ts->tv_nsec < 1000000000L &&
	    (uint64_t)ts->tv_sec <= (UINT64_MAX -
	    (uint64_t)ts->tv_nsec / 1000000U) / 1000U;
}

static uint64_t
timestamp_ms(const struct timespec *ts)
{
	return (uint64_t)ts->tv_sec * 1000U +
	    (uint64_t)ts->tv_nsec / 1000000U;
}

static int
same_timestamp(const struct timespec *a, const struct timespec *b)
{
	return a->tv_sec == b->tv_sec && a->tv_nsec == b->tv_nsec;
}

static int
refresh_clock_pair(struct eqml_wsmouse *mouse, uint64_t *read_time)
{
	uint64_t mono, wall, mono_elapsed, wall_elapsed, difference;

	if (clock_ms(CLOCK_MONOTONIC, &mono) == -1 ||
	    clock_ms(CLOCK_REALTIME, &wall) == -1)
		return -1;
	*read_time = mono;
	if (!mouse->clock_valid || wall < mouse->wall_anchor_ms ||
	    mono < mouse->mono_anchor_ms) {
		mouse->clock_valid = 1;
		mouse->wall_anchor_ms = wall;
		mouse->mono_anchor_ms = mono;
		return 0;
	}
	mono_elapsed = mono - mouse->mono_anchor_ms;
	wall_elapsed = wall - mouse->wall_anchor_ms;
	difference = mono_elapsed > wall_elapsed ?
	    mono_elapsed - wall_elapsed : wall_elapsed - mono_elapsed;
	/* Re-anchor after a wall-clock step; queued event times stay clamped. */
	if (difference > 250) {
		mouse->wall_anchor_ms = wall;
		mouse->mono_anchor_ms = mono;
	}
	return 0;
}

static uint64_t
event_time_ms(struct eqml_wsmouse *mouse, const struct timespec *event_time)
{
	uint64_t wall_event, value;

	wall_event = timestamp_ms(event_time);
	if (wall_event >= mouse->wall_anchor_ms) {
		uint64_t delta = wall_event - mouse->wall_anchor_ms;
		value = delta > UINT64_MAX - mouse->mono_anchor_ms ? UINT64_MAX :
		    mouse->mono_anchor_ms + delta;
	} else {
		uint64_t delta = mouse->wall_anchor_ms - wall_event;
		value = delta > mouse->mono_anchor_ms ? 0 :
		    mouse->mono_anchor_ms - delta;
	}
	if (value < mouse->last_event_ms)
		value = mouse->last_event_ms;
	return value;
}

static int32_t
add_delta(int32_t coordinate, int64_t delta)
{
	int64_t result = (int64_t)coordinate + delta;

	if (result > INT32_MAX)
		return INT32_MAX;
	if (result < INT32_MIN)
		return INT32_MIN;
	return (int32_t)result;
}

static int
button_bit(int raw_button, uint32_t *bit)
{
	if (raw_button < 0 || raw_button > 26)
		return 0;
	/* wscons numbers left, middle, right; Qt numbers left, right, middle. */
	if (raw_button == 1)
		raw_button = 2;
	else if (raw_button == 2)
		raw_button = 1;
	*bit = 1U << (unsigned int)raw_button;
	return 1;
}

static int
emit(struct eqml_wsmouse *mouse, enum eqml_phase phase, uint32_t button,
	uint64_t now_ms, struct eqml_wsmouse_event *event)
{
	if (now_ms < mouse->last_event_ms)
		now_ms = mouse->last_event_ms;
	else
		mouse->last_event_ms = now_ms;
	event->phase = phase;
	event->x = mouse->x;
	event->y = mouse->y;
	event->button = button;
	event->modifiers = 0;
	event->now_ms = now_ms;
	return 1;
}

static int
emit_move(struct eqml_wsmouse *mouse, struct eqml_wsmouse_event *event)
{
	int changed;

	if (!mouse->move_pending)
		return 0;
	changed = mouse->move_changed;
	if (changed)
		(void)emit(mouse, EQML_MOVE, 0, mouse->move_time_ms, event);
	mouse->move_pending = 0;
	mouse->move_changed = 0;
	return changed;
}

void
eqml_wsmouse_init(struct eqml_wsmouse *mouse)
{
	if (mouse == NULL)
		return;
	memset(mouse, 0, sizeof(*mouse));
	mouse->fd = -1;
}

int
eqml_wsmouse_open(struct eqml_wsmouse *mouse, const char *device)
{
	int version = WSMOUSE_EVENT_VERSION;
	int fd;

	if (mouse == NULL || device == NULL || mouse->fd != -1)
		return invalid();
	fd = open(device, O_RDONLY | O_NONBLOCK);
	if (fd == -1)
		return -1;
	if (ioctl(fd, WSMOUSEIO_SETVERSION, &version) == -1) {
		int saved_errno = errno;
		(void)close(fd);
		errno = saved_errno;
		return -1;
	}
	mouse->fd = fd;
	mouse->x = 0;
	mouse->y = 0;
	mouse->buttons = 0;
	mouse->clock_valid = 0;
	mouse->last_event_ms = 0;
	mouse->move_pending = 0;
	mouse->raw_pending = 0;
	return 0;
}

static int
read_raw(struct eqml_wsmouse *mouse, struct wscons_event *raw)
{
	ssize_t n;

	if (mouse->raw_pending) {
		*raw = mouse->pending_raw;
		mouse->raw_pending = 0;
		return 1;
	}
	n = read(mouse->fd, raw, sizeof(*raw));
	if (n == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
	if (n == 0 || (size_t)n != sizeof(*raw)) {
		errno = EIO;
		return -1;
	}
	return 1;
}

static void
defer_raw(struct eqml_wsmouse *mouse, const struct wscons_event *raw)
{
	mouse->pending_raw = *raw;
	mouse->raw_pending = 1;
}

static int
queue_move(struct eqml_wsmouse *mouse, const struct wscons_event *raw,
	uint64_t now_ms)
{
	int32_t old_x = mouse->x, old_y = mouse->y;

	switch (raw->type) {
	case WSCONS_EVENT_MOUSE_ABSOLUTE_X:
		mouse->x = raw->value;
		break;
	case WSCONS_EVENT_MOUSE_ABSOLUTE_Y:
		mouse->y = raw->value;
		break;
	case WSCONS_EVENT_MOUSE_DELTA_X:
		mouse->x = add_delta(mouse->x, raw->value);
		break;
	case WSCONS_EVENT_MOUSE_DELTA_Y:
		mouse->y = add_delta(mouse->y, -(int64_t)raw->value);
		break;
	default:
		return 0;
	}
	if (!mouse->move_pending) {
		mouse->move_pending = 1;
		mouse->move_time = raw->time;
		mouse->move_time_ms = now_ms;
		mouse->move_x = old_x;
		mouse->move_y = old_y;
	}
	mouse->move_changed = mouse->x != mouse->move_x ||
	    mouse->y != mouse->move_y;
	return 1;
}

static int
button_event(struct eqml_wsmouse *mouse, const struct wscons_event *raw,
	uint64_t now_ms, struct eqml_wsmouse_event *event)
{
	uint32_t button;
	int down;

	if (!button_bit(raw->value, &button))
		return 0;
	down = raw->type == WSCONS_EVENT_MOUSE_DOWN;
	if (down) {
		if (mouse->buttons & button)
			return 0;
		mouse->buttons |= button;
		return emit(mouse, EQML_DOWN, button, now_ms, event);
	}
	if ((mouse->buttons & button) == 0)
		return 0;
	mouse->buttons &= ~button;
	return emit(mouse, EQML_UP, button, now_ms, event);
}

int
eqml_wsmouse_read(struct eqml_wsmouse *mouse,
	struct eqml_wsmouse_event *events, size_t event_capacity)
{
	struct wscons_event raw;
	uint64_t now_ms, read_time;
	int count = 0, got, axis, button, same;

	if (mouse == NULL || mouse->fd == -1 || event_capacity > INT_MAX ||
	    (event_capacity != 0 && events == NULL))
		return invalid();
	if (event_capacity == 0)
		return 0;
	if (refresh_clock_pair(mouse, &read_time) == -1)
		return -1;
	for (;;) {
		got = read_raw(mouse, &raw);
		if (got == -1)
			return -1;
		if (got == 0) {
			if (mouse->move_pending && mouse->move_changed) {
				if ((size_t)count == event_capacity)
					return count;
				count += emit_move(mouse, &events[count]);
			}
			return count;
		}
		if (!valid_timestamp(&raw.time)) {
			errno = EIO;
			return -1;
		}
		now_ms = event_time_ms(mouse, &raw.time);
		if (now_ms > read_time) {
			/* A new report can arrive during a drain. A queued report from
			 * before a backward wall-clock step can also appear in the future.
			 */
			if (clock_ms(CLOCK_MONOTONIC, &read_time) == -1)
				return -1;
			if (now_ms > read_time)
				now_ms = read_time;
		}
		same = mouse->move_pending &&
		    same_timestamp(&mouse->move_time, &raw.time);
		axis = raw.type == WSCONS_EVENT_MOUSE_ABSOLUTE_X ||
		    raw.type == WSCONS_EVENT_MOUSE_ABSOLUTE_Y ||
		    raw.type == WSCONS_EVENT_MOUSE_DELTA_X ||
		    raw.type == WSCONS_EVENT_MOUSE_DELTA_Y;
		button = raw.type == WSCONS_EVENT_MOUSE_DOWN ||
		    raw.type == WSCONS_EVENT_MOUSE_UP;
		if (mouse->move_pending && (!same || button)) {
			if ((size_t)count == event_capacity) {
				defer_raw(mouse, &raw);
				return count;
			}
			count += emit_move(mouse, &events[count]);
		}
		if (axis) {
			if (queue_move(mouse, &raw, now_ms) == -1)
				return -1;
			continue;
		}
		if (button) {
			if ((size_t)count == event_capacity) {
				defer_raw(mouse, &raw);
				return count;
			}
			count += button_event(mouse, &raw, now_ms, &events[count]);
		}
	}
}

int
eqml_wsmouse_close(struct eqml_wsmouse *mouse)
{
	int fd;

	if (mouse == NULL)
		return invalid();
	fd = mouse->fd;
	mouse->fd = -1;
	mouse->buttons = 0;
	mouse->raw_pending = 0;
	mouse->move_pending = 0;
	if (fd == -1)
		return 0;
	return close(fd);
}
