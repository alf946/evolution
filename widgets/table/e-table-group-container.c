/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Chris Lahey <clahey@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include <config.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomecanvas/gnome-canvas-rect-ellipse.h>

#include "text/e-text.h"
#include <glib/gi18n.h>
#include "e-util/e-util.h"
#include "misc/e-canvas-utils.h"
#include "misc/e-canvas.h"
#include "misc/e-unicode.h"

#include "e-table-defines.h"
#include "e-table-group-container.h"
#include "e-table-group-leaf.h"
#include "e-table-item.h"

#define TITLE_HEIGHT         16

/* workaround for avoiding API breakage */
#define etgc_get_type e_table_group_container_get_type
G_DEFINE_TYPE (ETableGroupContainer, etgc, E_TABLE_GROUP_TYPE)

/* The arguments we take */
enum {
	PROP_0,
	PROP_HEIGHT,
	PROP_WIDTH,
	PROP_MINIMUM_WIDTH,
	PROP_FROZEN,
	PROP_TABLE_ALTERNATING_ROW_COLORS,
	PROP_TABLE_HORIZONTAL_DRAW_GRID,
	PROP_TABLE_VERTICAL_DRAW_GRID,
	PROP_TABLE_DRAW_FOCUS,
	PROP_CURSOR_MODE,
	PROP_SELECTION_MODEL,
	PROP_LENGTH_THRESHOLD,
	PROP_UNIFORM_ROW_HEIGHT
};

static EPrintable *
etgc_get_printable (ETableGroup *etg);


static void
e_table_group_container_child_node_free (ETableGroupContainer          *etgc,
					ETableGroupContainerChildNode *child_node)
{
	ETableGroup *etg = E_TABLE_GROUP (etgc);
	ETableGroup *child = child_node->child;

	gtk_object_destroy (GTK_OBJECT (child));
	e_table_model_free_value (etg->model, etgc->ecol->col_idx,
				  child_node->key);
	g_free(child_node->string);
	gtk_object_destroy (GTK_OBJECT (child_node->text));
	gtk_object_destroy (GTK_OBJECT (child_node->rect));
}

static void
e_table_group_container_list_free (ETableGroupContainer *etgc)
{
	ETableGroupContainerChildNode *child_node;
	GList *list;

	for (list = etgc->children; list; list = g_list_next (list)) {
		child_node = (ETableGroupContainerChildNode *) list->data;
		e_table_group_container_child_node_free (etgc, child_node);
	}

	g_list_free (etgc->children);
	etgc->children = NULL;
}

static void
etgc_dispose (GObject *object)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER (object);

	if (etgc->children)
		e_table_group_container_list_free (etgc);

	if (etgc->font_desc)
		pango_font_description_free (etgc->font_desc);
	etgc->font_desc = NULL;

	if (etgc->ecol)
		g_object_unref (etgc->ecol);
	etgc->ecol = NULL;

	if (etgc->sort_info)
		g_object_unref (etgc->sort_info);
	etgc->sort_info = NULL;

	if (etgc->selection_model)
		g_object_unref (etgc->selection_model);
	etgc->selection_model = NULL;

	if (etgc->rect)
		gtk_object_destroy (GTK_OBJECT(etgc->rect));
	etgc->rect = NULL;

	G_OBJECT_CLASS (etgc_parent_class)->dispose (object);
}

/**
 * e_table_group_container_construct
 * @parent: The %GnomeCanvasGroup to create a child of.
 * @etgc: The %ETableGroupContainer.
 * @full_header: The full header of the %ETable.
 * @header: The current header of the %ETable.
 * @model: The %ETableModel of the %ETable.
 * @sort_info: The %ETableSortInfo of the %ETable.
 * @n: Which grouping level this is (Starts at 0 and sends n + 1 to any child %ETableGroups.
 *
 * This routine constructs the new %ETableGroupContainer.
 */
void
e_table_group_container_construct (GnomeCanvasGroup *parent, ETableGroupContainer *etgc,
				   ETableHeader *full_header,
				   ETableHeader     *header,
				   ETableModel *model, ETableSortInfo *sort_info, int n)
{
	ETableCol *col;
	ETableSortColumn column = e_table_sort_info_grouping_get_nth(sort_info, n);
	GtkStyle *style;

	col = e_table_header_get_column_by_col_idx(full_header, column.column);
	if (col == NULL)
		col = e_table_header_get_column (full_header, e_table_header_count (full_header) - 1);

	e_table_group_construct (parent, E_TABLE_GROUP (etgc), full_header, header, model);
	etgc->ecol = col;
	g_object_ref (etgc->ecol);
	etgc->sort_info = sort_info;
	g_object_ref (etgc->sort_info);
	etgc->n = n;
	etgc->ascending = column.ascending;

	style = GTK_WIDGET (GNOME_CANVAS_ITEM (etgc)->canvas)->style;
	etgc->font_desc = pango_font_description_copy (style->font_desc);

	etgc->open = TRUE;
}

/**
 * e_table_group_container_new
 * @parent: The %GnomeCanvasGroup to create a child of.
 * @full_header: The full header of the %ETable.
 * @header: The current header of the %ETable.
 * @model: The %ETableModel of the %ETable.
 * @sort_info: The %ETableSortInfo of the %ETable.
 * @n: Which grouping level this is (Starts at 0 and sends n + 1 to any child %ETableGroups.
 *
 * %ETableGroupContainer is an %ETableGroup which groups by the nth
 * grouping of the %ETableSortInfo.  It creates %ETableGroups as
 * children.
 *
 * Returns: The new %ETableGroupContainer.
 */
