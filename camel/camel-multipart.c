/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-multipart.c : Abstract class for a multipart */

#warning This should be a mostly abstract class, but it is not!

/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <config.h>
#include "gmime-content-field.h"
#include "gmime-utils.h"
#include "camel-stream-mem.h"
#include "camel-multipart.h"
#include "camel-mime-part.h"

#define d(x)

static void                  add_part          (CamelMultipart *multipart,
						CamelMimePart *part);
static void                  add_part_at       (CamelMultipart *multipart,
						CamelMimePart *part,
						guint index);
static void                  remove_part       (CamelMultipart *multipart,
						CamelMimePart *part);
static CamelMimePart *       remove_part_at    (CamelMultipart *multipart,
						guint index);
static CamelMimePart *       get_part          (CamelMultipart *multipart,
						guint index);
static guint                 get_number        (CamelMultipart *multipart);
static void                  set_boundary      (CamelMultipart *multipart,
						gchar *boundary);
static const gchar *         get_boundary      (CamelMultipart *multipart);
static int                   write_to_stream   (CamelDataWrapper *data_wrapper,
						CamelStream *stream);
static void                  finalize          (GtkObject *object);

static CamelDataWrapperClass *parent_class = NULL;



/* Returns the class for a CamelMultipart */
#define CMP_CLASS(so) CAMEL_MULTIPART_CLASS (GTK_OBJECT(so)->klass)

/* Returns the class for a CamelDataWrapper */
#define CDW_CLASS(so) CAMEL_DATA_WRAPPER_CLASS (GTK_OBJECT(so)->klass)


static void
camel_multipart_class_init (CamelMultipartClass *camel_multipart_class)
{
	CamelDataWrapperClass *camel_data_wrapper_class =
		CAMEL_DATA_WRAPPER_CLASS (camel_multipart_class);
	GtkObjectClass *gtk_object_class =
		GTK_OBJECT_CLASS (camel_multipart_class);

	parent_class = gtk_type_class (camel_data_wrapper_get_type ());

	/* virtual method definition */
	camel_multipart_class->add_part = add_part;
	camel_multipart_class->add_part_at = add_part_at;
	camel_multipart_class->remove_part = remove_part;
	camel_multipart_class->remove_part_at = remove_part_at;
	camel_multipart_class->get_part = get_part;
	camel_multipart_class->get_number = get_number;
	camel_multipart_class->set_boundary = set_boundary;
	camel_multipart_class->get_boundary = get_boundary;

	/* virtual method overload */
	camel_data_wrapper_class->write_to_stream = write_to_stream;

	gtk_object_class->finalize = finalize;
}

static void
camel_multipart_init (gpointer object, gpointer klass)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (object);

	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (multipart),
					  "multipart/mixed");
	camel_multipart_set_boundary (multipart, "=-=-=-=");
	multipart->preface = NULL;
	multipart->postface = NULL;
}


GtkType
camel_multipart_get_type (void)
{
	static GtkType camel_multipart_type = 0;

	if (!camel_multipart_type) {
		GtkTypeInfo camel_multipart_info =
		{
			"CamelMultipart",
			sizeof (CamelMultipart),
			sizeof (CamelMultipartClass),
			(GtkClassInitFunc) camel_multipart_class_init,
			(GtkObjectInitFunc) camel_multipart_init,
				/* reserved_1 */ NULL,
				/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		camel_multipart_type = gtk_type_unique (camel_data_wrapper_get_type (), &camel_multipart_info);
	}

	return camel_multipart_type;
}

static void
unref_part (gpointer data, gpointer user_data)
{
	GtkObject *part = GTK_OBJECT (data);

	gtk_object_unref (part);
}

static void
finalize (GtkObject *object)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (object);

	g_list_foreach (multipart->parts, unref_part, NULL);

	if (multipart->boundary)
		g_free (multipart->boundary);
	if (multipart->preface)
		g_free (multipart->preface);
	if (multipart->postface)
		g_free (multipart->postface);

	GTK_OBJECT_CLASS (parent_class)->finalize (object);
}



