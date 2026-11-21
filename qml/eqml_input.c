/* SPDX-License-Identifier: BSD-2-Clause */
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "eqml_input.h"

#define HOLD_MS 800U
#define DOUBLE_MS 400U
#define DRAG_DISTANCE 10
#define NODE_LIMIT 256
#define FRAME_MS 16U

struct placement {
	const struct eqml_bitmap *bitmap;
	int32_t x, y, width, height;
	int32_t local_x, local_y;
	int32_t old_text_width;
	uint8_t transparency, hidden, visible, visiting, ready;
	uint8_t text_changed;
};

struct animation {
	struct animation *next;
	int32_t *coordinate;
	int32_t from, to;
	uint32_t duration;
	uint64_t start;
};

struct eqml_ui {
	const struct eqml_program *program;
	struct eqml_scene frame;
	struct eqml_region damage;
	struct eqml_item *items;
	struct placement *places;
	char (*strings)[EQML_STRING_CAP];
	void (*logger)(void *, const char *);
	void *cookie;
	void (*signal_handler)(void *, const char *);
	void *signal_cookie;
	int grab, hover, last_click, inside, held, hold_done, doubled, dragging, moved;
	uint32_t button, modifiers, last_button;
	uint64_t down_time, last_time;
	struct eqml_point down, pointer, last_point, drag_origin, drag_start;
	unsigned int notifying;
	uint8_t pointer_present, recheck_pointer;
	uint64_t now;
	struct animation *animations;
};

static int
invalid(void)
{
	errno = EINVAL;
	return -1;
}

static int32_t
narrow(int64_t x)
{
	if (x > INT32_MAX)
		return INT32_MAX;
	if (x < INT32_MIN)
		return INT32_MIN;
	return (int32_t)x;
}

static int
valid_index(const struct eqml_ui *ui, int index)
{
	return index >= 0 && (size_t)index < ui->program->node_count;
}

static const char *
label(const struct eqml_ui *ui, const struct eqml_node *node)
{
	if (node->text_slot >= 0)
		return ui->strings[node->text_slot];
	return node->text != NULL ? node->text : "";
}

static int
cell_width(const struct eqml_node *node)
{
	return (node->pixel_size + 1) / 2;
}

static int32_t
label_width(const struct eqml_node *node, const char *text)
{
	int cell = cell_width(node), spacing = (cell + 7) / 8;
	size_t length = strlen(text);

	return length != 0 ? (int32_t)length * (cell + spacing) - spacing : 0;
}

static int
place(struct eqml_ui *ui, size_t index)
{
	const struct eqml_node *node = &ui->program->nodes[index];
	struct placement *p = &ui->places[index];
	const struct placement *parent = NULL;
	int64_t x = p->local_x, y = p->local_y;
	unsigned int opacity = 255;
	unsigned int transparency = node->transparency;

	if (p->ready)
		return 0;
	if (p->visiting)
		return invalid();
	p->visiting = 1;
	p->visible = !p->hidden;
	p->width = node->width;
	p->height = node->height;
	if (node->kind == EQML_NODE_TEXT) {
		if (p->width == 0)
			p->width = label_width(node, label(ui, node));
		if (p->height == 0)
			p->height = node->pixel_size;
	}
	if (node->parent >= 0) {
		if (place(ui, (size_t)node->parent) == -1)
			return -1;
		parent = &ui->places[node->parent];
		x += parent->x;
		y += parent->y;
		if (node->anchors & EQML_HCENTER)
			x += (parent->width - p->width) / 2;
		if (node->anchors & EQML_VCENTER)
			y += (parent->height - p->height) / 2;
		p->visible = p->visible && parent->visible;
		opacity = 255U - parent->transparency;
	}
	if (node->bottom_edge != EQML_EDGE_NONE) {
		const struct placement *target;
		if (place(ui, (size_t)node->bottom_target) == -1)
			return -1;
		target = &ui->places[node->bottom_target];
		y = (int64_t)target->y - node->bottom_margin - p->height;
		if (node->bottom_edge == EQML_EDGE_BOTTOM)
			y += target->height;
	}
	if (node->opacity_area >= 0 && ui->grab == node->opacity_area && ui->inside)
		transparency = node->pressed_transparency;
	opacity = (opacity * (255U - transparency) + 127U) / 255U;
	p->transparency = p->visible ? (uint8_t)(255U - opacity) : 255;
	p->x = narrow(x);
	p->y = narrow(y);
	p->ready = 1;
	p->visiting = 0;
	return 0;
}

