/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <dev/wscons/wsconsio.h>

#include "eqml_input.h"
#include "eqml_wsmouse.h"

static int pipefd[2];
static int opened_flags;
static int selected_version;
static uint64_t test_monotonic_ms = 500000;
static uint64_t test_realtime_ms = 100000;
static unsigned int click_count;
static unsigned int hold_count;

int
eqml_test_open(const char *path, int flags, ...)
{
	if (strcmp(path, "/dev/wsmouse-test") != 0) {
		errno = ENOENT;
		return -1;
	}
	opened_flags = flags;
	return pipefd[0];
}

int
eqml_test_ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	int *version;

	if (fd != pipefd[0] || request != WSMOUSEIO_SETVERSION) {
		errno = EINVAL;
		return -1;
	}
	va_start(ap, request);
	version = va_arg(ap, int *);
	selected_version = *version;
	va_end(ap);
	return 0;
}

int
eqml_test_clock_gettime(clockid_t clock_id, struct timespec *ts)
{
	uint64_t value;
	if (clock_id == CLOCK_MONOTONIC)
		value = test_monotonic_ms;
	else if (clock_id == CLOCK_REALTIME)
		value = test_realtime_ms;
	else {
		errno = EINVAL;
		return -1;
	}
	ts->tv_sec = (time_t)(value / 1000);
	ts->tv_nsec = (long)(value % 1000) * 1000000L;
	return 0;
}

static int
check(int condition)
{
	if (!condition) {
		(void)write(STDERR_FILENO, "wsmouse assertion failed\n", 25);
		return 0;
	}
	return 1;
}

static int
put_absolute(uint32_t type, int value, uint64_t milliseconds)
{
	struct wscons_event event = {
		.type = type,
		.value = value,
		.time = {
			.tv_sec = (time_t)(milliseconds / 1000),
			.tv_nsec = (long)(milliseconds % 1000) * 1000000L
		}
	};

	return write(pipefd[1], &event, sizeof(event)) == sizeof(event);
}

static int
put_event(uint32_t type, int value, uint64_t elapsed)
{
	return put_absolute(type, value, 100000 + elapsed);
}

static int
ui_handler(struct eqml_ui *ui, size_t node, enum eqml_signal signal,
	const struct eqml_mouse *mouse)
{
	(void)ui;
	(void)node;
	(void)mouse;
	if (signal == EQML_CLICKED)
		click_count++;
	if (signal == EQML_PRESS_AND_HOLD)
		hold_count++;
	return 0;
}

static const struct eqml_node ui_nodes[] = {
	{
		.kind = EQML_NODE_RECT, .parent = -1, .width = 320, .height = 480,
		.bottom_edge = EQML_EDGE_NONE, .bottom_target = -1,
		.opacity_area = -1, .text_slot = -1, .drag_target = -1
	},
	{
		.kind = EQML_NODE_AREA, .parent = 0, .width = 120, .height = 120,
		.bottom_edge = EQML_EDGE_NONE, .bottom_target = -1,
		.opacity_area = -1, .text_slot = -1,
		.buttons = EQML_BUTTON_LEFT,
		.signals = (1U << EQML_CLICKED) | (1U << EQML_PRESS_AND_HOLD),
		.drag_target = -1
	}
};

static const struct eqml_program ui_program = {
	.nodes = ui_nodes,
	.node_count = sizeof(ui_nodes) / sizeof(ui_nodes[0]),
	.handler = ui_handler
};

static const struct eqml_scene ui_scene = {
	.width = 320,
	.height = 480,
	.program = &ui_program
};

