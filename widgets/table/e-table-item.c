/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * E-table-item.c: A GnomeCanvasItem that is a view of an ETableModel.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 * Copyright 1999, Helix Code, Inc.
 *
 * TODO:
 *   Add a border to the thing, so that focusing works properly.
 *
 */
#include <config.h>
#include <stdio.h>
#include <gtk/gtksignal.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include "e-table-item.h"
#include "e-cell.h"
#include "e-util/e-canvas.h"

#define PARENT_OBJECT_TYPE gnome_canvas_item_get_type ()

#define FOCUSED_BORDER 2

static GnomeCanvasItemClass *eti_parent_class;

enum {
	ROW_SELECTION,
	LAST_SIGNAL
};

static gint eti_signals [LAST_SIGNAL] = { 0, };

enum {
	ARG_0,
	ARG_TABLE_HEADER,
	ARG_TABLE_MODEL,
	ARG_TABLE_DRAW_GRID,
	ARG_TABLE_DRAW_FOCUS,
	ARG_MODE_SPREADSHEET,
	ARG_LENGHT_THRESHOLD,
	
	ARG_WIDTH,
	ARG_HEIGHT,
	ARG_HAS_FOCUS
};

static int eti_get_height (ETableItem *eti);

static gboolean
eti_editing (ETableItem *eti)
{
	if (eti->editing_col == -1)
		return FALSE;
	else
		return TRUE;
}

/*
 * During realization, we have to invoke the per-ecell realize routine
 * (On our current setup, we have one e-cell per column.
 *
 * We might want to optimize this to only realize the unique e-cells:
 * ie, a strings-only table, uses the same e-cell for every column, and
 * we might want to avoid realizing each e-cell.
 */
static void
eti_realize_cell_views (ETableItem *eti)
{
	int i;
	
	for (i = 0; i < eti->n_cells; i++)
		e_cell_realize (eti->cell_views [i]);
	eti->cell_views_realized = 1;
}

static void
eti_attach_cell_views (ETableItem *eti)
{
	int i;

	g_assert (eti->header);
	g_assert (eti->table_model);
	
	/*
	 * Now realize the various ECells
	 */
	eti->n_cells = eti->cols;
	eti->cell_views = g_new (ECellView *, eti->n_cells);

	for (i = 0; i < eti->n_cells; i++){
		ETableCol *col = e_table_header_get_column (eti->header, i);
		
		eti->cell_views [i] = e_cell_new_view (col->ecell, eti->table_model, eti);
	}

	eti->needs_compute_height = 1;
	e_canvas_item_request_reflow (GNOME_CANVAS_ITEM (eti));
	eti->needs_redraw = 1;
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (eti));
}

/*
 * During unrealization: we invoke every e-cell (one per column in the current
 * setup) to dispose all X resources allocated
 */
static void
eti_unrealize_cell_views (ETableItem *eti)
{
	int i;

	if (eti->cell_views_realized == 0)
		return;
	
	for (i = 0; i < eti->n_cells; i++)
		e_cell_unrealize (eti->cell_views [i]);
	eti->cell_views_realized = 0;
}

static void
eti_detach_cell_views (ETableItem *eti)
{
	int i;
	
	for (i = 0; i < eti->n_cells; i++){
		e_cell_kill_view (eti->cell_views [i]);
		eti->cell_views [i] = NULL;
	}
		
	g_free (eti->cell_views);
	eti->cell_views = NULL;
	eti->n_cells = 0;
}

static void
eti_bounds (GnomeCanvasItem *item, double *x1, double *y1, double *x2, double *y2)
{
	double   i2c [6];
	ArtPoint c1, c2, i1, i2;
	ETableItem *eti = E_TABLE_ITEM (item);

	/* Wrong BBox's are the source of redraw nightmares */

	gnome_canvas_item_i2c_affine (GNOME_CANVAS_ITEM (eti), i2c);

	i1.x = eti->x1;
	i1.y = eti->y1;
	i2.x = eti->x1 + eti->width;
	i2.y = eti->y1 + eti->height;
	art_affine_point (&c1, &i1, i2c);
	art_affine_point (&c2, &i2, i2c);
	
	*x1 = c1.x;
	*y1 = c1.y;
	*x2 = c2.x + 1;
	*y2 = c2.y + 1;
}

static void
eti_reflow (GnomeCanvasItem *item, gint flags)
{
	ETableItem *eti = E_TABLE_ITEM (item);

	if (eti->needs_compute_height) {
		int new_height = eti_get_height (eti);

		if (new_height != eti->height) {
			eti->height = new_height;
			e_canvas_item_request_parent_reflow (GNOME_CANVAS_ITEM (eti));
			eti->needs_redraw = 1;
			gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (eti));
		}
		eti->needs_compute_height = 0;
	}
}

/*
 * GnomeCanvasItem::update method
 */