static void
include_damage(struct eqml_ui *ui, int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct eqml_region *d = &ui->damage;
	int64_t right = (int64_t)x + width, bottom = (int64_t)y + height;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (right > ui->frame.width)
		right = ui->frame.width;
	if (bottom > ui->frame.height)
		bottom = ui->frame.height;
	if (right <= x || bottom <= y)
		return;
	if (d->width != 0) {
		if (x > d->x)
			x = d->x;
		if (y > d->y)
			y = d->y;
		if (right < d->x + d->width)
			right = d->x + d->width;
		if (bottom < d->y + d->height)
			bottom = d->y + d->height;
	}
	*d = (struct eqml_region){ (uint16_t)x, (uint16_t)y,
	    (uint16_t)(right - x), (uint16_t)(bottom - y) };
}

static void
full_damage(struct eqml_ui *ui)
{
	ui->damage = (struct eqml_region){ 0, 0, ui->frame.width,
		ui->frame.height };
}

static void
clear_text_changes(struct eqml_ui *ui)
{
	size_t i;

	for (i = 0; i < ui->program->node_count; i++)
		ui->places[i].text_changed = 0;
}

static void
track_placement(struct eqml_ui *ui, const struct eqml_item *old,
    const struct eqml_node *n, const struct placement *p)
{
	int32_t x, y, width, height;
	int text_changed = n->kind == EQML_NODE_TEXT && p->text_changed;

	if (ui->damage.width == ui->frame.width && ui->damage.height == ui->frame.height)
		return;
	if (n->kind == EQML_NODE_TEXT) {
		x = old->data.text.x;
		y = old->data.text.y;
	} else if (n->kind == EQML_NODE_IMAGE) {
		x = old->data.image.x;
		y = old->data.image.y;
	} else {
		x = old->data.rect.x;
		y = old->data.rect.y;
	}
	if (!text_changed && x == p->x && y == p->y &&
	    old->transparency == p->transparency)
		return;
	if (n->kind == EQML_NODE_TEXT) {
		width = text_changed ? p->old_text_width :
		    label_width(n, label(ui, n));
		height = n->pixel_size;
		if (old->transparency != 255)
			include_damage(ui, x, y, width, height);
		if (p->transparency != 255)
			include_damage(ui, p->x, p->y,
			    label_width(n, label(ui, n)), height);
		return;
	} else {
		width = n->width;
		height = n->height;
	}
	if (old->transparency != 255)
		include_damage(ui, x, y, width, height);
	if (p->transparency != 255)
		include_damage(ui, p->x, p->y, width, height);
}

const struct eqml_scene *
eqml_ui_frame(struct eqml_ui *ui)
{
	size_t i, count = 0;

	if (ui == NULL) {
		errno = EINVAL;
		return NULL;
	}
	for (i = 0; i < ui->program->node_count; i++) {
		ui->places[i].ready = 0;
		ui->places[i].visiting = 0;
	}
	for (i = 0; i < ui->program->node_count; i++) {
		const struct eqml_node *n = &ui->program->nodes[i];
		const struct placement *p = &ui->places[i];
		struct eqml_item *item;

		if (place(ui, i) == -1) {
			full_damage(ui);
			clear_text_changes(ui);
			return NULL;
		}
		if (n->kind == EQML_NODE_AREA || n->kind == EQML_NODE_ITEM)
			continue;
		item = &ui->items[count];
		if (count < ui->frame.item_count)
			track_placement(ui, item, n, p);
		count++;
		item->transparency = p->transparency;
		if (n->kind == EQML_NODE_RECT) {
			item->kind = EQML_RECT;
			item->data.rect = (struct eqml_rect){ .x = p->x, .y = p->y,
			    .width = n->width, .height = n->height, .color = n->color };
		} else if (n->kind == EQML_NODE_IMAGE) {
			item->kind = EQML_IMAGE;
			item->data.image = (struct eqml_image){ .x = p->x,
			    .y = p->y, .bitmap = p->bitmap };
		} else {
			item->kind = EQML_TEXT;
			item->data.text = (struct eqml_text){ .binding = -1,
			    .x = p->x, .y = p->y, .prefix = label(ui, n),
			    .pixel_size = n->pixel_size, .glyph_width = (uint8_t)cell_width(n),
			    .color = n->color };
		}
	}
	ui->frame.item_count = count;
	clear_text_changes(ui);
	return &ui->frame;
}