ETableGroup *
e_table_group_container_new (GnomeCanvasGroup *parent, ETableHeader *full_header,
			     ETableHeader     *header,
			     ETableModel *model, ETableSortInfo *sort_info, int n)
{
	ETableGroupContainer *etgc;

	g_return_val_if_fail (parent != NULL, NULL);

	etgc = g_object_new (E_TABLE_GROUP_CONTAINER_TYPE, NULL);

	e_table_group_container_construct (parent, etgc, full_header, header,
					   model, sort_info, n);
	return E_TABLE_GROUP (etgc);
}


static int
etgc_event (GnomeCanvasItem *item, GdkEvent *event)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(item);
	gboolean return_val = TRUE;
	gboolean change_focus = FALSE;
	gboolean use_col = FALSE;
	gint start_col = 0;
	gint old_col;
	EFocus direction = E_FOCUS_START;

	switch (event->type) {
	case GDK_KEY_PRESS:
		if (event->key.keyval == GDK_Tab ||
		    event->key.keyval == GDK_KP_Tab ||
		    event->key.keyval == GDK_ISO_Left_Tab) {
			change_focus = TRUE;
			use_col      = TRUE;
			start_col    = (event->key.state & GDK_SHIFT_MASK) ? -1 : 0;
			direction    = (event->key.state & GDK_SHIFT_MASK) ? E_FOCUS_END : E_FOCUS_START;
		} else if (event->key.keyval == GDK_Left ||
			   event->key.keyval == GDK_KP_Left) {
			change_focus = TRUE;
			use_col      = TRUE;
			start_col    = -1;
			direction    = E_FOCUS_END;
		} else if (event->key.keyval == GDK_Right ||
			   event->key.keyval == GDK_KP_Right) {
			change_focus = TRUE;
			use_col   = TRUE;
			start_col = 0;
			direction = E_FOCUS_START;
		} else if (event->key.keyval == GDK_Down ||
			   event->key.keyval == GDK_KP_Down) {
			change_focus = TRUE;
			use_col      = FALSE;
			direction    = E_FOCUS_START;
		} else if (event->key.keyval == GDK_Up ||
			   event->key.keyval == GDK_KP_Up) {
			change_focus = TRUE;
			use_col      = FALSE;
			direction    = E_FOCUS_END;
		} else if (event->key.keyval == GDK_Return ||
			   event->key.keyval == GDK_KP_Enter) {
			change_focus = TRUE;
			use_col      = FALSE;
			direction    = E_FOCUS_START;
		}
		if (change_focus){
			GList *list;
			for (list = etgc->children; list; list = list->next) {
				ETableGroupContainerChildNode *child_node;
				ETableGroup                   *child;

				child_node = (ETableGroupContainerChildNode *)list->data;
				child      = child_node->child;

				if (e_table_group_get_focus (child)) {
					old_col = e_table_group_get_focus_column (child);
					if (old_col == -1)
						old_col = 0;
					if (start_col == -1)
						start_col = e_table_header_count (e_table_group_get_header (child)) - 1;

					if (direction == E_FOCUS_END)
						list = list->prev;
					else
						list = list->next;

					if (list) {
						child_node = (ETableGroupContainerChildNode *)list->data;
						child = child_node->child;
						if (use_col)
							e_table_group_set_focus (child, direction, start_col);
						else
							e_table_group_set_focus (child, direction, old_col);
						return 1;
					} else {
						return 0;
					}
				}
			}
			if (direction == E_FOCUS_END)
				list = g_list_last(etgc->children);
			else
				list = etgc->children;
			if (list) {
				ETableGroupContainerChildNode *child_node;
				ETableGroup                   *child;

				child_node = (ETableGroupContainerChildNode *)list->data;
				child = child_node->child;

				if (start_col == -1)
					start_col = e_table_header_count (e_table_group_get_header (child)) - 1;

				e_table_group_set_focus (child, direction, start_col);
				return 1;
			}
		}
		return_val = FALSE;
		break;
	default:
		return_val = FALSE;
		break;
	}
	if (return_val == FALSE) {
		if (GNOME_CANVAS_ITEM_CLASS(etgc_parent_class)->event)
			return GNOME_CANVAS_ITEM_CLASS (etgc_parent_class)->event (item, event);
	}
	return return_val;

}

/* Realize handler for the text item */
static void
etgc_realize (GnomeCanvasItem *item)
{
	ETableGroupContainer *etgc;

	if (GNOME_CANVAS_ITEM_CLASS (etgc_parent_class)->realize)
		(* GNOME_CANVAS_ITEM_CLASS (etgc_parent_class)->realize) (item);

	etgc = E_TABLE_GROUP_CONTAINER (item);

	e_canvas_item_request_reflow (GNOME_CANVAS_ITEM (etgc));
}

/* Unrealize handler for the etgc item */
static void
etgc_unrealize (GnomeCanvasItem *item)
{
	if (GNOME_CANVAS_ITEM_CLASS (etgc_parent_class)->unrealize)
		(* GNOME_CANVAS_ITEM_CLASS (etgc_parent_class)->unrealize) (item);
}

static void
compute_text (ETableGroupContainer *etgc, ETableGroupContainerChildNode *child_node)
{
	gchar *text;

	if (etgc->ecol->text) {
		text = g_strdup_printf (ngettext("%s : %s (%d item)",
						 "%s : %s (%d items)",
						 child_node->count),
					etgc->ecol->text, child_node->string,
					(gint) child_node->count);
	} else {
		text = g_strdup_printf (ngettext("%s (%d item)",
						 "%s (%d items)",
						 child_node->count),
					child_node->string,
					(gint) child_node->count);
	}
	gnome_canvas_item_set (child_node->text,
			       "text", text,
			       NULL);
	g_free (text);
}