static void
eti_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	ArtPoint o1, o2;
	ETableItem *eti = E_TABLE_ITEM (item);

	if (GNOME_CANVAS_ITEM_CLASS (eti_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS (eti_parent_class)->update)(item, affine, clip_path, flags);


	o1.x = item->x1;
	o1.y = item->y1;
	o2.x = item->x2;
	o2.y = item->y2;

	eti_bounds (item, &item->x1, &item->y1, &item->x2, &item->y2);
	if (item->x1 != o1.x ||
	    item->y1 != o1.y ||
	    item->x2 != o2.x ||
	    item->y2 != o2.y) {
		gnome_canvas_request_redraw (item->canvas, o1.x, o1.y, o2.x, o2.y);
		eti->needs_redraw = 1;
	}

	if (eti->needs_redraw) {
		gnome_canvas_request_redraw (item->canvas, item->x1, item->y1,
					     item->x2, item->y2);
		eti->needs_redraw = 0;
	}
}

/*
 * eti_remove_table_model:
 *
 * Invoked to release the table model associated with this ETableItem
 */
static void
eti_remove_table_model (ETableItem *eti)
{
	if (!eti->table_model)
		return;

	gtk_signal_disconnect (GTK_OBJECT (eti->table_model),
			       eti->table_model_change_id);
	gtk_signal_disconnect (GTK_OBJECT (eti->table_model),
			       eti->table_model_row_change_id);
	gtk_object_unref (GTK_OBJECT (eti->table_model));

	eti->table_model_change_id = 0;
	eti->table_model_row_change_id = 0;
	eti->table_model = NULL;
}

/*
 * eti_remove_header_model:
 *
 * Invoked to release the header model associated with this ETableItem
 */
static void
eti_remove_header_model (ETableItem *eti)
{
	if (!eti->header)
		return;

	gtk_signal_disconnect (GTK_OBJECT (eti->header),
			       eti->header_structure_change_id);
	gtk_signal_disconnect (GTK_OBJECT (eti->header),
			       eti->header_dim_change_id);

	if (eti->cell_views){
		eti_unrealize_cell_views (eti);
		eti_detach_cell_views (eti);
	}
	gtk_object_unref (GTK_OBJECT (eti->header));


	eti->header_structure_change_id = 0;
	eti->header_dim_change_id = 0;
	eti->header = NULL;
}

/*
 * eti_row_height:
 *
 * Returns the height used by row @row.  This does not include the one-pixel
 * used as a separator between rows
 */
static int
eti_row_height (ETableItem *eti, int row)
{
	const int cols = e_table_header_count (eti->header);
	int col;
	int h, max_h;

	g_assert (eti->cell_views);
	
	max_h = 0;
	
	for (col = 0; col < cols; col++){
		ETableCol *ecol = e_table_header_get_column (eti->header, col);
		
		h = e_cell_height (eti->cell_views [col], ecol->col_idx, col, row);

		if (h > max_h)
			max_h = h;
	}
	return max_h;
}

/*
 * eti_get_height:
 *
 * Returns the height of the ETableItem.
 *
 * The ETableItem might compute the whole height by asking every row its
 * size.  There is a special mode (designed to work when there are too
 * many rows in the table that performing the previous step could take 
 * too long) set by the ETableItem->length_threshold that would determine
 * when the height is computed by using the first row as the size for
 * every other row in the ETableItem.
 */
static int
eti_get_height (ETableItem *eti)
{
	const int rows = eti->rows;
	int row;
	int height;

	if (rows == 0)
		return 0;

	if (eti->length_threshold != -1){
		if (rows > eti->length_threshold){
			height = (eti_row_height (eti, 0) + 1) * rows;

			/*
			 * 1 pixel at the top
			 */
			return height + 1;
		}
	}

	height = 1;
	for (row = 0; row < rows; row++)
		height += eti_row_height (eti, row) + 1;

	return height;
}

static void
eti_item_region_redraw (ETableItem *eti, int x0, int y0, int x1, int y1)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (eti);
	ArtDRect rect;
	double i2c [6];
	
	rect.x0 = x0;
	rect.y0 = y0;
	rect.x1 = x1;
	rect.y1 = y1;

	gnome_canvas_item_i2c_affine (item, i2c);
	art_drect_affine_transform (&rect, &rect, i2c);
	
	gnome_canvas_request_redraw (item->canvas, rect.x0, rect.y0, rect.x1, rect.y1);
}

/*
 * Callback routine: invoked when the ETableModel has suffered a change
 */
static void
eti_table_model_changed (ETableModel *table_model, ETableItem *eti)
{
	eti->rows = e_table_model_row_count (eti->table_model);
	
	if (eti->focused_row > eti->rows - 1)
		eti->focused_row = eti->rows - 1;

	eti->needs_compute_height = 1;
	e_canvas_item_request_reflow (GNOME_CANVAS_ITEM (eti));
	eti->needs_redraw = 1;
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (eti));
}

/* Unused. */
#if 0
/*
 * eti_request_redraw:
 *
 * Queues a canvas redraw for the entire ETableItem.
 */
static void
eti_request_redraw (ETableItem *eti)
{
	eti_item_region_redraw (eti, eti->x1, eti->y1, eti->x1 + eti->width + 1, eti->y1 + eti->height + 1);
}
#endif