/**
 * camel_multipart_new:
 *
 * Create a new CamelMultipart object.
 *
 * Return value: a new CamelMultipart
 **/
CamelMultipart *
camel_multipart_new (void)
{
	CamelMultipart *multipart;

	multipart = (CamelMultipart *)gtk_type_new (CAMEL_MULTIPART_TYPE);
	multipart->preface = NULL;
	multipart->postface = NULL;

	return multipart;
}


static void
add_part (CamelMultipart *multipart, CamelMimePart *part)
{
	multipart->parts = g_list_append (multipart->parts, part);
	gtk_object_ref (GTK_OBJECT (part));
}

/**
 * camel_multipart_add_part:
 * @multipart: a CamelMultipart
 * @part: the part to add
 *
 * Appends the part to the multipart object.
 **/
void
camel_multipart_add_part (CamelMultipart *multipart, CamelMimePart *part)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (part));

	CMP_CLASS (multipart)->add_part (multipart, part);
}


static void
add_part_at (CamelMultipart *multipart, CamelMimePart *part, guint index)
{
	multipart->parts = g_list_insert (multipart->parts, part, index);
	gtk_object_ref (GTK_OBJECT (part));
}

/**
 * camel_multipart_add_part_at:
 * @multipart: a CamelMultipart
 * @part: the part to add
 * @index: index to add the multipart at
 *
 * Adds the part to the multipart object after the @index'th
 * element. If @index is greater than the number of parts, it is
 * equivalent to camel_multipart_add_part().
 **/
void
camel_multipart_add_part_at (CamelMultipart *multipart,
			     CamelMimePart *part, guint index)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (part));

	CMP_CLASS (multipart)->add_part_at (multipart, part, index);
}


static void
remove_part (CamelMultipart *multipart, CamelMimePart *part)
{
	if (!multipart->parts)
		return;
	multipart->parts = g_list_remove (multipart->parts, part);
	gtk_object_unref (GTK_OBJECT (part));
}

/**
 * camel_multipart_remove_part:
 * @multipart: a CamelMultipart
 * @part: the part to remove
 *
 * Removes @part from @multipart.
 **/
void
camel_multipart_remove_part (CamelMultipart *multipart,
			     CamelMimePart *part)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (part));

	CMP_CLASS (multipart)->remove_part (multipart, part);
}


static CamelMimePart *
remove_part_at (CamelMultipart *multipart, guint index)
{
	GList *parts_list;
	GList *part_to_remove;
	CamelMimePart *removed_part;

	if (!(multipart->parts))
		return NULL;

	parts_list = multipart->parts;
	part_to_remove = g_list_nth (parts_list, index);
	if (!part_to_remove) {
		g_warning ("CamelMultipart::remove_part_at: "
			   "part to remove is NULL\n");
		return NULL;
	}
	removed_part = CAMEL_MIME_PART (part_to_remove->data);

	multipart->parts = g_list_remove_link (parts_list, part_to_remove);
	if (part_to_remove->data)
		gtk_object_unref (GTK_OBJECT (part_to_remove->data));
	g_list_free_1 (part_to_remove);

	return removed_part;
}

/**
 * camel_multipart_remove_part_at:
 * @multipart: a CamelMultipart
 * @index: a zero-based index indicating the part to remove
 *
 * Remove the indicated part from the multipart object.
 *
 * Return value: the removed part. Note that it is gtk_object_unref()ed
 * before being returned, which may cause it to be destroyed.
 **/
CamelMimePart *
camel_multipart_remove_part_at (CamelMultipart *multipart, guint index)
{
	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), NULL);

	return CMP_CLASS (multipart)->remove_part_at (multipart, index);
}


static CamelMimePart *
get_part (CamelMultipart *multipart, guint index)
{
	GList *part;

	if (!(multipart->parts))
		return NULL;

	part = g_list_nth (multipart->parts, index);
	if (part)
		return CAMEL_MIME_PART (part->data);
	else
		return NULL;
}