static void
child_cursor_change (ETableGroup *etg, int row,
		    ETableGroupContainer *etgc)
{
	e_table_group_cursor_change (E_TABLE_GROUP (etgc), row);
}

static void
child_cursor_activated (ETableGroup *etg, int row,
		    ETableGroupContainer *etgc)
{
	e_table_group_cursor_activated (E_TABLE_GROUP (etgc), row);
}

static void
child_double_click (ETableGroup *etg, int row, int col, GdkEvent *event,
		    ETableGroupContainer *etgc)
{
	e_table_group_double_click (E_TABLE_GROUP (etgc), row, col, event);
}

static gint
child_right_click (ETableGroup *etg, int row, int col, GdkEvent *event,
		   ETableGroupContainer *etgc)
{
	return e_table_group_right_click (E_TABLE_GROUP (etgc), row, col, event);
}

static gint
child_click (ETableGroup *etg, int row, int col, GdkEvent *event,
		   ETableGroupContainer *etgc)
{
	return e_table_group_click (E_TABLE_GROUP (etgc), row, col, event);
}

static gint
child_key_press (ETableGroup *etg, int row, int col, GdkEvent *event,
		 ETableGroupContainer *etgc)
{
	return e_table_group_key_press (E_TABLE_GROUP (etgc), row, col, event);
}

static gint
child_start_drag (ETableGroup *etg, int row, int col, GdkEvent *event,
		 ETableGroupContainer *etgc)
{
	return e_table_group_start_drag (E_TABLE_GROUP (etgc), row, col, event);
}

static ETableGroupContainerChildNode *
create_child_node (ETableGroupContainer *etgc, void *val)
{
	ETableGroup *child;
	ETableGroupContainerChildNode *child_node;
	ETableGroup *etg = E_TABLE_GROUP(etgc);

	child_node = g_new (ETableGroupContainerChildNode, 1);
	child_node->rect = gnome_canvas_item_new (GNOME_CANVAS_GROUP (etgc),
						  gnome_canvas_rect_get_type (),
						  "fill_color", "grey70",
						  "outline_color", "grey50",
						  NULL);
	child_node->text = gnome_canvas_item_new (GNOME_CANVAS_GROUP (etgc),
						  e_text_get_type (),
						  "anchor", GTK_ANCHOR_SW,
						  "fill_color", "black",
						  "draw_background", FALSE,
						  NULL);
	child = e_table_group_new (GNOME_CANVAS_GROUP (etgc), etg->full_header,
				   etg->header, etg->model, etgc->sort_info, etgc->n + 1);
	gnome_canvas_item_set(GNOME_CANVAS_ITEM(child),
			      "alternating_row_colors", etgc->alternating_row_colors,
			      "horizontal_draw_grid", etgc->horizontal_draw_grid,
			      "vertical_draw_grid", etgc->vertical_draw_grid,
			      "drawfocus", etgc->draw_focus,
			      "cursor_mode", etgc->cursor_mode,
			      "selection_model", etgc->selection_model,
			      "length_threshold", etgc->length_threshold,
			      "uniform_row_height", etgc->uniform_row_height,
			      "minimum_width", etgc->minimum_width - GROUP_INDENT,
			      NULL);

	g_signal_connect (child, "cursor_change",
			  G_CALLBACK (child_cursor_change), etgc);
	g_signal_connect (child, "cursor_activated",
			  G_CALLBACK (child_cursor_activated), etgc);
	g_signal_connect (child, "double_click",
			  G_CALLBACK (child_double_click), etgc);
	g_signal_connect (child, "right_click",
			  G_CALLBACK (child_right_click), etgc);
	g_signal_connect (child, "click",
			  G_CALLBACK (child_click), etgc);
	g_signal_connect (child, "key_press",
			  G_CALLBACK (child_key_press), etgc);
	g_signal_connect (child, "start_drag",
			  G_CALLBACK (child_start_drag), etgc);
	child_node->child = child;
	child_node->key = e_table_model_duplicate_value (etg->model, etgc->ecol->col_idx, val);
	child_node->string = e_table_model_value_to_string (etg->model, etgc->ecol->col_idx, val);
	child_node->count = 0;

	return child_node;
}

static void
etgc_add (ETableGroup *etg, gint row)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER (etg);
	void *val = e_table_model_value_at (etg->model, etgc->ecol->col_idx, row);
	GCompareFunc comp = etgc->ecol->compare;
	GList *list = etgc->children;
	ETableGroup *child;
	ETableGroupContainerChildNode *child_node;
	int i = 0;

	for (; list; list = g_list_next (list), i++){
		int comp_val;

		child_node = list->data;
		comp_val = (*comp)(child_node->key, val);
		if (comp_val == 0) {
			child = child_node->child;
			child_node->count ++;
			e_table_group_add (child, row);
			compute_text (etgc, child_node);
			return;
		}
		if ((comp_val > 0 && etgc->ascending) ||
		    (comp_val < 0 && (!etgc->ascending)))
			break;
	}
	child_node = create_child_node (etgc, val);
	child = child_node->child;
	child_node->count = 1;
	e_table_group_add (child, row);

	if (list)
		etgc->children = g_list_insert (etgc->children, child_node, i);
	else
		etgc->children = g_list_append (etgc->children, child_node);

	compute_text (etgc, child_node);
	e_canvas_item_request_reflow (GNOME_CANVAS_ITEM (etgc));
}