/*
 * Computes the distance between @start_row and @end_row in pixels
 */
static int
eti_row_diff (ETableItem *eti, int start_row, int end_row)
{
	int row, total;

	total = 0;
	
	for (row = start_row; row < end_row; row++)
		total += eti_row_height (eti, row) + 1;

	return total;
}

/*
 * eti_request_region_redraw:
 *
 * Request a canvas redraw on the range (start_col, start_row) to (end_col, end_row).
 * This is inclusive (ie, you can use: 0,0-0,0 to redraw the first cell).
 *
 * The @border argument is a number of pixels around the region that should also be queued
 * for redraw.   This is typically used by the focus routines to queue a redraw for the
 * border as well.
 */
static void
eti_request_region_redraw (ETableItem *eti,
			   int start_col, int start_row,
			   int end_col, int end_row, int border)
{
	int x1, y1, width, height;
	
	x1 = e_table_header_col_diff (eti->header, 0, start_col);
	y1 = eti_row_diff (eti, 0, start_row);
	width = e_table_header_col_diff (eti->header, start_col, end_col + 1);
	height = eti_row_diff (eti, start_row, end_row + 1);

	eti_item_region_redraw (eti, eti->x1 + x1 - border,
				     eti->y1 + y1 - border,
				     eti->x1 + x1 + width + 1 + border,
				     eti->y1 + y1 + height + 1 + border);
}

static void
eti_table_model_row_changed (ETableModel *table_model, int row, ETableItem *eti)
{
	if (eti->renderers_can_change_size) {
		eti_table_model_changed (table_model, eti);
		return;
	}

	eti_request_region_redraw (eti, 0, row, eti->cols, row, 0);
}

void
e_table_item_redraw_range (ETableItem *eti,
			   int start_col, int start_row,
			   int end_col, int end_row)
{
	int border;
	
	g_return_if_fail (eti != NULL);
	g_return_if_fail (E_IS_TABLE_ITEM (eti));

	if ((start_col == eti->focused_col) ||
	    (end_col   == eti->focused_col) ||
	    (start_row == eti->focused_row) ||
	    (end_row   == eti->focused_row))
		border = 2;
	else
		border = 0;

	eti_request_region_redraw (eti, start_col, start_row, end_col, end_row, border);
}

static void
eti_add_table_model (ETableItem *eti, ETableModel *table_model)
{
	g_assert (eti->table_model == NULL);
	
	eti->table_model = table_model;
	gtk_object_ref (GTK_OBJECT (eti->table_model));
	
	eti->table_model_change_id = gtk_signal_connect (
		GTK_OBJECT (table_model), "model_changed",
		GTK_SIGNAL_FUNC (eti_table_model_changed), eti);

	eti->table_model_row_change_id = gtk_signal_connect (
		GTK_OBJECT (table_model), "model_row_changed",
		GTK_SIGNAL_FUNC (eti_table_model_row_changed), eti);

	if (eti->header) {
		eti_detach_cell_views (eti);
		eti_attach_cell_views (eti);
	}
	
	eti_table_model_changed (table_model, eti);
}

static void
eti_header_dim_changed (ETableHeader *eth, int col, ETableItem *eti)
{
	eti->needs_redraw = 1;
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (eti));
}

static void
eti_header_structure_changed (ETableHeader *eth, ETableItem *eti)
{
	eti->cols = e_table_header_count (eti->header);
	eti->width = e_table_header_total_width (eti->header);

	/*
	 * There should be at least one column
	 */
	g_assert (eti->cols != 0);
	
	if (eti->cell_views){
		eti_unrealize_cell_views (eti);
		eti_detach_cell_views (eti);
		eti_attach_cell_views (eti);
		eti_realize_cell_views (eti);
	} else {
		if (eti->table_model) {
			eti_detach_cell_views (eti);
			eti_attach_cell_views (eti);
		}
	}
	eti->needs_redraw = 1;
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (eti));
}

static void
eti_add_header_model (ETableItem *eti, ETableHeader *header)
{
	g_assert (eti->header == NULL);
	
	eti->header = header;
	gtk_object_ref (GTK_OBJECT (header));

	eti_header_structure_changed (header, eti);
	
	eti->header_dim_change_id = gtk_signal_connect (
		GTK_OBJECT (header), "dimension_change",
		GTK_SIGNAL_FUNC (eti_header_dim_changed), eti);

	eti->header_structure_change_id = gtk_signal_connect (
		GTK_OBJECT (header), "structure_change",
		GTK_SIGNAL_FUNC (eti_header_structure_changed), eti);
}

/*
 * GtkObject::destroy method
 */