int
main(void)
{
	struct eqml_wsmouse mouse;
	struct eqml_wsmouse_event events[3];
	struct eqml_ui *ui;
	int flags, count, total = 0, ok = 1;
	static struct eqml_wsmouse_event observed[12];

	if (!check(pipe(pipefd) == 0))
		return 1;
	flags = fcntl(pipefd[0], F_GETFL);
	if (!check(flags != -1 && fcntl(pipefd[0], F_SETFL,
	    flags | O_NONBLOCK) == 0))
		return 1;
	eqml_wsmouse_init(&mouse);
	if (!check(eqml_wsmouse_open(&mouse, "/dev/wsmouse-test") == 0))
		return 1;
	ok &= check(opened_flags == (O_RDONLY | O_NONBLOCK));
	ok &= check(selected_version == WSMOUSE_EVENT_VERSION);
	if (SIZE_MAX > INT_MAX)
		ok &= check(eqml_wsmouse_read(&mouse, events,
		    (size_t)INT_MAX + 1) == -1 && errno == EINVAL);

	/* Two axes and the press belong to one kernel report and share its time. */
	ok &= check(put_event(WSCONS_EVENT_MOUSE_ABSOLUTE_X, 100, 0));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_ABSOLUTE_Y, 100, 0));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_DOWN, 0, 0));
	/* Relative Y is screen-down negative; X/Y coalesce into one diagonal. */
	ok &= check(put_event(WSCONS_EVENT_MOUSE_DELTA_X, 10, 20));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_DELTA_Y, 10, 20));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_UP, 0, 100));
	/* A short tap, followed by a queued press that spans the hold deadline. */
	ok &= check(put_event(WSCONS_EVENT_MOUSE_ABSOLUTE_X, 100, 200));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_ABSOLUTE_Y, 100, 200));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_DOWN, 0, 200));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_UP, 0, 1100));
	/* wscons 1=middle and 2=right; QML follows Qt's opposite bit ordering. */
	ok &= check(put_event(WSCONS_EVENT_MOUSE_DOWN, 1, 1200));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_DOWN, 2, 1200));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_UP, 1, 1210));
	ok &= check(put_event(WSCONS_EVENT_MOUSE_UP, 2, 1210));
	if (!ok)
		return 1;

	/* Read these reports after they have accumulated in the kernel queue. */
	test_monotonic_ms += 1500;
	test_realtime_ms += 1500;
	ui = eqml_ui_create(&ui_scene, NULL, NULL);
	if (!check(ui != NULL))
		return 1;
	for (;;) {
		count = eqml_wsmouse_read(&mouse, events,
		    sizeof(events) / sizeof(events[0]));
		if (!check(count >= 0))
			return 1;
		if (count == 0)
			break;
		for (int i = 0; i < count; i++) {
			if (!check(total < (int)(sizeof(observed) / sizeof(observed[0]))))
				return 1;
			observed[total++] = events[i];
			if (eqml_ui_pointer(ui, events[i].phase, events[i].x,
			    events[i].y, events[i].button, events[i].modifiers,
			    events[i].now_ms) == -1)
				return 1;
		}
	}
	ok &= check(total == 11);
	if (total == 11) {
		ok &= check(observed[0].phase == EQML_MOVE &&
		    observed[0].x == 100 && observed[0].y == 100 &&
		    observed[0].now_ms == 500000);
		ok &= check(observed[1].phase == EQML_DOWN &&
		    observed[1].button == EQML_BUTTON_LEFT &&
		    observed[1].now_ms == 500000);
		ok &= check(observed[2].phase == EQML_MOVE &&
		    observed[2].x == 110 && observed[2].y == 90 &&
		    observed[2].now_ms == 500020);
		ok &= check(observed[3].phase == EQML_UP &&
		    observed[3].button == EQML_BUTTON_LEFT &&
		    observed[3].now_ms == 500100);
		ok &= check(observed[4].phase == EQML_MOVE &&
		    observed[4].x == 100 && observed[4].y == 100 &&
		    observed[4].now_ms == 500200);
		ok &= check(observed[5].phase == EQML_DOWN &&
		    observed[5].now_ms == 500200);
		ok &= check(observed[6].phase == EQML_UP &&
		    observed[6].now_ms == 501100);
		ok &= check(observed[7].phase == EQML_DOWN &&
		    observed[7].button == EQML_BUTTON_MIDDLE);
		ok &= check(observed[8].phase == EQML_DOWN &&
		    observed[8].button == EQML_BUTTON_RIGHT);
		ok &= check(observed[9].phase == EQML_UP &&
		    observed[9].button == EQML_BUTTON_MIDDLE);
		ok &= check(observed[10].phase == EQML_UP &&
		    observed[10].button == EQML_BUTTON_RIGHT);
		for (int i = 1; i < total; i++)
			ok &= check(observed[i].now_ms >= observed[i - 1].now_ms);
	}
	ok &= check(click_count == 1 && hold_count == 1);
	/* A queued DOWN predates a backward realtime correction. Its timestamp
	 * cannot run ahead and suppress the following contact's real duration.
	 */
	ok &= check(put_event(WSCONS_EVENT_MOUSE_DOWN, 0, 1600));
	test_monotonic_ms = 502000;
	test_realtime_ms = 90000;
	count = eqml_wsmouse_read(&mouse, events, 3);
	ok &= check(count == 1 && events[0].now_ms == 502000);
	test_monotonic_ms += 125;
	test_realtime_ms += 125;
	ok &= check(put_absolute(WSCONS_EVENT_MOUSE_UP, 0, test_realtime_ms));
	count = eqml_wsmouse_read(&mouse, events, 3);
	ok &= check(count == 1 && events[0].now_ms == 502125);
	ok &= check(eqml_wsmouse_close(&mouse) == 0 && mouse.fd == -1);
	eqml_ui_destroy(ui);
	(void)close(pipefd[1]);
	return ok ? 0 : 1;
}