static void
etgc_add_array (ETableGroup *etg, const int *array, int count)
{
	int i;
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER (etg);
	void *lastval = NULL;
	int laststart = 0;
	GCompareFunc comp = etgc->ecol->compare;
	ETableGroupContainerChildNode *child_node;
	ETableGroup *child;

	if (count <= 0)
		return;

	e_table_group_container_list_free (etgc);
	etgc->children = NULL;

	lastval = e_table_model_value_at (etg->model, etgc->ecol->col_idx, array[0]);

	for (i = 1; i < count; i++) {
		void *val = e_table_model_value_at (etg->model, etgc->ecol->col_idx, array[i]);
		int comp_val;

		comp_val = (*comp)(lastval, val);
		if (comp_val != 0) {
			child_node = create_child_node(etgc, lastval);
			child = child_node->child;

			e_table_group_add_array(child, array + laststart, i - laststart);
			child_node->count = i - laststart;

			etgc->children = g_list_append (etgc->children, child_node);
			compute_text (etgc, child_node);
			laststart = i;
			lastval = val;
		}
	}

	child_node = create_child_node(etgc, lastval);
	child = child_node->child;

	e_table_group_add_array(child, array + laststart, i - laststart);
	child_node->count = i - laststart;

	etgc->children = g_list_append (etgc->children, child_node);
	compute_text (etgc, child_node);

	e_canvas_item_request_reflow (GNOME_CANVAS_ITEM (etgc));
}

static void
etgc_add_all (ETableGroup *etg)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER (etg);
	ESorter *sorter = etgc->selection_model->sorter;
	int *array;
	int count;

	e_sorter_get_sorted_to_model_array(sorter, &array, &count);

	etgc_add_array(etg, array, count);
}

static gboolean
etgc_remove (ETableGroup *etg, gint row)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);
	GList *list;

	for (list = etgc->children ; list; list = g_list_next (list)) {
		ETableGroupContainerChildNode *child_node = list->data;
		ETableGroup                   *child = child_node->child;

		if (e_table_group_remove (child, row)) {
			child_node->count --;
			if (child_node->count == 0) {
				e_table_group_container_child_node_free (etgc, child_node);
				etgc->children = g_list_remove (etgc->children, child_node);
				g_free (child_node);
			} else
				compute_text (etgc, child_node);

			e_canvas_item_request_reflow (GNOME_CANVAS_ITEM (etgc));

			return TRUE;
		}
	}
	return FALSE;
}

static int
etgc_row_count (ETableGroup *etg)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);
	GList *list;
	gint count = 0;
	for (list = etgc->children; list; list = g_list_next(list)) {
		ETableGroup *group = ((ETableGroupContainerChildNode *)list->data)->child;
		gint this_count = e_table_group_row_count(group);
		count += this_count;
	}
	return count;
}

static void
etgc_increment (ETableGroup *etg, gint position, gint amount)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);
	GList *list = etgc->children;

	for (list = etgc->children ; list; list = g_list_next (list))
		e_table_group_increment (((ETableGroupContainerChildNode *)list->data)->child,
					 position, amount);
}

static void
etgc_decrement (ETableGroup *etg, gint position, gint amount)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);
	GList *list = etgc->children;

	for (list = etgc->children ; list; list = g_list_next (list))
		e_table_group_decrement (((ETableGroupContainerChildNode *)list->data)->child,
					 position, amount);
}

static void
etgc_set_focus (ETableGroup *etg, EFocus direction, gint view_col)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);
	if (etgc->children) {
		if (direction == E_FOCUS_END)
			e_table_group_set_focus (((ETableGroupContainerChildNode *)g_list_last (etgc->children)->data)->child,
						 direction, view_col);
		else
			e_table_group_set_focus (((ETableGroupContainerChildNode *)etgc->children->data)->child,
						 direction, view_col);
	}
}

static gint
etgc_get_focus_column (ETableGroup *etg)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);
	if (etgc->children) {
		GList *list;
		for (list = etgc->children; list; list = list->next) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			ETableGroup *child = child_node->child;
			if (e_table_group_get_focus (child)) {
				return e_table_group_get_focus_column (child);
			}
		}
	}
	return 0;
}

static void
etgc_compute_location (ETableGroup *etg, int *x, int *y, int *row, int *col)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);

	if (row)
		*row = -1;
	if (col)
		*col = -1;

	*x -= GROUP_INDENT;
	*y -= TITLE_HEIGHT;

	if (*x >= 0 && *y >= 0 && etgc->children) {
		GList *list;
		for (list = etgc->children; list; list = list->next) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			ETableGroup *child = child_node->child;

			e_table_group_compute_location (child, x, y, row, col);
			if ((*row != -1) && (*col != -1))
				return;
		}
	}
}

static void
etgc_get_mouse_over (ETableGroup *etg, int *row, int *col)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);

	if (row)
		*row = -1;
	if (col)
		*col = -1;

	if (etgc->children) {
		int row_plus = 0;
		GList *list;

		for (list = etgc->children; list; list = list->next) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			ETableGroup *child = child_node->child;

			e_table_group_get_mouse_over (child, row, col);

			if ((!row || *row != -1) && (!col || *col != -1)) {
				if (row)
					*row += row_plus;
				return;
			}

			row_plus += e_table_group_row_count (child);
		}
	}
}

static void
etgc_get_cell_geometry (ETableGroup *etg, int *row, int *col, int *x, int *y, int *width, int *height)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);

	int ypos;

	ypos = 0;

	if (etgc->children) {
		GList *list;
		for (list = etgc->children; list; list = list->next) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			ETableGroup *child = child_node->child;
			int thisy;

			e_table_group_get_cell_geometry (child, row, col, x, &thisy, width, height);
			ypos += thisy;
			if ((*row == -1) || (*col == -1)) {
				ypos += TITLE_HEIGHT;
				*x += GROUP_INDENT;
				*y = ypos;
				return;
			}
		}
	}
}

static void etgc_thaw (ETableGroup *etg)
{
	e_canvas_item_request_reflow (GNOME_CANVAS_ITEM(etg));
}