/**
 * camel_multipart_get_part:
 * @multipart: a CamelMultipart
 * @index: a zero-based index indicating the part to get
 *
 * Return value: the indicated subpart, or %NULL
 **/
CamelMimePart *
camel_multipart_get_part (CamelMultipart *multipart, guint index)
{
	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), NULL);

	return CMP_CLASS (multipart)->get_part (multipart, index);
}


static guint
get_number (CamelMultipart *multipart)
{
	return g_list_length (multipart->parts);
}

/**
 * camel_multipart_get_number:
 * @multipart: a CamelMultipart
 *
 * Return value: the number of subparts in @multipart
 **/
guint
camel_multipart_get_number (CamelMultipart *multipart)
{
	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), 0);

	return CMP_CLASS (multipart)->get_number (multipart);
}


static void
set_boundary (CamelMultipart *multipart, gchar *boundary)
{
	CamelDataWrapper *cdw = CAMEL_DATA_WRAPPER (multipart);

	g_return_if_fail (cdw->mime_type != NULL);

	gmime_content_field_set_parameter (cdw->mime_type, "boundary",
					   boundary);
}

/**
 * camel_multipart_set_boundary:
 * @multipart: a CamelMultipart
 * @boundary: the message boundary
 *
 * Sets the message boundary for @multipart to @boundary. This should
 * be a string which does not occur anywhere in any of @multipart's
 * subparts.
 **/
void
camel_multipart_set_boundary (CamelMultipart *multipart, gchar *boundary)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (boundary != NULL);

	CMP_CLASS (multipart)->set_boundary (multipart, boundary);
}


static const gchar *
get_boundary (CamelMultipart *multipart)
{
	CamelDataWrapper *cdw = CAMEL_DATA_WRAPPER (multipart);

	g_return_val_if_fail (cdw->mime_type != NULL, NULL);
	return gmime_content_field_get_parameter (cdw->mime_type, "boundary");
}

/**
 * camel_multipart_get_boundary:
 * @multipart: a CamelMultipart
 *
 * Return value: @multipart's message boundary
 **/
const gchar *
camel_multipart_get_boundary (CamelMultipart *multipart)
{
	return CMP_CLASS (multipart)->get_boundary (multipart);
}

/* this is MIME specific, doesn't belong here really */
static int
write_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (data_wrapper);
	const gchar *boundary;
	int total = 0, count;
	GList *node;

	/* get the bundary text */
	boundary = camel_multipart_get_boundary (multipart);

	/* we cannot write a multipart without a boundary string */
	g_return_val_if_fail (boundary, -1);

	/*
	 * write the preface text (usually something like
	 *   "This is a mime message, if you see this, then
	 *    your mail client probably doesn't support ...."
	 */
	if (multipart->preface) {
		count = camel_stream_write_strings (stream, multipart->preface, NULL);
		if (count == -1)
			return -1;
		total += count;
	}

	/*
	 * Now, write all the parts, separated by the boundary
	 * delimiter
	 */
	if (boundary==NULL || (boundary[0] == '\0'))
		g_warning ("Multipart boundary is zero length\n");
	
	node = multipart->parts;
	while (node) {
		if ( (count = camel_stream_write_strings (stream, "\n--", boundary?boundary:"", "\n", NULL) ) == -1 )
			return -1;
		total += count;
		if ( (count = camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (node->data), stream)) == -1 )
			return -1;
		total += count;
		node = node->next;
	}

	/* write the terminating boudary delimiter */
	if ( ( count = camel_stream_write_strings (stream, "\n--", boundary, "--\n", NULL) ) == -1 )
		return -1;
	total += count;
	
	/* and finally the postface - it is NOT fatal if this fails */
	if (multipart->postface) {
		if ( ( count = camel_stream_write_strings (stream, multipart->postface, NULL) ) != -1 )
			total += count;
	}

	return total;
}