int
eqml_ui_present(struct eqml_ui *ui, eqml_present_fn present, void *cookie)
{
	const struct eqml_scene *frame;
	struct eqml_region pending;

	if (ui == NULL || present == NULL)
		return invalid();
	frame = eqml_ui_frame(ui);
	if (frame == NULL)
		return -1;
	if (ui->damage.width == 0)
		return 0;
	pending = ui->damage;
	ui->damage = (struct eqml_region){ 0 };
	if (present(cookie, frame, &pending) == -1) {
		include_damage(ui, pending.x, pending.y, pending.width, pending.height);
		return -1;
	}
	return 0;
}

static int
copy_text(char *dest, const char *src)
{
	size_t i;
	if (src == NULL)
		return invalid();
	for (i = 0; i < EQML_STRING_CAP && src[i] != 0; ++i) {
		if ((unsigned char)src[i] < 32 || (unsigned char)src[i] > 126) {
			errno = EILSEQ;
			return -1;
		}
	}
	if (i == EQML_STRING_CAP) {
		errno = E2BIG;
		return -1;
	}
	if (dest != NULL)
		memmove(dest, src, i + 1);
	return 0;
}

int
eqml_string_copy(char dest[EQML_STRING_CAP], const char *src)
{
	return dest == NULL ? invalid() : copy_text(dest, src);
}

struct eqml_ui *
eqml_ui_create(const struct eqml_scene *scene,
    void (*logger)(void *, const char *), void *cookie)
{
	struct eqml_ui *ui;
	const struct eqml_program *pg;
	size_t i;

	if (scene == NULL || (pg = scene->program) == NULL || pg->nodes == NULL ||
	    !pg->node_count || pg->node_count > NODE_LIMIT || pg->string_count > NODE_LIMIT ||
	    !scene->width || !scene->height) {
		errno = EINVAL;
		return NULL;
	}
	ui = calloc(1, sizeof(*ui));
	if (ui == NULL)
		return NULL;
	ui->program = pg;
	ui->frame.width = scene->width;
	ui->frame.height = scene->height;
	ui->frame.background = scene->background;
	ui->damage = (struct eqml_region){ 0, 0, scene->width, scene->height };
	ui->grab = ui->hover = ui->last_click = -1;
	ui->logger = logger;
	ui->cookie = cookie;
	ui->places = calloc(pg->node_count, sizeof(*ui->places));
	ui->items = calloc(pg->node_count, sizeof(*ui->items));
	if (pg->string_count)
		ui->strings = calloc(pg->string_count, sizeof(*ui->strings));
	if (ui->places == NULL || ui->items == NULL ||
	    (pg->string_count && ui->strings == NULL))
		goto fail;
	ui->frame.items = ui->items;
	for (i = 0; i < pg->node_count; i++) {
		const struct eqml_node *n = &pg->nodes[i];
		if ((i == 0 ? n->parent != -1 : n->parent < 0 || (size_t)n->parent >= i) ||
		    n->kind < EQML_NODE_RECT || n->kind > EQML_NODE_IMAGE ||
		    (n->kind == EQML_NODE_IMAGE && (!eqml_bitmap_valid(n->bitmap) ||
		    n->width != n->bitmap->width || n->height != n->bitmap->height)) ||
		    (n->anchors & ~(EQML_HCENTER | EQML_VCENTER)) ||
		    n->bottom_edge < EQML_EDGE_NONE || n->bottom_edge > EQML_EDGE_BOTTOM ||
		    (n->bottom_edge && !valid_index(ui, n->bottom_target)) ||
		    (n->kind == EQML_NODE_TEXT && (n->pixel_size < 7 || n->pixel_size > 64 ||
		    n->text_slot < -1 || (n->text_slot >= 0 && (size_t)n->text_slot >= pg->string_count))) ||
		    (n->opacity_area != -1 && (!valid_index(ui, n->opacity_area) ||
		    pg->nodes[n->opacity_area].kind != EQML_NODE_AREA)) ||
		    (n->kind == EQML_NODE_AREA && n->drag_target != -1 &&
		    (!valid_index(ui, n->drag_target) || (n->drag_axes & ~3U) ||
		    n->min_x > n->max_x || n->min_y > n->max_y))) {
			errno = EINVAL;
			goto fail;
		}
		ui->places[i].bitmap = n->bitmap;
		ui->places[i].hidden = n->hidden;
		ui->places[i].local_x = n->x;
		ui->places[i].local_y = n->y;
		if (n->kind == EQML_NODE_TEXT && n->text != NULL &&
		    copy_text(n->text_slot < 0 ? NULL : ui->strings[n->text_slot], n->text) == -1)
			goto fail;
	}
	if (eqml_ui_frame(ui) == NULL)
		goto fail;
	return ui;
fail:
	{
		int error = errno;
		eqml_ui_destroy(ui);
		errno = error;
	}
	return NULL;
}