static void
etgc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	ETableGroup *etg = E_TABLE_GROUP (object);
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER (object);
	GList *list;

	switch (prop_id) {
	case PROP_FROZEN:
		if (g_value_get_boolean (value))
			etg->frozen = TRUE;
		else {
			etg->frozen = FALSE;
			etgc_thaw (etg);
		}
		break;
	case PROP_MINIMUM_WIDTH:
	case PROP_WIDTH:
		etgc->minimum_width = g_value_get_double (value);

		for (list = etgc->children; list; list = g_list_next (list)) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			g_object_set (child_node->child,
				      "minimum_width", etgc->minimum_width - GROUP_INDENT,
				      NULL);
		}
		break;
	case PROP_LENGTH_THRESHOLD:
		etgc->length_threshold = g_value_get_int (value);
		for (list = etgc->children; list; list = g_list_next (list)) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			g_object_set (child_node->child,
				      "length_threshold", etgc->length_threshold,
				      NULL);
		}
		break;
	case PROP_UNIFORM_ROW_HEIGHT:
		etgc->uniform_row_height = g_value_get_boolean (value);
		for (list = etgc->children; list; list = g_list_next (list)) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			g_object_set (child_node->child,
				      "uniform_row_height", etgc->uniform_row_height,
				      NULL);
		}
		break;

	case PROP_SELECTION_MODEL:
		if (etgc->selection_model)
			g_object_unref (etgc->selection_model);
		etgc->selection_model = E_SELECTION_MODEL(g_value_get_object (value));
		if (etgc->selection_model)
			g_object_ref (etgc->selection_model);
		for (list = etgc->children; list; list = g_list_next (list)) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			g_object_set (child_node->child,
				      "selection_model", etgc->selection_model,
				      NULL);
		}
		break;

	case PROP_TABLE_ALTERNATING_ROW_COLORS:
		etgc->alternating_row_colors = g_value_get_boolean (value);
		for (list = etgc->children; list; list = g_list_next (list)) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			g_object_set (child_node->child,
				      "alternating_row_colors", etgc->alternating_row_colors,
				      NULL);
		}
		break;

	case PROP_TABLE_HORIZONTAL_DRAW_GRID:
		etgc->horizontal_draw_grid = g_value_get_boolean (value);
		for (list = etgc->children; list; list = g_list_next (list)) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			g_object_set (child_node->child,
				      "horizontal_draw_grid", etgc->horizontal_draw_grid,
				      NULL);
		}
		break;

	case PROP_TABLE_VERTICAL_DRAW_GRID:
		etgc->vertical_draw_grid = g_value_get_boolean (value);
		for (list = etgc->children; list; list = g_list_next (list)) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			g_object_set (child_node->child,
				      "vertical_draw_grid", etgc->vertical_draw_grid,
				      NULL);
		}
		break;

	case PROP_TABLE_DRAW_FOCUS:
		etgc->draw_focus = g_value_get_boolean (value);
		for (list = etgc->children; list; list = g_list_next (list)) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			g_object_set (child_node->child,
				      "drawfocus", etgc->draw_focus,
				      NULL);
		}
		break;

	case PROP_CURSOR_MODE:
		etgc->cursor_mode = g_value_get_int (value);
		for (list = etgc->children; list; list = g_list_next (list)) {
			ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *)list->data;
			g_object_set (child_node->child,
				      "cursor_mode", etgc->cursor_mode,
				      NULL);
		}
		break;
	default:
		break;
	}
}