static void
eti_destroy (GtkObject *object)
{
	ETableItem *eti = E_TABLE_ITEM (object);

	eti_remove_header_model (eti);
	eti_remove_table_model (eti);

	g_slist_free (eti->selection);
	
	if (GTK_OBJECT_CLASS (eti_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (eti_parent_class)->destroy) (object);
}

static void
eti_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ETableItem *eti;

	item = GNOME_CANVAS_ITEM (o);
	eti = E_TABLE_ITEM (o);

	switch (arg_id){
	case ARG_TABLE_HEADER:
		eti_remove_header_model (eti);
		eti_add_header_model (eti, E_TABLE_HEADER(GTK_VALUE_OBJECT (*arg)));
		break;

	case ARG_TABLE_MODEL:
		eti_remove_table_model (eti);
		eti_add_table_model (eti, E_TABLE_MODEL(GTK_VALUE_OBJECT (*arg)));
		break;
		
	case ARG_LENGHT_THRESHOLD:
		eti->length_threshold = GTK_VALUE_INT (*arg);
		break;

	case ARG_TABLE_DRAW_GRID:
		eti->draw_grid = GTK_VALUE_BOOL (*arg);
		break;

	case ARG_TABLE_DRAW_FOCUS:
		eti->draw_focus = GTK_VALUE_BOOL (*arg);
		break;

	case ARG_MODE_SPREADSHEET:
		eti->mode_spreadsheet = GTK_VALUE_BOOL (*arg);
		break;
	}
	eti->needs_redraw = 1;
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM(eti));
}

static void
eti_get_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ETableItem *eti;

	item = GNOME_CANVAS_ITEM (o);
	eti = E_TABLE_ITEM (o);

	switch (arg_id){
	case ARG_WIDTH:
		GTK_VALUE_DOUBLE (*arg) = eti->width;
		break;
	case ARG_HEIGHT:
		GTK_VALUE_DOUBLE (*arg) = eti->height;
		break;
	default:
		arg->type = GTK_TYPE_INVALID;
	}
}

static void
eti_init (GnomeCanvasItem *item)
{
	ETableItem *eti = E_TABLE_ITEM (item);

	eti->focused_col = -1;
	eti->focused_row = -1;
	eti->editing_col = -1;
	eti->editing_row = -1;
	eti->height = 0;
	
	eti->length_threshold = -1;
	eti->renderers_can_change_size = 1;
	
	eti->selection_mode = GTK_SELECTION_SINGLE;

	eti->needs_redraw = 0;
	eti->needs_compute_height = 0;

	e_canvas_item_set_reflow_callback (GNOME_CANVAS_ITEM (eti), eti_reflow);
}

#define gray50_width 2
#define gray50_height 2
static const char gray50_bits[] = {
	0x02, 0x01, };

static void
eti_realize (GnomeCanvasItem *item)
{
	ETableItem *eti = E_TABLE_ITEM (item);
	GtkWidget *canvas_widget = GTK_WIDGET (item->canvas);
	GdkWindow *window;
	
	if (GNOME_CANVAS_ITEM_CLASS (eti_parent_class)->realize)
                (*GNOME_CANVAS_ITEM_CLASS (eti_parent_class)->realize)(item);

	/*
	 * Gdk Resource allocation
	 */
	window = canvas_widget->window;

	eti->fill_gc = canvas_widget->style->white_gc;
	gdk_gc_ref (canvas_widget->style->white_gc);

	eti->grid_gc = gdk_gc_new (window);
#if 0
	/* This sets it to gray */
/*	gdk_gc_set_foreground (eti->grid_gc, &canvas_widget->style->bg [GTK_STATE_NORMAL]); */
#else
	gdk_gc_set_foreground (eti->grid_gc, &canvas_widget->style->black);
#endif
	eti->focus_gc = gdk_gc_new (window);
	gdk_gc_set_foreground (eti->focus_gc, &canvas_widget->style->bg [GTK_STATE_NORMAL]);
	gdk_gc_set_background (eti->focus_gc, &canvas_widget->style->fg [GTK_STATE_NORMAL]);
	eti->stipple = gdk_bitmap_create_from_data (NULL, gray50_bits, gray50_width, gray50_height);
	gdk_gc_set_ts_origin (eti->focus_gc, 0, 0);
	gdk_gc_set_stipple (eti->focus_gc, eti->stipple);
	gdk_gc_set_fill (eti->focus_gc, GDK_OPAQUE_STIPPLED);

	if (eti->cell_views == NULL)
		eti_attach_cell_views (eti);
	
	eti_realize_cell_views (eti);

	eti->needs_compute_height = 1;
	e_canvas_item_request_reflow (GNOME_CANVAS_ITEM (eti));
	eti->needs_redraw = 1;
	gnome_canvas_item_request_update (GNOME_CANVAS_ITEM (eti));
}

static void
eti_unrealize (GnomeCanvasItem *item)
{
	ETableItem *eti = E_TABLE_ITEM (item);
	
	gdk_gc_unref (eti->fill_gc);
	eti->fill_gc = NULL;
	gdk_gc_unref (eti->grid_gc);
	eti->grid_gc = NULL;
	gdk_gc_unref (eti->focus_gc);
	eti->focus_gc = NULL;
	gdk_bitmap_unref (eti->stipple);
	eti->stipple = NULL;
	
	eti_unrealize_cell_views (eti);

	eti->height = 0;
	
	if (GNOME_CANVAS_ITEM_CLASS (eti_parent_class)->unrealize)
                (*GNOME_CANVAS_ITEM_CLASS (eti_parent_class)->unrealize)(item);
}