static void
remove_animation(struct animation **link)
{
	struct animation *a = *link;

	*link = a->next;
	free(a);
}

void
eqml_ui_destroy(struct eqml_ui *ui)
{
	if (ui == NULL)
		return;
	while (ui->animations != NULL)
		remove_animation(&ui->animations);
	free(ui->places);
	free(ui->items);
	free(ui->strings);
	free(ui);
}

static int
valid_coordinate(const struct eqml_ui *ui, size_t node, enum eqml_axis axis)
{
	return ui != NULL && node < ui->program->node_count &&
	    (axis == EQML_AXIS_X || axis == EQML_AXIS_Y);
}

static int
anchored(const struct eqml_node *n, enum eqml_axis axis)
{
	return axis == EQML_AXIS_X ? (n->anchors & EQML_HCENTER) != 0 :
	    (n->anchors & EQML_VCENTER) != 0 || n->bottom_edge != EQML_EDGE_NONE;
}

int32_t
eqml_ui_coordinate(const struct eqml_ui *ui, size_t node, enum eqml_axis axis)
{
	const struct eqml_node *n;
	const struct placement *p, *parent;

	if (!valid_coordinate(ui, node, axis)) {
		errno = EINVAL;
		return 0;
	}
	n = &ui->program->nodes[node];
	p = &ui->places[node];
	if (!anchored(n, axis))
		return axis == EQML_AXIS_X ? p->local_x : p->local_y;
	parent = n->parent >= 0 ? &ui->places[n->parent] : NULL;
	if (axis == EQML_AXIS_X)
		return narrow((int64_t)p->x - (parent != NULL ? parent->x : 0));
	return narrow((int64_t)p->y - (parent != NULL ? parent->y : 0));
}

static struct animation **
animation_for(struct eqml_ui *ui, const int32_t *coordinate)
{
	struct animation **link = &ui->animations;

	while (*link != NULL && (*link)->coordinate != coordinate)
		link = &(*link)->next;
	return link;
}

static void
stop_animation(struct eqml_ui *ui, const int32_t *coordinate)
{
	struct animation **link = animation_for(ui, coordinate);

	if (*link != NULL)
		remove_animation(link);
}

int
eqml_ui_set_coordinate(struct eqml_ui *ui, size_t node, enum eqml_axis axis,
    int32_t target, uint32_t duration)
{
	struct animation **link, *a;
	int32_t *coordinate;

	if (!valid_coordinate(ui, node, axis) || anchored(&ui->program->nodes[node], axis))
		return invalid();
	coordinate = axis == EQML_AXIS_X ? &ui->places[node].local_x : &ui->places[node].local_y;
	link = animation_for(ui, coordinate);
	a = *link;
	if (a != NULL && duration != 0 && a->to == target)
		return 0;
	if (duration == 0 || target == *coordinate) {
		if (a != NULL)
			remove_animation(link);
		if (target == *coordinate)
			return 0;
		*coordinate = target;
		ui->recheck_pointer = ui->pointer_present;
		return eqml_ui_frame(ui) == NULL ? -1 : 0;
	}
	if (a == NULL) {
		a = malloc(sizeof(*a));
		if (a == NULL)
			return -1;
		*link = a;
		a->next = NULL;
		a->coordinate = coordinate;
	}
	a->from = *coordinate;
	a->to = target;
	a->start = ui->now;
	a->duration = duration;
	return 0;
}