static void
etgc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	ETableGroup *etg = E_TABLE_GROUP (object);
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER (object);

	switch (prop_id) {
	case PROP_FROZEN:
		g_value_set_boolean (value, etg->frozen);
		break;
	case PROP_HEIGHT:
		g_value_set_double (value, etgc->height);
		break;
	case PROP_WIDTH:
		g_value_set_double (value, etgc->width);
		break;
	case PROP_MINIMUM_WIDTH:
		g_value_set_double (value, etgc->minimum_width);
		break;
	case PROP_UNIFORM_ROW_HEIGHT:
		g_value_set_boolean (value, etgc->uniform_row_height);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
etgc_class_init (ETableGroupContainerClass *klass)
{
	GnomeCanvasItemClass *item_class = GNOME_CANVAS_ITEM_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	ETableGroupClass *e_group_class = E_TABLE_GROUP_CLASS (klass);

	object_class->dispose = etgc_dispose;
	object_class->set_property = etgc_set_property;
	object_class->get_property = etgc_get_property;

	item_class->event = etgc_event;
	item_class->realize = etgc_realize;
	item_class->unrealize = etgc_unrealize;

	e_group_class->add = etgc_add;
	e_group_class->add_array = etgc_add_array;
	e_group_class->add_all = etgc_add_all;
	e_group_class->remove = etgc_remove;
	e_group_class->increment  = etgc_increment;
	e_group_class->decrement  = etgc_decrement;
	e_group_class->row_count  = etgc_row_count;
	e_group_class->set_focus  = etgc_set_focus;
	e_group_class->get_focus_column = etgc_get_focus_column;
	e_group_class->get_printable = etgc_get_printable;
	e_group_class->compute_location = etgc_compute_location;
	e_group_class->get_mouse_over = etgc_get_mouse_over;
	e_group_class->get_cell_geometry = etgc_get_cell_geometry;

	g_object_class_install_property (object_class, PROP_TABLE_ALTERNATING_ROW_COLORS,
					 g_param_spec_boolean ("alternating_row_colors",
							       _( "Alternating Row Colors" ),
							       _( "Alternating Row Colors" ),
							       FALSE,
							       G_PARAM_WRITABLE));

	g_object_class_install_property (object_class, PROP_TABLE_HORIZONTAL_DRAW_GRID,
					 g_param_spec_boolean ("horizontal_draw_grid",
							       _( "Horizontal Draw Grid" ),
							       _( "Horizontal Draw Grid" ),
							       FALSE,
							       G_PARAM_WRITABLE));

	g_object_class_install_property (object_class, PROP_TABLE_VERTICAL_DRAW_GRID,
					 g_param_spec_boolean ("vertical_draw_grid",
							       _( "Vertical Draw Grid" ),
							       _( "Vertical Draw Grid" ),
							       FALSE,
							       G_PARAM_WRITABLE));

	g_object_class_install_property (object_class, PROP_TABLE_DRAW_FOCUS,
					 g_param_spec_boolean ("drawfocus",
							       _( "Draw focus" ),
							       _( "Draw focus" ),
							       FALSE,
							       G_PARAM_WRITABLE));

	g_object_class_install_property (object_class, PROP_CURSOR_MODE,
					 g_param_spec_int ("cursor_mode",
							   _( "Cursor mode" ),
							   _( "Cursor mode" ),
							   E_CURSOR_LINE, E_CURSOR_SPREADSHEET, E_CURSOR_LINE,
							   G_PARAM_WRITABLE));

	g_object_class_install_property (object_class, PROP_SELECTION_MODEL,
					 g_param_spec_object ("selection_model",
							      _( "Selection model" ),
							      _( "Selection model" ),
							      E_SELECTION_MODEL_TYPE,
							      G_PARAM_WRITABLE));

	g_object_class_install_property (object_class, PROP_LENGTH_THRESHOLD,
					 g_param_spec_int ("length_threshold",
							   _( "Length Threshold" ),
							   _( "Length Threshold" ),
							   -1, G_MAXINT, 0,
							   G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_UNIFORM_ROW_HEIGHT,
					 g_param_spec_boolean ("uniform_row_height",
							       _( "Uniform row height" ),
							       _( "Uniform row height" ),
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_FROZEN,
					 g_param_spec_boolean ("frozen",
							       _( "Frozen" ),
							       _( "Frozen" ),
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_HEIGHT,
					 g_param_spec_double ("height",
							      _( "Height" ),
							      _( "Height" ),
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_WIDTH,
					 g_param_spec_double ("width",
							      _( "Width" ),
							      _( "Width" ),
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class, PROP_MINIMUM_WIDTH,
					 g_param_spec_double ("minimum_width",
							      _( "Minimum width" ),
							      _( "Minimum Width" ),
							      0.0, G_MAXDOUBLE, 0.0,
							      G_PARAM_READWRITE));
}

static void
etgc_reflow (GnomeCanvasItem *item, gint flags)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(item);
	gboolean frozen;

        g_object_get (etgc,
		      "frozen", &frozen,
		      NULL);

	if (frozen)
		return;


	if (GTK_OBJECT_FLAGS(etgc)& GNOME_CANVAS_ITEM_REALIZED){
		gdouble running_height = 0;
		gdouble running_width = 0;
		gdouble old_height;
		gdouble old_width;

		old_height = etgc->height;
		old_width = etgc->width;
		if (etgc->children == NULL){
		} else {
			GList *list;
			gdouble extra_height = 0;
			gdouble item_height = 0;
			gdouble item_width = 0;

			if (etgc->font_desc) {
				PangoContext *context;
				PangoFontMetrics *metrics;

				context = gtk_widget_get_pango_context (GTK_WIDGET (item->canvas));
				metrics = pango_context_get_metrics (context, etgc->font_desc, NULL);
				extra_height +=
					PANGO_PIXELS (pango_font_metrics_get_ascent (metrics)) +
					PANGO_PIXELS (pango_font_metrics_get_descent (metrics)) +
					BUTTON_PADDING * 2;
				pango_font_metrics_unref (metrics);
			}

			extra_height = MAX(extra_height, BUTTON_HEIGHT + BUTTON_PADDING * 2);

			running_height = extra_height;

			for ( list = etgc->children; list; list = g_list_next (list)){
				ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *) list->data;
				ETableGroup *child = child_node->child;

				g_object_get (child,
					      "width", &item_width,
					      NULL);

				if (item_width > running_width)
					running_width = item_width;
			}
			for ( list = etgc->children; list; list = g_list_next (list)){
				ETableGroupContainerChildNode *child_node = (ETableGroupContainerChildNode *) list->data;
				ETableGroup *child = child_node->child;
				g_object_get (child,
					      "height", &item_height,
					      NULL);

				e_canvas_item_move_absolute (GNOME_CANVAS_ITEM(child_node->text),
							    GROUP_INDENT,
							    running_height - BUTTON_PADDING);

				e_canvas_item_move_absolute (GNOME_CANVAS_ITEM(child),
							    GROUP_INDENT,
							    running_height);

				gnome_canvas_item_set (GNOME_CANVAS_ITEM(child_node->rect),
						      "x1", (double) 0,
						      "x2", (double) running_width + GROUP_INDENT,
						      "y1", (double) running_height - extra_height,
						      "y2", (double) running_height + item_height,
						      NULL);

				running_height += item_height + extra_height;
			}
			running_height -= extra_height;
		}
		if (running_height != old_height || running_width != old_width) {
			etgc->height = running_height;
			etgc->width = running_width;
			e_canvas_item_request_parent_reflow (item);
		}
	}
}

static void
etgc_init (ETableGroupContainer *container)
{
	container->children = NULL;

	e_canvas_item_set_reflow_callback (GNOME_CANVAS_ITEM(container), etgc_reflow);

	container->alternating_row_colors = 1;
	container->horizontal_draw_grid = 1;
	container->vertical_draw_grid = 1;
	container->draw_focus = 1;
	container->cursor_mode = E_CURSOR_SIMPLE;
	container->length_threshold = -1;
	container->selection_model = NULL;
	container->uniform_row_height = FALSE;
}

void
e_table_group_apply_to_leafs (ETableGroup *etg, ETableGroupLeafFn fn, void *closure)
{
	if (E_IS_TABLE_GROUP_CONTAINER (etg)){
		ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER (etg);
		GList *list = etgc->children;

		/* Protect from unrefs in the callback functions */
		g_object_ref (etg);

		for (list = etgc->children; list; list = list->next){
			ETableGroupContainerChildNode *child_node = list->data;

			e_table_group_apply_to_leafs (child_node->child, fn, closure);
		}

		g_object_unref (etg);
	} else if (E_IS_TABLE_GROUP_LEAF (etg)){
		(*fn) (E_TABLE_GROUP_LEAF (etg)->item, closure);
	} else {
		g_error ("Unknown ETableGroup found: %s",
			 g_type_name (G_TYPE_FROM_INSTANCE (etg)));
	}
}


typedef struct {
	ETableGroupContainer *etgc;
	GList *child;
	EPrintable *child_printable;
} ETGCPrintContext;



#define CHECK(x) if((x) == -1) return -1;

#if 0
static gint
gp_draw_rect (GtkPrintContext *context, double x, double y, double width, double height)
{
	cairo_t *cr;
	cr = gtk_print_context_get_cairo_context (context);
	cairo_move_to (cr, x, y);
	cairo_rectangle (cr, x, y, x + width, y + height);
	cairo_fill (cr);
}
#endif

#define TEXT_HEIGHT (12)
#define TEXT_AREA_HEIGHT (TEXT_HEIGHT + 4)

static void
e_table_group_container_print_page  (EPrintable *ep,
				     GtkPrintContext *context,
				     gdouble width,
				     gdouble height,
				     gboolean quantize,
				     ETGCPrintContext *groupcontext)
{
	cairo_t *cr;
	gdouble yd = height;
	gdouble child_height;
	ETableGroupContainerChildNode *child_node;
	GList *child;
	EPrintable *child_printable;
	gchar *string;
	PangoLayout *layout;
	PangoFontDescription *desc;

	child_printable = groupcontext->child_printable;
	child = groupcontext->child;
	yd = 6.5 * 72;
	height = 5 * 72;

	if (child_printable) {
		if (child)
			child_node = child->data;
		else
			child_node = NULL;
		g_object_ref (child_printable);
	} else {
		if (!child) {
			return;
		} else {
			child_node = child->data;
			child_printable = e_table_group_get_printable(child_node->child);
			if (child_printable)
				g_object_ref (child_printable);
			e_printable_reset(child_printable);
		}
	}

	layout = gtk_print_context_create_pango_layout (context);

	desc = pango_font_description_new ();
	pango_font_description_set_family_static (desc, "Helvetica");
	pango_font_description_set_size (desc, TEXT_HEIGHT);
	pango_layout_set_font_description (layout, desc);
	pango_font_description_free (desc);

	while (1) {
		child_height = e_printable_height(child_printable, context, width,yd + 2 * TEXT_AREA_HEIGHT, quantize);
		cr = gtk_print_context_get_cairo_context (context);
		cairo_save (cr);
		cairo_rectangle (cr, 0, 0, width,TEXT_AREA_HEIGHT);
		cairo_rectangle (cr, 0, 0, 2 * TEXT_AREA_HEIGHT, child_height + TEXT_AREA_HEIGHT);
		cairo_set_source_rgb (cr, .7, .7, .7) ;
		cairo_fill(cr) ;
		cairo_restore (cr);

		cairo_save (cr);
		cairo_rectangle (cr, 0, 0, width, TEXT_AREA_HEIGHT);
		cairo_clip (cr);
		cairo_restore (cr);

		cairo_move_to(cr, 0, 0);
		if (groupcontext->etgc->ecol->text)
			string = g_strdup_printf ("%s : %s (%d item%s)",
						  groupcontext->etgc->ecol->text,
						  child_node->string,
						  (gint) child_node->count,
						  child_node->count == 1 ? "" : "s");
		else
			string = g_strdup_printf ("%s (%d item%s)",
						  child_node->string,
						  (gint) child_node->count,
						  child_node->count == 1 ? "" : "s");
		pango_layout_set_text (layout, string, -1);
		pango_cairo_show_layout (cr, layout);
		g_free(string);

		cairo_translate(cr, 2 * TEXT_AREA_HEIGHT, TEXT_AREA_HEIGHT) ;
		cairo_move_to(cr, 0, 0);
		cairo_rectangle (cr, 0, 0, width - 2 * TEXT_AREA_HEIGHT,child_height);
		cairo_clip(cr);

		e_printable_print_page (child_printable, context, width-2 * TEXT_AREA_HEIGHT, 0, quantize);
		yd += child_height + TEXT_AREA_HEIGHT;

		if (e_printable_data_left(child_printable))
			break;

		    child = child->next;
		if (!child) {
			child_printable = NULL;
			break;
		}

		    child_node = child->data;
		if (child_printable)
			g_object_unref (child_printable);
		    child_printable = e_table_group_get_printable(child_node->child);
		if (child_printable)
			g_object_ref (child_printable);
		 e_printable_reset(child_printable);
	}
	if (groupcontext->child_printable)
		g_object_unref (groupcontext->child_printable);
	groupcontext->child_printable = child_printable;
	groupcontext->child = child;

	g_object_unref (layout);
}

static gboolean
e_table_group_container_data_left   (EPrintable *ep,
				     ETGCPrintContext *groupcontext)
{
	g_signal_stop_emission_by_name(ep, "data_left");
	return groupcontext->child != NULL;
}

static void
e_table_group_container_reset       (EPrintable *ep,
				     ETGCPrintContext *groupcontext)
{
	groupcontext->child = groupcontext->etgc->children;
	if (groupcontext->child_printable)
		g_object_unref (groupcontext->child_printable);
	groupcontext->child_printable = NULL;
}

static gdouble
e_table_group_container_height      (EPrintable *ep,
				     GtkPrintContext *context,
				     gdouble width,
				     gdouble max_height,
				     gboolean quantize,
				     ETGCPrintContext *groupcontext)
{
	gdouble height = 0;
	gdouble child_height;
	gdouble yd = max_height;
	ETableGroupContainerChildNode *child_node;
	GList *child;
	EPrintable *child_printable;

	child_printable = groupcontext->child_printable;
	child = groupcontext->child;

	if (child_printable)
		g_object_ref (child_printable);
	else {
		if (!child) {
			g_signal_stop_emission_by_name(ep, "height");
			return 0;
		} else {
			child_node = child->data;
			child_printable = e_table_group_get_printable(child_node->child);
			if (child_printable)
				g_object_ref (child_printable);
			e_printable_reset(child_printable);
		}
	}

	if (yd != -1 && yd < TEXT_AREA_HEIGHT)
		return 0;

	while (1) {
		child_height = e_printable_height(child_printable, context, width - 36, yd - (yd == -1 ? 0 : TEXT_AREA_HEIGHT), quantize);

		height -= child_height + TEXT_AREA_HEIGHT;

		if (yd != -1) {
			if (!e_printable_will_fit(child_printable, context, width - 36, yd - (yd == -1 ? 0 : TEXT_AREA_HEIGHT), quantize)) {
				break;
			}

			yd += child_height + TEXT_AREA_HEIGHT;
		}

		child = child->next;
		if (!child) {
			break;
		}

		child_node = child->data;
		if (child_printable)
			g_object_unref (child_printable);
		child_printable = e_table_group_get_printable(child_node->child);
		if (child_printable)
			g_object_ref (child_printable);
		e_printable_reset(child_printable);
	}
	if (child_printable)
		g_object_unref (child_printable);
	g_signal_stop_emission_by_name(ep, "height");
	return height;
}

static gboolean
e_table_group_container_will_fit      (EPrintable *ep,
				       GtkPrintContext *context,
				       gdouble width,
				       gdouble max_height,
				       gboolean quantize,
				       ETGCPrintContext *groupcontext)
{
	gboolean will_fit = TRUE;
	gdouble child_height;
	gdouble yd = max_height;
	ETableGroupContainerChildNode *child_node;
	GList *child;
	EPrintable *child_printable;

	child_printable = groupcontext->child_printable;
	child = groupcontext->child;

	if (child_printable)
		g_object_ref (child_printable);
	else {
		if (!child) {
			g_signal_stop_emission_by_name(ep, "will_fit");
			return will_fit;
		} else {
			child_node = child->data;
			child_printable = e_table_group_get_printable(child_node->child);
			if (child_printable)
				g_object_ref (child_printable);
			e_printable_reset(child_printable);
		}
	}

	if (yd != -1 && yd < TEXT_AREA_HEIGHT)
		will_fit = FALSE;
	else {
		while (1) {
			child_height = e_printable_height(child_printable, context, width - 36, yd - (yd == -1 ? 0 : TEXT_AREA_HEIGHT), quantize);

			if (yd != -1) {
				if (!e_printable_will_fit(child_printable, context, width - 36, yd - (yd == -1 ? 0 : TEXT_AREA_HEIGHT), quantize)) {
					will_fit = FALSE;
					break;
				}

				yd += child_height + TEXT_AREA_HEIGHT;
			}

			child = child->next;
			if (!child) {
				break;
			}

			child_node = child->data;
			if (child_printable)
				g_object_unref (child_printable);
			child_printable = e_table_group_get_printable(child_node->child);
			if (child_printable)
				g_object_ref (child_printable);
			e_printable_reset(child_printable);
		}
	}

	if (child_printable)
		g_object_unref (child_printable);

	g_signal_stop_emission_by_name(ep, "will_fit");
	return will_fit;
}

static void
e_table_group_container_printable_destroy (gpointer data,
					   GObject *where_object_was)

{
	ETGCPrintContext *groupcontext = data;

	g_object_unref (groupcontext->etgc);
	if (groupcontext->child_printable)
		g_object_ref (groupcontext->child_printable);
	g_free(groupcontext);
}

static EPrintable *
etgc_get_printable (ETableGroup *etg)
{
	ETableGroupContainer *etgc = E_TABLE_GROUP_CONTAINER(etg);
	EPrintable *printable = e_printable_new();
	ETGCPrintContext *groupcontext;

	groupcontext = g_new(ETGCPrintContext, 1);
	groupcontext->etgc = etgc;
	g_object_ref (etgc);
	groupcontext->child = etgc->children;
	groupcontext->child_printable = NULL;

	g_signal_connect (printable,
			  "print_page",
			  G_CALLBACK(e_table_group_container_print_page),
			  groupcontext);
	g_signal_connect (printable,
			  "data_left",
			  G_CALLBACK(e_table_group_container_data_left),
			  groupcontext);
	g_signal_connect (printable,
			  "reset",
			  G_CALLBACK(e_table_group_container_reset),
			  groupcontext);
	g_signal_connect (printable,
			  "height",
			  G_CALLBACK(e_table_group_container_height),
			  groupcontext);
	g_signal_connect (printable,
			  "will_fit",
			  G_CALLBACK(e_table_group_container_will_fit),
			  groupcontext);
	g_object_weak_ref (G_OBJECT (printable),
			   e_table_group_container_printable_destroy,
			   groupcontext);

	return printable;
}