static void
eti_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ETableItem *eti = E_TABLE_ITEM (item);
	const int rows = eti->rows;
	const int cols = eti->cols;
	int row, col, y1, y2;
	int first_col, last_col, x_offset;
	int first_row, last_row, y_offset, yd;
	int x1, x2;
	int f_x1, f_x2, f_y1, f_y2;
	gboolean f_found;
	double i2c [6];
	ArtPoint eti_base, eti_base_item;
	
	/*
	 * Clear the background
	 */
#if 0
	gdk_draw_rectangle (
		drawable, eti->fill_gc, TRUE,
		eti->x1 - x, eti->y1 - y, eti->width, eti->height);
#endif

	/*
	 * Find out our real position after grouping
	 */
	gnome_canvas_item_i2c_affine (item, i2c);
	eti_base_item.x = eti->x1;
	eti_base_item.y = eti->y1;
	art_affine_point (&eti_base, &eti_base_item, i2c);

	/*
	 * First column to draw, last column to draw
	 */
	first_col = -1;
	last_col = x_offset = 0;
	x1 = x2 = floor (eti_base.x);
	for (col = 0; col < cols; col++, x1 = x2){
		ETableCol *ecol = e_table_header_get_column (eti->header, col);

		x2 = x1 + ecol->width;
		
		if (x1 > (x + width))
			break;
		if (x2 < x)
			continue;
		if (first_col == -1){
			x_offset = x1 - x;
			first_col = col;
		}
	}
	last_col = col;

	/*
	 * Nothing to paint
	 */
	if (first_col == -1)
		return;

	/*
	 * Compute row span.
	 */
	first_row = -1;
	y_offset = 0;
	y1 = y2 = floor (eti_base.y) + 1;
	for (row = 0; row < rows; row++, y1 = y2){

		y2 += eti_row_height (eti, row) + 1;

		if (y1 > y + height)
			break;

		if (y2 < y)
			continue;

		if (first_row == -1){
			y_offset = y1 - y;
			first_row = row;
		}
	}
	last_row = row;

	if (first_row == -1)
		return;

	/*
	 * Draw cells
	 */
	yd = y_offset;
	f_x1 = f_x2 = f_y1 = f_y2 = -1;
	f_found = FALSE;

	if (eti->draw_grid && first_row == 0){
		gdk_draw_line (
			drawable, eti->grid_gc,
				eti_base.x - x, yd, eti_base.x + eti->width - x, yd);
	}
	yd++;
	
	for (row = first_row; row < last_row; row++){
		int xd, height;
		gboolean selected;
		
		height = eti_row_height (eti, row);

		xd = x_offset;
/*		printf ("paint: %d %d\n", yd, yd + height); */

		selected = g_slist_find (eti->selection, GINT_TO_POINTER (row)) != NULL;
		
		for (col = first_col; col < last_col; col++){
			ETableCol *ecol = e_table_header_get_column (eti->header, col);
			ECellView *ecell_view = eti->cell_views [col];

			e_cell_draw (ecell_view, drawable, ecol->col_idx, col, row, selected,
				     xd, yd, xd + ecol->width, yd + height);
			
			if (col == eti->focused_col && row == eti->focused_row){
				f_x1 = xd;
				f_x2 = xd + ecol->width;
				f_y1 = yd;
				f_y2 = yd + height;
				f_found = TRUE;
			}

			xd += ecol->width;
		}
		yd += height;

		if (eti->draw_grid)
			gdk_draw_line (
				drawable, eti->grid_gc,
				eti_base.x - x, yd, eti_base.x + eti->width - x, yd);
		yd++;
	}

	if (eti->draw_grid){
		int xd = x_offset;
		
		for (col = first_col; col <= last_col; col++){
			ETableCol *ecol = e_table_header_get_column (eti->header, col);

			gdk_draw_line (
				drawable, eti->grid_gc,
				xd, y_offset, xd, yd - 1);

			/*
			 * This looks wierd, but it is to draw the last line
			 */
			if (ecol)
				xd += ecol->width;
		}
	}
	
	/*
	 * Draw focus
	 */
	if (f_found && eti->draw_focus){

		if (!eti_editing (eti))
			gdk_draw_rectangle (
				drawable, eti->focus_gc, FALSE,
				f_x1 + 1, f_y1, f_x2 - f_x1 - 2, f_y2 - f_y1 - 1);
	}
}

static double
eti_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
	   GnomeCanvasItem **actual_item)
{
	*actual_item = item;

	return 0.0;
}