int
eqml_ui_visible(const struct eqml_ui *ui, size_t node)
{
	if (ui == NULL || node >= ui->program->node_count) {
		errno = EINVAL;
		return 0;
	}
	return ui->places[node].visible;
}

int
eqml_ui_set_bitmap(struct eqml_ui *ui, size_t node, const struct eqml_bitmap *bitmap)
{
	const struct eqml_node *n;
	struct placement *p;

	if (ui == NULL || node >= ui->program->node_count)
		return invalid();
	n = &ui->program->nodes[node];
	if (n->kind != EQML_NODE_IMAGE)
		return invalid();
	if (bitmap == NULL)
		bitmap = n->bitmap;
	if (!eqml_bitmap_valid(bitmap) || bitmap->width != n->width ||
	    bitmap->height != n->height)
		return invalid();
	p = &ui->places[node];
	p->bitmap = bitmap;
	if (p->transparency != 255)
		include_damage(ui, p->x, p->y, n->width, n->height);
	return 0;
}

int
eqml_ui_set_visible(struct eqml_ui *ui, size_t node, int visible)
{
	if (ui == NULL || node >= ui->program->node_count)
		return invalid();
	if (ui->places[node].hidden == !visible)
		return 0;
	ui->places[node].hidden = !visible;
	if (eqml_ui_frame(ui) == NULL)
		return -1;
	/* A touch-only area loses its implicit hover along with visibility. */
	if (ui->hover >= 0 && !ui->places[ui->hover].visible &&
	    !ui->program->nodes[ui->hover].hover)
		ui->hover = -1;
	ui->recheck_pointer = ui->pointer_present || ui->hover >= 0;
	if (ui->last_click >= 0 && !ui->places[ui->last_click].visible)
		ui->last_click = -1;
	if (ui->grab >= 0 && !ui->places[ui->grab].visible) {
		ui->grab = -1;
		ui->button = 0;
		ui->inside = ui->held = 0;
		/* A visible sibling may bind its opacity to the old grab. */
		if (eqml_ui_frame(ui) == NULL)
			return -1;
	}
	return 0;
}

int
eqml_ui_pressed(const struct eqml_ui *ui, size_t node)
{
	return ui != NULL && ui->grab >= 0 && (size_t)ui->grab == node;
}

int
eqml_ui_contains_press(const struct eqml_ui *ui, size_t node)
{
	return eqml_ui_pressed(ui, node) && ui->inside;
}

const char *
eqml_ui_string(const struct eqml_ui *ui, size_t slot)
{
	if (ui == NULL || slot >= ui->program->string_count) {
		errno = EINVAL;
		return NULL;
	}
	return ui->strings[slot];
}

int
eqml_ui_set(struct eqml_ui *ui, size_t slot, const char *value)
{
	size_t i;
	int nested;
	if (ui == NULL || slot >= ui->program->string_count || value == NULL)
		return invalid();
	if (copy_text(NULL, value) == -1)
		return -1;
	if (strcmp(ui->strings[slot], value) == 0)
		return 0;
	if (ui->notifying >= 16) {
		errno = ELOOP;
		return -1;
	}
	nested = ui->notifying != 0;
	if (eqml_ui_frame(ui) == NULL) {
		full_damage(ui);
		clear_text_changes(ui);
		return -1;
	}
	if (nested) {
		/* Handler code may mutate arbitrary state beyond the current text. */
		full_damage(ui);
	} else {
		for (i = 0; i < ui->program->node_count; ++i) {
			const struct eqml_node *n = &ui->program->nodes[i];
			if (n->kind == EQML_NODE_TEXT && n->text_slot == (int)slot) {
				ui->places[i].old_text_width =
				    label_width(n, label(ui, n));
				ui->places[i].text_changed = 1;
			}
		}
	}
	if (copy_text(ui->strings[slot], value) == -1) {
		clear_text_changes(ui);
		return -1;
	}
	if (eqml_ui_frame(ui) == NULL) {
		full_damage(ui);
		clear_text_changes(ui);
		return -1;
	}
	if (nested)
		full_damage(ui);
	if (ui->program->handler == NULL)
		return 0;
	for (i = 0; i < ui->program->node_count; ++i) {
		const struct eqml_node *n = &ui->program->nodes[i];
		if (n->kind == EQML_NODE_TEXT && n->text_slot == (int)slot) {
			int status;
			ui->notifying++;
			status = ui->program->handler(ui, i, EQML_TEXT_CHANGED, NULL);
			ui->notifying--;
			if (status == -1) {
				full_damage(ui);
				return -1;
			}
		}
	}
	return 0;
}