static gboolean
find_cell (ETableItem *eti, double x, double y, int *col_res, int *row_res, double *x1_res, double *y1_res)
{
	const int cols = eti->cols;
	const int rows = eti->rows;
	gdouble x1, y1, x2, y2;
	int col, row;
	
	/* FIXME: this routine is inneficient, fix later */

	x -= eti->x1;
	y -= eti->y1;
	
	x1 = 0;
	for (col = 0; col < cols; col++, x1 = x2){
		ETableCol *ecol = e_table_header_get_column (eti->header, col);

		if (x < x1)
			return FALSE;
		
		x2 = x1 + ecol->width;

		if (x > x2)
			continue;

		*col_res = col;
		if (x1_res)
			*x1_res = x - x1;
		break;
	}

	y1 = y2 = 0;
	for (row = 0; row < rows; row++, y1 = y2){
		if (y < y1)
			return FALSE;
		
		y2 += eti_row_height (eti, row) + 1;

		if (y > y2)
			continue;

		*row_res = row;
		if (y1_res)
			*y1_res = y - y1;
		break;
	}

	return TRUE;
}

static void
eti_cursor_move (ETableItem *eti, gint row, gint column)
{
	e_table_item_leave_edit (eti);
	e_table_item_focus (eti, column, row);
}

static void
eti_cursor_move_left (ETableItem *eti)
{
	eti_cursor_move (eti, eti->focused_row, eti->focused_col - 1);
}

static void
eti_cursor_move_right (ETableItem *eti)
{
	eti_cursor_move (eti, eti->focused_row, eti->focused_col + 1);
}

static void
eti_cursor_move_up (ETableItem *eti)
{
	eti_cursor_move (eti, eti->focused_row - 1, eti->focused_col);
}

static void
eti_cursor_move_down (ETableItem *eti)
{
	eti_cursor_move (eti, eti->focused_row + 1, eti->focused_col);
}

static int
eti_event (GnomeCanvasItem *item, GdkEvent *e)
{
	ETableItem *eti = E_TABLE_ITEM (item);
	ECellView *ecell_view;
	ETableCol *ecol;
	gint return_val = TRUE;

	switch (e->type){
	case GDK_BUTTON_PRESS:
	case GDK_BUTTON_RELEASE:
	case GDK_2BUTTON_PRESS: {
		double x1, y1;
		int col, row;
		
		gnome_canvas_item_w2i (item, &e->button.x, &e->button.y);
		
		if (!find_cell (eti, e->button.x, e->button.y, &col, &row, &x1, &y1))
			return TRUE;

		if (eti->focused_row == row && eti->focused_col == col){

			ecol = e_table_header_get_column (eti->header, col);
			ecell_view = eti->cell_views [col];

			/*
			 * Adjust the event positions
			 */
			e->button.x = x1; 
			e->button.y = y1;
			
			e_cell_event (ecell_view, e, ecol->col_idx, col, row);
		} else {
			/*
			 * Focus the cell, and select the row
			 */
			e_table_item_leave_edit (eti);
			e_table_item_focus (eti, col, row);
			e_table_item_select_row (eti, row);
		}
		break;
	}

	case GDK_MOTION_NOTIFY: {
		int col, row;
		double x1, y1;

		gnome_canvas_item_w2i (item, &e->motion.x, &e->motion.y);

		if (!find_cell (eti, e->motion.x, e->motion.y, &col, &row, &x1, &y1))
			return TRUE;

		if (eti->focused_row == row && eti->focused_col == col){
			ecol = e_table_header_get_column (eti->header, col);
			ecell_view = eti->cell_views [col];

			/*
			 * Adjust the event positions
			 */
			e->motion.x = x1;
			e->motion.y = y1;
			
			e_cell_event (ecell_view, e, ecol->col_idx, col, row);
		}
		break;
	}
		
	case GDK_KEY_PRESS:
		if (eti->focused_col == -1)
			return FALSE;

		switch (e->key.keyval){
		case GDK_Left:
			if (!eti->mode_spreadsheet && eti_editing (eti))
				break;
			
			if (eti->focused_col > 0)
				eti_cursor_move_left (eti);
			break;
			
		case GDK_Right:
			if (!eti->mode_spreadsheet && eti_editing (eti))
				break;
			
			if (eti->focused_col < eti->cols - 1)
				eti_cursor_move_right (eti);
			break;
			
		case GDK_Up:
			if (eti->focused_row > 0)
				eti_cursor_move_up (eti);
			else
				return_val = FALSE;
			break;
			
		case GDK_Down:
			if ((eti->focused_row + 1) < eti->rows)
				eti_cursor_move_down (eti);
			else
				return_val = FALSE;
			break;

		case GDK_Tab:
		case GDK_KP_Tab:
		case GDK_ISO_Left_Tab:
			if ((e->key.state & GDK_SHIFT_MASK) != 0){
				/* shift tab */
				if (eti->focused_col > 0)
					eti_cursor_move_left (eti);
				else if (eti->focused_row > 0)
					eti_cursor_move (eti, eti->focused_row - 1, eti->cols - 1);
				else
					return_val = FALSE;
			} else {
				if (eti->focused_col < eti->cols - 1)
					eti_cursor_move_right (eti);
				else if (eti->focused_row < eti->rows - 1)
					eti_cursor_move (eti, eti->focused_row + 1, 0);
				else 
					return_val = FALSE;
			}
			break;
			
		default:
			if (!eti_editing (eti)){
				if ((e->key.state & (GDK_MOD1_MASK | GDK_CONTROL_MASK)) != 0)
					return_val = FALSE;

				if (!(e->key.keyval >= 0x20 && e->key.keyval <= 0xff))
					return_val = FALSE;
			}

			ecol = e_table_header_get_column (eti->header, eti->focused_col);
			ecell_view = eti->cell_views [eti->focused_col];
			e_cell_event (ecell_view, e, ecol->col_idx, eti->focused_col, eti->focused_row);
		}
		break;
		
	case GDK_KEY_RELEASE:
		if (eti->focused_col == -1)
			return FALSE;

		if (eti_editing (eti)){
			ecell_view = eti->cell_views [eti->editing_col];
			ecol = e_table_header_get_column (eti->header, eti->editing_col);
			e_cell_event (ecell_view, e, ecol->col_idx, eti->editing_col, eti->editing_row);
		}
		break;

	case GDK_FOCUS_CHANGE:
		if (e->focus_change.in) {
		} else {
			e_table_item_leave_edit (eti);
			e_table_item_unfocus (eti);
		}

	default:
		return_val = FALSE;
	}
	return return_val;
}