void
eqml_ui_log(struct eqml_ui *ui, const char *value)
{
	if (ui != NULL && ui->logger != NULL && value != NULL)
		ui->logger(ui->cookie, value);
}

void
eqml_ui_set_signal_handler(struct eqml_ui *ui,
    void (*handler)(void *, const char *), void *cookie)
{
	if (ui != NULL) {
		ui->signal_handler = handler;
		ui->signal_cookie = cookie;
	}
}

void
eqml_ui_emit(struct eqml_ui *ui, const char *name)
{
	if (ui != NULL && ui->signal_handler != NULL && name != NULL)
		ui->signal_handler(ui->signal_cookie, name);
}

struct eqml_point
eqml_ui_map(const struct eqml_ui *ui, size_t from, size_t to, int32_t x, int32_t y)
{
	struct eqml_point p = { 0, 0 };
	if (ui == NULL || from >= ui->program->node_count || to >= ui->program->node_count) {
		errno = EINVAL;
		return p;
	}
	p.x = narrow((int64_t)x + ui->places[from].x - ui->places[to].x);
	p.y = narrow((int64_t)y + ui->places[from].y - ui->places[to].y);
	return p;
}

static int
contains(const struct placement *p, struct eqml_point at)
{
	return at.x >= p->x && at.y >= p->y &&
	    (int64_t)at.x - p->x < p->width && (int64_t)at.y - p->y < p->height;
}

static int
hit(const struct eqml_ui *ui, uint32_t button)
{
	size_t i = ui->program->node_count;
	while (i != 0) {
		const struct eqml_node *n = &ui->program->nodes[--i];
		if (n->kind == EQML_NODE_AREA && ui->places[i].visible &&
		    (button ? (n->buttons & button) != 0 : n->hover) &&
		    contains(&ui->places[i], ui->pointer))
			return (int)i;
	}
	return -1;
}

static int
emit(struct eqml_ui *ui, int node, enum eqml_signal sig, int click)
{
	struct eqml_mouse m;
	const struct placement *p;

	if (node < 0 || ui->program->handler == NULL ||
	    (!ui->places[node].visible && sig != EQML_EXITED && sig != EQML_CLICKED))
		return 0;
	p = &ui->places[node];
	m.x = narrow((int64_t)ui->pointer.x - p->x);
	m.y = narrow((int64_t)ui->pointer.y - p->y);
	m.button = ui->button;
	m.modifiers = ui->modifiers;
	m.was_held = ui->held;
	m.is_click = click;
	return ui->program->handler(ui, (size_t)node, sig, &m);
}

static int
hover(struct eqml_ui *ui, int node)
{
	int previous = ui->hover;

	if (previous == node)
		return 0;
	ui->hover = node;
	if (emit(ui, previous, EQML_EXITED, 0) == -1 ||
	    emit(ui, node, EQML_ENTERED, 0) == -1)
		return -1;
	return 1;
}

static int
far_apart(struct eqml_point a, struct eqml_point b)
{
	return llabs((int64_t)a.x - b.x) > DRAG_DISTANCE ||
	    llabs((int64_t)a.y - b.y) > DRAG_DISTANCE;
}

static int
hold_timeout(const struct eqml_ui *ui, uint64_t now)
{
	uint64_t elapsed;
	if (ui->grab < 0 || ui->hold_done || ui->doubled ||
	    !(ui->program->nodes[ui->grab].signals & (1U << EQML_PRESS_AND_HOLD)))
		return -1;
	elapsed = now >= ui->down_time ? now - ui->down_time : 0;
	return elapsed >= HOLD_MS ? 0 : (int)(HOLD_MS - elapsed);
}