/*
 * ETableItem::row_selection method
 */
static void
eti_row_selection (ETableItem *eti, int row, gboolean selected)
{
	eti_request_region_redraw (eti, 0, row, eti->cols - 1, row, 0);
	
	if (selected)
		eti->selection = g_slist_prepend (eti->selection, GINT_TO_POINTER (row));
	else
		eti->selection = g_slist_remove (eti->selection, GINT_TO_POINTER (row));
	
}

static void
eti_class_init (GtkObjectClass *object_class)
{
	GnomeCanvasItemClass *item_class = (GnomeCanvasItemClass *) object_class;
	ETableItemClass *eti_class = (ETableItemClass *) object_class;
	
	eti_parent_class = gtk_type_class (PARENT_OBJECT_TYPE);
	
	object_class->destroy = eti_destroy;
	object_class->set_arg = eti_set_arg;
	object_class->get_arg = eti_get_arg;

	item_class->update      = eti_update;
	item_class->realize     = eti_realize;
	item_class->unrealize   = eti_unrealize;
	item_class->draw        = eti_draw;
	item_class->point       = eti_point;
	item_class->event       = eti_event;
	
	eti_class->row_selection = eti_row_selection;

	gtk_object_add_arg_type ("ETableItem::ETableHeader", GTK_TYPE_OBJECT,
				 GTK_ARG_WRITABLE, ARG_TABLE_HEADER);
	gtk_object_add_arg_type ("ETableItem::ETableModel", GTK_TYPE_OBJECT,
				 GTK_ARG_WRITABLE, ARG_TABLE_MODEL);
	gtk_object_add_arg_type ("ETableItem::drawgrid", GTK_TYPE_BOOL,
				 GTK_ARG_WRITABLE, ARG_TABLE_DRAW_GRID);
	gtk_object_add_arg_type ("ETableItem::drawfocus", GTK_TYPE_BOOL,
				 GTK_ARG_WRITABLE, ARG_TABLE_DRAW_FOCUS);
	gtk_object_add_arg_type ("ETableItem::spreadsheet", GTK_TYPE_BOOL,
				 GTK_ARG_WRITABLE, ARG_MODE_SPREADSHEET);

	gtk_object_add_arg_type ("ETableItem::width", GTK_TYPE_DOUBLE, 
				 GTK_ARG_READWRITE, ARG_WIDTH); 
	gtk_object_add_arg_type ("ETableItem::height", GTK_TYPE_DOUBLE, 
				 GTK_ARG_READABLE, ARG_HEIGHT);
	gtk_object_add_arg_type ("ETableItem::has_focus", GTK_TYPE_BOOL,
				 GTK_ARG_READWRITE, ARG_HAS_FOCUS);

	eti_signals [ROW_SELECTION] =
		gtk_signal_new ("row_selection",
				GTK_RUN_LAST,
				object_class->type,
				GTK_SIGNAL_OFFSET (ETableItemClass, row_selection),
				gtk_marshal_NONE__INT_INT,
				GTK_TYPE_NONE, 2, GTK_TYPE_INT, GTK_TYPE_INT);
	
	gtk_object_class_add_signals (object_class, eti_signals, LAST_SIGNAL);

}