int
eqml_ui_timeout(const struct eqml_ui *ui, uint64_t now)
{
	const struct animation *a;
	uint64_t sampled, next, elapsed;
	int timeout, delay;

	if (ui == NULL)
		return -1;
	if (ui->recheck_pointer)
		return 0;
	if (now < ui->now)
		now = ui->now;
	timeout = hold_timeout(ui, now);
	for (a = ui->animations; a != NULL; a = a->next) {
		sampled = ui->now - a->start;
		next = sampled - sampled % FRAME_MS + FRAME_MS;
		if (next > a->duration)
			next = a->duration;
		elapsed = now - a->start;
		delay = elapsed < next ? (int)(next - elapsed) : 0;
		if (timeout < 0 || delay < timeout)
			timeout = delay;
	}
	return timeout;
}

static int
advance_animations(struct eqml_ui *ui)
{
	struct animation **link = &ui->animations, *a;
	int changed = 0;

	while ((a = *link) != NULL) {
		uint64_t elapsed = ui->now - a->start;
		int32_t value;

		if (elapsed >= a->duration) {
			value = a->to;
		} else {
			int64_t t = (int64_t)elapsed;
			int64_t sum = (a->duration - t) * a->from + t * a->to;
			int64_t half = a->duration / 2U;

			/* The weighted sum and rounding fit int64_t for all int32
			 * coordinates and uint32 durations. */
			value = (int32_t)((sum + (sum < 0 ? -half : half)) / a->duration);
		}
		if (value != *a->coordinate) {
			*a->coordinate = value;
			changed = 1;
		}
		if (elapsed >= a->duration)
			remove_animation(link);
		else
			link = &a->next;
	}
	if (!changed)
		return 0;
	if (eqml_ui_frame(ui) == NULL)
		return -1;
	return 1;
}

/* Geometry can move beneath a stationary pointer between device events. */
static int
update_pointer(struct eqml_ui *ui)
{
	if (ui->grab >= 0) {
		int inside = contains(&ui->places[ui->grab], ui->pointer);

		if (inside != ui->inside) {
			ui->inside = inside;
			return hover(ui, inside ? ui->grab : -1);
		}
	} else if (ui->pointer_present)
		return hover(ui, hit(ui, 0));
	return 0;
}

int
eqml_ui_tick(struct eqml_ui *ui, uint64_t now)
{
	int changed;

	if (ui == NULL)
		return invalid();
	if (now > ui->now)
		ui->now = now;
	changed = advance_animations(ui);
	if (changed == -1)
		return -1;
	if (changed || ui->recheck_pointer) {
		int pointer;

		ui->recheck_pointer = 0;
		pointer = update_pointer(ui);
		if (pointer == -1)
			return -1;
		changed |= pointer;
	}
	if (hold_timeout(ui, ui->now) != 0)
		return changed;
	ui->hold_done = 1;
	if (!ui->inside || ui->dragging)
		return changed;
	ui->held = 1;
	return emit(ui, ui->grab, EQML_PRESS_AND_HOLD, 0) == -1 ? -1 : 1;
}

static int32_t
bounded_position(int32_t origin, int32_t now, int32_t start, int32_t low, int32_t high)
{
	int64_t position = (int64_t)origin + now - start;
	if (position < low)
		return low;
	if (position > high)
		return high;
	return (int32_t)position;
}

static int
move(struct eqml_ui *ui)
{
	const struct eqml_node *area;
	int inside, changed = 0;

	if (ui->grab < 0)
		return hover(ui, hit(ui, 0));
	area = &ui->program->nodes[ui->grab];
	if (area->drag_target >= 0) {
		struct placement *p = &ui->places[area->drag_target];
		int32_t x = p->local_x, y = p->local_y;
		if (area->drag_axes & 1)
			x = bounded_position(ui->drag_origin.x, ui->pointer.x, ui->drag_start.x,
			    area->min_x, area->max_x);
		if (area->drag_axes & 2)
			y = bounded_position(ui->drag_origin.y, ui->pointer.y, ui->drag_start.y,
			    area->min_y, area->max_y);
		changed = x != p->local_x || y != p->local_y;
		if (!ui->moved) {
			if (changed && far_apart(ui->down, ui->pointer)) {
				/* Smoothed drag starts at the threshold crossing. */
				ui->drag_start = ui->pointer;
				ui->moved = 1;
			}
			changed = 0;
		} else {
			ui->dragging = 1;
		}
		if (changed) {
			if (area->drag_axes & 1)
				stop_animation(ui, &p->local_x);
			if (area->drag_axes & 2)
				stop_animation(ui, &p->local_y);
			p->local_x = x;
			p->local_y = y;
			if (eqml_ui_frame(ui) == NULL)
				return -1;
		}
	}
	inside = contains(&ui->places[ui->grab], ui->pointer);
	if (inside != ui->inside) {
		ui->inside = inside;
		return hover(ui, inside ? ui->grab : -1);
	}
	return changed;
}

static int
press(struct eqml_ui *ui, uint32_t button, uint64_t now)
{
	const struct eqml_node *n;
	int target;

	if (ui->grab >= 0 || button == 0)
		return 0;
	target = hit(ui, button);
	if (target < 0) {
		ui->last_click = -1;
		return hover(ui, -1);
	}
	n = &ui->program->nodes[target];
	ui->grab = target;
	ui->down = ui->drag_start = ui->pointer;
	ui->button = button;
	ui->inside = 1;
	ui->moved = ui->held = ui->hold_done = ui->dragging = 0;
	ui->doubled = ui->last_click == target && button == ui->last_button &&
	    now >= ui->last_time && now - ui->last_time <= DOUBLE_MS &&
	    !far_apart(ui->last_point, ui->pointer) &&
	    (n->signals & (1U << EQML_DOUBLE_CLICKED));
	ui->last_click = -1;
	ui->down_time = now;
	if (n->drag_target >= 0) {
		ui->drag_origin.x = ui->places[n->drag_target].local_x;
		ui->drag_origin.y = ui->places[n->drag_target].local_y;
	}
	if (hover(ui, target) == -1 ||
	    emit(ui, target, ui->doubled ? EQML_DOUBLE_CLICKED : EQML_PRESSED, 0) == -1)
		return -1;
	return 1;
}

static int
release(struct eqml_ui *ui, uint32_t button)
{
	int target = ui->grab, click;

	if (target < 0 || button != ui->button)
		return 0;
	if (move(ui) == -1)
		return -1;
	click = ui->inside && !ui->dragging;
	ui->grab = -1;
	ui->inside = 0;
	/* Qt Quick emits released for the second lift, but not clicked. */
	if (emit(ui, target, EQML_RELEASED, click) == -1)
		return -1;
	/* The accepted release owns its click, even if onReleased hides the area. */
	if (click && !ui->held && !ui->doubled) {
		if (emit(ui, target, EQML_CLICKED, 1) == -1)
			return -1;
		ui->last_click = ui->places[target].visible ? target : -1;
		ui->last_button = button;
		ui->last_point = ui->down;
		ui->last_time = ui->down_time;
	}
	ui->button = 0;
	/* After a primary-contact lift the touchscreen has no hover point. */
	return hover(ui, -1) == -1 ? -1 : 1;
}

int
eqml_ui_pointer(struct eqml_ui *ui, enum eqml_phase phase, int32_t x,
    int32_t y, uint32_t button, uint32_t modifiers, uint64_t now)
{
	int timer, changed;

	if (ui == NULL || phase < EQML_DOWN || phase > EQML_CANCEL)
		return invalid();
	if (phase == EQML_CANCEL) {
		changed = ui->grab >= 0 || ui->hover >= 0;
		ui->grab = ui->last_click = -1;
		ui->pointer_present = ui->recheck_pointer = 0;
		ui->inside = ui->held = 0;
		ui->button = 0;
		timer = eqml_ui_tick(ui, now);
		if (timer == -1 || hover(ui, -1) == -1)
			return -1;
		return changed || timer;
	}
	timer = eqml_ui_tick(ui, now);
	if (timer == -1)
		return -1;
	ui->pointer = (struct eqml_point){ x, y };
	ui->modifiers = modifiers;
	ui->pointer_present = phase != EQML_UP || (ui->grab >= 0 && button != ui->button);
	switch (phase) {
	case EQML_DOWN:
		changed = press(ui, button, ui->now);
		break;
	case EQML_UP:
		changed = release(ui, button);
		if (changed == 0 && !ui->pointer_present)
			changed = hover(ui, -1);
		break;
	default:
		changed = move(ui);
		break;
	}
	return changed == 0 ? timer : changed;
}