GtkType
e_table_item_get_type (void)
{
	static GtkType type = 0;

	if (!type){
		GtkTypeInfo info = {
			"ETableItem",
			sizeof (ETableItem),
			sizeof (ETableItemClass),
			(GtkClassInitFunc) eti_class_init,
			(GtkObjectInitFunc) eti_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		type = gtk_type_unique (PARENT_OBJECT_TYPE, &info);
	}

	return type;
}

void
e_table_item_focus (ETableItem *eti, int col, int row)
{
	g_return_if_fail (eti != NULL);
	g_return_if_fail (E_IS_TABLE_ITEM (eti));
	
	/*
	 * make sure we have the Gtk Focus
	 */
	gnome_canvas_item_grab_focus (GNOME_CANVAS_ITEM (eti));

	if (eti->focused_col != -1)
		e_table_item_unfocus (eti);

	eti->focused_col = col;
	eti->focused_row = row;

	eti_request_region_redraw (eti, col, row, col, row, FOCUSED_BORDER);
}

void
e_table_item_unfocus (ETableItem *eti)
{
	g_return_if_fail (eti != NULL);
	g_return_if_fail (E_IS_TABLE_ITEM (eti));

	if (eti->focused_row == -1)
		return;

	{
		const int col = eti->focused_col;
		const int row = eti->focused_row;
		
		eti_request_region_redraw (eti, col, row, col, row, FOCUSED_BORDER);
		while (eti->selection){
			e_table_item_unselect_row (eti, GPOINTER_TO_INT(eti->selection->data));
		}
	}
	eti->focused_col = -1;
	eti->focused_row = -1;	
}

gint
e_table_item_get_focused_column (ETableItem *eti)
{	
	g_return_val_if_fail (eti != NULL, -1);
	g_return_val_if_fail (E_IS_TABLE_ITEM (eti), -1);

	return eti->focused_col;
}


const GSList *
e_table_item_get_selection (ETableItem *eti)
{
	g_return_val_if_fail (eti != NULL, NULL);
	g_return_val_if_fail (E_IS_TABLE_ITEM (eti), NULL);

	return eti->selection;
}

GtkSelectionMode
e_table_item_get_selection_mode (ETableItem *eti)
{
	g_return_val_if_fail (eti != NULL, GTK_SELECTION_SINGLE);
	g_return_val_if_fail (E_IS_TABLE_ITEM (eti), GTK_SELECTION_SINGLE);

	return eti->selection_mode;
}

void
e_table_item_set_selection_mode (ETableItem *eti, GtkSelectionMode selection_mode)
{
	g_return_if_fail (eti != NULL);
	g_return_if_fail (E_IS_TABLE_ITEM (eti));

	if (selection_mode == GTK_SELECTION_BROWSE ||
	    selection_mode == GTK_SELECTION_EXTENDED){
		g_error ("GTK_SELECTION_BROWSE and GTK_SELECTION_EXTENDED are not implemented");
	}
	
	eti->selection_mode = selection_mode;
}

gboolean
e_table_item_is_row_selected (ETableItem *eti, int row)
{
	g_return_val_if_fail (eti != NULL, FALSE);
	g_return_val_if_fail (E_IS_TABLE_ITEM (eti), FALSE);

	if (g_slist_find (eti->selection, GINT_TO_POINTER (row)))
		return TRUE;
	else
		return FALSE;
}

void
e_table_item_unselect_row (ETableItem *eti, int row)
{
	g_return_if_fail (eti != NULL);
	g_return_if_fail (E_IS_TABLE_ITEM (eti));

	if (e_table_item_is_row_selected (eti, row)){
		gtk_signal_emit (
			GTK_OBJECT (eti), eti_signals [ROW_SELECTION],
			row, 0);
	}
}

void
e_table_item_select_row (ETableItem *eti, int row)
{
	g_return_if_fail (eti != NULL);
	g_return_if_fail (E_IS_TABLE_ITEM (eti));

	switch (eti->selection_mode){
	case GTK_SELECTION_SINGLE:
		if (eti->selection){
			gtk_signal_emit (
				GTK_OBJECT (eti), eti_signals [ROW_SELECTION],
				GPOINTER_TO_INT (eti->selection->data), 0);
		}
		g_slist_free (eti->selection);
		eti->selection = NULL;

		gtk_signal_emit (
			GTK_OBJECT (eti), eti_signals [ROW_SELECTION],
			GINT_TO_POINTER (row), 1);
		break;
				
	case GTK_SELECTION_MULTIPLE:
		if (g_slist_find (eti->selection, GINT_TO_POINTER (row)))
			return;
		gtk_signal_emit (
			GTK_OBJECT (eti), eti_signals [ROW_SELECTION],
			GINT_TO_POINTER (row), 1);
		break;

	default:
		
	}
}

void
e_table_item_enter_edit (ETableItem *eti, int col, int row)
{
	ETableCol *ecol;
	
	g_return_if_fail (eti != NULL);
	g_return_if_fail (E_IS_TABLE_ITEM (eti));

	eti->editing_col = col;
	eti->editing_row = row;

	ecol = e_table_header_get_column (eti->header, col);
	eti->edit_ctx = e_cell_enter_edit (eti->cell_views [col], ecol->col_idx, col, row);
}

void
e_table_item_leave_edit (ETableItem *eti)
{
	ETableCol *ecol;

	g_return_if_fail (eti != NULL);
	g_return_if_fail (E_IS_TABLE_ITEM (eti));

	if (!eti_editing (eti))
		return;

	ecol = e_table_header_get_column (eti->header, eti->editing_col);
	e_cell_leave_edit (
		eti->cell_views [eti->editing_col],
		ecol->col_idx, eti->editing_col,
		eti->editing_row, eti->edit_ctx);
	eti->editing_col = -1;
	eti->editing_row = -1;
	eti->edit_ctx = NULL;
}

