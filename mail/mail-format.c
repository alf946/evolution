/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * Author :
 *  Matt Loper <matt@helixcode.com>
 *
 *  Copyright 2000, Helix Code, Inc. (http://www.helixcode.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>
#include "mail-format.h"
#include "camel/hash-table-utils.h"

#include <libgnome/libgnome.h>
#include <ctype.h>    /* for isprint */
#include <string.h>   /* for strstr  */

static void handle_text_plain           (CamelDataWrapper *wrapper,
			                 GtkHTMLStreamHandle *stream,
					 CamelDataWrapper *root);
static void handle_text_html            (CamelDataWrapper *wrapper,
			                 GtkHTMLStreamHandle *stream,
					 CamelDataWrapper *root);
static void handle_image                (CamelDataWrapper *wrapper,
				         GtkHTMLStreamHandle *stream,
					 CamelDataWrapper *root);
static void handle_vcard                (CamelDataWrapper *wrapper,
				         GtkHTMLStreamHandle *stream,
					 CamelDataWrapper *root);
static void handle_mime_part            (CamelDataWrapper *wrapper,
			                 GtkHTMLStreamHandle *stream,
					 CamelDataWrapper *root);
static void handle_multipart_mixed      (CamelDataWrapper *wrapper,
				         GtkHTMLStreamHandle *stream,
					 CamelDataWrapper *root);
static void handle_multipart_related    (CamelDataWrapper *wrapper,
				         GtkHTMLStreamHandle *stream,
					 CamelDataWrapper *root);
static void handle_multipart_alternative(CamelDataWrapper *wrapper,
				         GtkHTMLStreamHandle *stream,
					 CamelDataWrapper *root);
static void handle_unknown_type         (CamelDataWrapper *wrapper,
				         GtkHTMLStreamHandle *stream,
					 CamelDataWrapper *root);

/* encodes some characters into their 'escaped' version;
 * so '<' turns into '&lt;', and '"' turns into '&quot;' */
static gchar *text_to_html (const guchar *input,
			    guint len,
			    guint *encoded_len_return,
			    gboolean add_pre,
			    gboolean convert_newlines_to_br);

/* writes the header info for a mime message into an html stream */
static void write_header_info_to_stream (CamelMimeMessage* mime_message,
					 GtkHTMLStreamHandle *stream);

/* dispatch html printing via mimetype */
static void call_handler_function (CamelDataWrapper *wrapper,
				   gchar *mimetype_whole, 
				   gchar *mimetype_main,
				   GtkHTMLStreamHandle *stream,
				   CamelDataWrapper *root);

#if 0
/**
 * camel_formatter_wrapper_to_html: 
 * @formatter: the camel formatter object
 * @data_wrapper: the data wrapper
 * @stream: byte stream where data will be written 
 *
 * Writes a CamelDataWrapper out, as html, into a stream passed in as
 * a parameter.
 **/
void camel_formatter_wrapper_to_html (CamelFormatter* formatter,
				      CamelDataWrapper* data_wrapper,
				      CamelStream* stream_out)
{
	CamelFormatterPrivate* fmt = formatter->priv;
	gchar *mimetype_whole =
		g_strdup_printf ("%s/%s",
				 data_wrapper->mime_type->type,
				 data_wrapper->mime_type->subtype);

	debug ("camel_formatter_wrapper_to_html: entered\n");
	g_assert (formatter && data_wrapper && stream_out);

	/* give the root CamelDataWrapper and the stream to the formatter */
	initialize_camel_formatter (formatter, data_wrapper, stream_out);
	
	if (stream_out) {
		
		/* write everything to the stream */
		camel_stream_write_string (
			fmt->stream, "<html><body bgcolor=\"white\">\n");
		call_handler_function (
			formatter,
			data_wrapper,
			mimetype_whole,
			data_wrapper->mime_type->type);
		
		camel_stream_write_string (fmt->stream, "\n</body></html>\n");
	}
	

	g_free (mimetype_whole);
}
#endif

/**
 * mail_format_mime_message: 
 * @mime_message: the input mime message
 * @header_stream: HTML stream to write headers to
 * @body_stream: HTML stream to write data to
 *
 * Writes a CamelMimeMessage out, as html, into streams passed in as
 * a parameter. Either stream may be #NULL.
 **/
void
mail_format_mime_message (CamelMimeMessage *mime_message,
			  GtkHTMLStreamHandle *header_stream,
			  GtkHTMLStreamHandle *body_stream)
{
	g_return_if_fail (CAMEL_IS_MIME_MESSAGE (mime_message));

	/* Write the headers fields out as HTML to the header stream. */
	if (header_stream)
		write_header_info_to_stream (mime_message, header_stream);

	/* Write the contents of the MIME message to the body stream. */
	if (body_stream) {
		mail_write_html (body_stream, "<html><body>\n");
		call_handler_function (CAMEL_DATA_WRAPPER (mime_message),
				       "message/rfc822",
				       "message",
				       body_stream,
				       CAMEL_DATA_WRAPPER (mime_message));
		mail_write_html (body_stream, "\n</body></html>\n");
	}
}

/* We're maintaining a hashtable of mimetypes -> functions;
 * Those functions have the following signature...
 */
typedef void (*mime_handler_fn) (CamelDataWrapper *wrapper,
				 GtkHTMLStreamHandle *stream,
				 CamelDataWrapper *root);

static gchar*
lookup_unique_id (CamelDataWrapper *root, CamelDataWrapper *child)
{
	/* ** FIXME : replace this with a string representing
	   the location of the objetc in the tree */
	/* TODO: assert our return value != NULL */

	gchar *temp_hack_uid;

	temp_hack_uid = g_strdup_printf ("%p", camel_data_wrapper_get_output_stream (child));

	return temp_hack_uid;
}

static GHashTable *mime_function_table;

/* This tries to create a tag, given a mimetype and the child of a
 * mime message. It can return NULL if it can't match the mimetype to
 * a bonobo object.
 */
static gchar *
get_bonobo_tag_for_object (CamelDataWrapper *wrapper,
			   gchar *mimetype, CamelDataWrapper *root)
{
	char *uid = lookup_unique_id (root, wrapper);
	const char *goad_id = gnome_mime_get_value (mimetype,
						    "bonobo-goad-id");

	if (goad_id) {
		return g_strdup_printf ("<object classid=\"%s\"> "
					"<param name=\"uid\" "
					"value=\"camel://%s\"> </object>",
					goad_id, uid);
	} else
		return NULL;
}


/*
 * This takes a mimetype, and tries to map that mimetype to a function
 * or a bonobo object.
 *
 * - If it's mapped to a bonobo object, this function prints a tag
 *   into the stream, designating the bonobo object and a place that
 *   the bonobo object can find data to hydrate from
 *
 * - otherwise, the mimetype is mapped to another function, which can
 *   print into the stream
 */
static void
call_handler_function (CamelDataWrapper *wrapper,
		       gchar *mimetype_whole_in, /* ex. "image/jpeg" */
		       gchar *mimetype_main_in, /* ex. "image" */
		       GtkHTMLStreamHandle *stream,
		       CamelDataWrapper *root)
{
	mime_handler_fn handler_function = NULL;
	gchar *mimetype_whole = NULL;
	gchar *mimetype_main = NULL;

	g_return_if_fail (mimetype_whole_in || mimetype_main_in);
	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (wrapper));
	g_return_if_fail (CAMEL_IS_DATA_WRAPPER (root));

	if (mime_function_table == NULL) {
		mime_function_table = g_hash_table_new (g_strcase_hash,
							g_strcase_equal);

		/* hook up mime types to functions that handle them */
		g_hash_table_insert (mime_function_table, "text/plain",
				     handle_text_plain);
		g_hash_table_insert (mime_function_table, "text/richtext",
				     handle_text_plain);
		g_hash_table_insert (mime_function_table, "text/html",
				     handle_text_html);
		g_hash_table_insert (mime_function_table, "multipart/alternative",
				     handle_multipart_alternative);
		g_hash_table_insert (mime_function_table, "multipart/related",
				     handle_multipart_related);
		g_hash_table_insert (mime_function_table, "multipart/mixed",
				     handle_multipart_mixed);
		g_hash_table_insert (mime_function_table, "message/rfc822",
				     handle_mime_part);
		g_hash_table_insert (mime_function_table, "image",
				     handle_image);
		g_hash_table_insert (mime_function_table, "vcard",
				     handle_vcard);

		/* RFC 2046 says unrecognized multipart subtypes should
		 * be treated like multipart/mixed.
		 */
		g_hash_table_insert (mime_function_table, "multipart",
				     handle_multipart_mixed);

		/* Body parts don't have mime parts per se, so Camel
		 * sticks on the following one.
		 */
		g_hash_table_insert (mime_function_table, "mime/body-part",
				     handle_mime_part);
	}

	/* Try to find a handler function in our own lookup table */
	if (mimetype_whole_in) {
		mimetype_whole = g_strdup (mimetype_whole_in);
		g_strdown (mimetype_whole);

		handler_function = g_hash_table_lookup (mime_function_table,
							mimetype_whole);
	}

	if (mimetype_main_in && !handler_function) {
		mimetype_main = g_strdup (mimetype_main_in);
		g_strdown (mimetype_main);

		handler_function = g_hash_table_lookup (mime_function_table,
							mimetype_main);
	}

	/* Upon failure, try to find a bonobo object to show the object */
	if (!handler_function) {
		gchar *bonobo_tag = NULL;

		if (mimetype_whole)
			bonobo_tag = get_bonobo_tag_for_object (
				wrapper, mimetype_whole, root);

		if (mimetype_main && !bonobo_tag)
			bonobo_tag = get_bonobo_tag_for_object (
				wrapper, mimetype_main, root);

		if (bonobo_tag) {
			/* We can print a tag, and return! */

			mail_write_html (stream, bonobo_tag);
			g_free (bonobo_tag);
			if (mimetype_whole)
				g_free (mimetype_whole);
			if (mimetype_main)
				g_free (mimetype_main);

			return; 
		}
	}

	/* Use either a handler function we've found, or a default handler. */
	if (handler_function)
		(*handler_function) (wrapper, stream, root);
	else
		handle_unknown_type (wrapper, stream, root);
	if (mimetype_whole)
		g_free (mimetype_whole);
	if (mimetype_main)
		g_free (mimetype_main);
}


/* Convert plain text in equivalent-looking valid HTML. */
static gchar *
text_to_html (const guchar *input, guint len,
	      guint *encoded_len_return, gboolean add_pre,
	      gboolean convert_newlines_to_br)
{
	const guchar *cur = input;
	guchar *buffer = NULL;
	guchar *out = NULL;
	gint buffer_size = 0;

	/* Allocate a translation buffer.  */
	buffer_size = len * 2 + 5;
	buffer = g_malloc (buffer_size);

	out = buffer;
	if (add_pre)
		out += sprintf (out, "<PRE>\n");

	while (len--) {
		if (out - buffer > buffer_size - 100) {
			gint index = out - buffer;

			buffer_size *= 2;
			buffer = g_realloc (buffer, buffer_size);
			out = buffer + index;
		}

		switch (*cur) {
		case '<':
			strcpy (out, "&lt;");
			out += 4;
			break;

		case '>':
			strcpy (out, "&gt;");
			out += 4;
			break;

		case '&':
			strcpy (out, "&amp;");
			out += 5;
			break;

		case '"':
			strcpy (out, "&quot;");
			out += 6;
			break;

		case '\n':
			*out++ = *cur;
			if (convert_newlines_to_br) {
				strcpy (out, "<br>");
				out += 4;
			}
			break;

		default:
			if ((*cur >= 0x20 && *cur < 0x80) ||
			    (*cur == '\r' || *cur == '\t')) {
				/* Default case, just copy. */
				*out++ = *cur;
			} else
				out += g_snprintf(out, 9, "&#%d;", *cur);
			break;
		}

		cur++;
	}

	if (add_pre)
		strcpy (out, "</PRE>");
	else
		*out = '\0';
	if (encoded_len_return)
		*encoded_len_return = out - buffer;

	return buffer;
}


static void
write_field_to_stream (const gchar *description, const gchar *value,
		       gboolean bold, GtkHTMLStreamHandle *stream)
{
	gchar *s;
	gchar *encoded_value;

	if (value) {
		unsigned char *p;

		encoded_value = text_to_html (value, strlen(value),
					      NULL, FALSE, TRUE);
		for (p = (unsigned char *)encoded_value; *p; p++) {
			if (!isprint (*p))
				*p = '?';
		}
	} else
		encoded_value = g_strdup ("");

	s = g_strdup_printf ("<tr valign=top><%s align=right>%s</%s>"
			     "<td>%s</td></tr>", bold ? "th" : "td",
			     description, bold ? "th" : "td",
			     encoded_value);
	mail_write_html (stream, s);
	g_free (encoded_value);
	g_free (s);
}

static void
write_recipients_to_stream (const gchar *recipient_type,
			    const GList *recipients, gboolean bold,
			    GtkHTMLStreamHandle *stream)
{
	gchar *recipients_string = NULL;

 	while (recipients) {
		gchar *old_string = recipients_string;
		recipients_string =
			g_strdup_printf ("%s%s%s",
					 old_string ? old_string : "",
					 old_string ? "; " : "",
					 (gchar *)recipients->data);
		g_free (old_string);

		recipients = recipients->next;
	}

	write_field_to_stream (recipient_type, recipients_string,
			       bold, stream);
	g_free (recipients_string);
}



static void
write_header_info_to_stream (CamelMimeMessage *mime_message,
			     GtkHTMLStreamHandle *stream)
{
	const GList *recipients;

	mail_write_html (stream, "<table>");

	/* A few fields will probably be available from the mime_message;
	 * for each one that's available, write it to the output stream
	 * with a helper function, 'write_field_to_stream'.
	 */

	write_field_to_stream ("From:",
			       camel_mime_message_get_from (mime_message),
			       TRUE, stream);

	if (camel_mime_message_get_reply_to (mime_message)) {
		write_field_to_stream ("Reply-To:",
				       camel_mime_message_get_reply_to (mime_message),
				       FALSE, stream);
	}

	write_recipients_to_stream ("To:",
				    camel_mime_message_get_recipients (mime_message, CAMEL_RECIPIENT_TYPE_TO),
				    TRUE, stream);

	recipients = camel_mime_message_get_recipients (mime_message, CAMEL_RECIPIENT_TYPE_CC);
	if (recipients)
		write_recipients_to_stream ("Cc:", recipients, TRUE, stream);
	write_field_to_stream ("Subject:",
			       camel_mime_message_get_subject (mime_message),
			       TRUE, stream);

	mail_write_html (stream, "</table>");	
}

#define MIME_TYPE_WHOLE(a)  (gmime_content_field_get_mime_type ( \
                                      camel_mime_part_get_content_type (CAMEL_MIME_PART (a))))
#define MIME_TYPE_MAIN(a)  ((camel_mime_part_get_content_type (CAMEL_MIME_PART (a)))->type)
#define MIME_TYPE_SUB(a)   ((camel_mime_part_get_content_type (CAMEL_MIME_PART (a)))->subtype)


/*----------------------------------------------------------------------*
 *                     Mime handling functions
 *----------------------------------------------------------------------*/

static void
handle_text_plain (CamelDataWrapper *wrapper, GtkHTMLStreamHandle *stream,
		   CamelDataWrapper *root)
{
	gchar *text;
	CamelStream *wrapper_output_stream;
	gchar tmp_buffer[4096];
	gint nb_bytes_read;
	gboolean empty_text = TRUE;

	
	mail_write_html (stream, "\n<!-- text/plain below -->\n");
	mail_write_html (stream, "<pre>\n");

	/* FIXME: text/richtext is not difficult to translate into HTML */
	if (strcmp (wrapper->mime_type->subtype, "richtext") == 0) {
		mail_write_html (stream, "<center><b>"
				 "<table bgcolor=\"b0b0ff\" cellpadding=3>"
				 "<tr><td>Warning: the following "
				 "richtext may not be formatted correctly. "
				 "</b></td></tr></table></center><br>");
	}

	/* Get the output stream of the data wrapper. */
	wrapper_output_stream = camel_data_wrapper_get_output_stream (wrapper);
	camel_stream_reset (wrapper_output_stream);

	do {
		/* Read next chunk of text. */
		nb_bytes_read = camel_stream_read (wrapper_output_stream,
						   tmp_buffer, 4096);

		/* If there's any text, write it to the stream. */
		if (nb_bytes_read > 0) {
			int returned_strlen;

			empty_text = FALSE;

			/* replace '<' with '&lt;', etc. */
			text = text_to_html (tmp_buffer,
					     nb_bytes_read,
					     &returned_strlen,
					     FALSE, FALSE);
			mail_write_html (stream, text);
			g_free (text);
		}
	} while (!camel_stream_eos (wrapper_output_stream));

	if (empty_text)
		mail_write_html (stream, "<b>(empty)</b>");

	mail_write_html (stream, "</pre>\n");	
}

static void
handle_text_html (CamelDataWrapper *wrapper, GtkHTMLStreamHandle *stream,
		  CamelDataWrapper *root)
{
	CamelStream *wrapper_output_stream;
	gchar tmp_buffer[4096];
	gint nb_bytes_read;
	gboolean empty_text = TRUE;

	/* Get the output stream of the data wrapper. */
	wrapper_output_stream = camel_data_wrapper_get_output_stream (wrapper);
	camel_stream_reset (wrapper_output_stream);

	/* Write the header. */
	mail_write_html (stream, "\n<!-- text/html below -->\n");

	do {
		/* Read next chunk of text. */
		nb_bytes_read = camel_stream_read (wrapper_output_stream,
						   tmp_buffer, 4096);

		/* If there's any text, write it to the stream */
		if (nb_bytes_read > 0) {
			empty_text = FALSE;
			
			/* Write the buffer to the html stream */
			gtk_html_stream_write (stream, tmp_buffer,
					       nb_bytes_read);
		}
	} while (!camel_stream_eos (wrapper_output_stream));

	if (empty_text)
		mail_write_html (stream, "<b>(empty)</b>");
}

static void
handle_image (CamelDataWrapper *wrapper, GtkHTMLStreamHandle *stream,
	      CamelDataWrapper *root)
{
	gchar *uuid;
	gchar *tag;

	uuid = lookup_unique_id (root, wrapper);

	mail_write_html (stream, "\n<!-- image below -->\n");
	tag = g_strdup_printf ("<img src=\"camel://%s\">\n", uuid);
	mail_write_html (stream, tag);
	g_free (uuid);
	g_free (tag);		
}

static void
handle_vcard (CamelDataWrapper *wrapper, GtkHTMLStreamHandle *stream,
	      CamelDataWrapper *root)
{
	mail_write_html (stream, "\n<!-- vcard below -->\n");

	/* FIXME: do something here. */
}

static void
handle_mime_part (CamelDataWrapper *wrapper, GtkHTMLStreamHandle *stream,
		  CamelDataWrapper *root)
{
	CamelMimePart *mime_part; 
	CamelDataWrapper *message_contents; 
	gchar *whole_mime_type;

	g_return_if_fail (CAMEL_IS_MIME_PART (wrapper));

	mime_part = CAMEL_MIME_PART (wrapper);
	message_contents =
		camel_medium_get_content_object (CAMEL_MEDIUM (mime_part));

	g_assert (message_contents);

	mail_write_html (stream, "\n<!-- mime message below -->\n");

//	mail_write_html (stream,
//				   "<table width=95% border=1><tr><td>\n\n");		

	/* dispatch the correct handler function for the mime type */
	whole_mime_type = MIME_TYPE_WHOLE (mime_part);
	call_handler_function (message_contents,
			       whole_mime_type,
			       MIME_TYPE_MAIN (mime_part),
			       stream, root);
	g_free (whole_mime_type);

	/* close up the table we opened */
//	mail_write_html (stream,
//				   "\n\n</td></tr></table>\n\n");
}


/* called for each body part in a multipart/mixed */
static void
display_camel_body_part (CamelMimeBodyPart *body_part,
			 GtkHTMLStreamHandle *stream,
			 CamelDataWrapper *root)
{
	gchar *whole_mime_type;

	CamelDataWrapper* contents =
		camel_medium_get_content_object (CAMEL_MEDIUM (body_part));

	whole_mime_type = MIME_TYPE_WHOLE (body_part);
	call_handler_function (contents, whole_mime_type,
			       MIME_TYPE_MAIN (body_part),
			       stream, root);
	g_free (whole_mime_type);

	mail_write_html (stream, "\n<hr>\n");
}


/* Our policy here is this:
   (1) print text/(plain|html) parts found
   (2) print vcards and images inline
   (3) treat all other parts as attachments */
static void
handle_multipart_mixed (CamelDataWrapper *wrapper,
			GtkHTMLStreamHandle *stream, CamelDataWrapper *root)
{
	CamelMultipart *mp;
	int i, nparts;

	g_return_if_fail (CAMEL_IS_MULTIPART (wrapper));

	mp = CAMEL_MULTIPART (wrapper);

	nparts = camel_multipart_get_number (mp);	
	for (i = 0; i < nparts; i++) {
		CamelMimeBodyPart *body_part =
			camel_multipart_get_part (mp, i);

		display_camel_body_part (body_part, stream, root);
	}
}

/* As seen in RFC 2387! */
/* FIXME: camel doesn't currently set CamelMultipart::parent, so we
 * can use it here.
 */
static void
handle_multipart_related (CamelDataWrapper *wrapper,
			  GtkHTMLStreamHandle *stream,
			  CamelDataWrapper *root)
{
	CamelMultipart *mp;
	CamelMimeBodyPart *body_part;
#if 0
	GMimeContentField *parent_content_type;
	const char *start, *cid;
	int i, nparts;
#endif

	g_return_if_fail (CAMEL_IS_MULTIPART (wrapper));

	mp = CAMEL_MULTIPART (wrapper);
#if 0
	parent_content_type =
		camel_mime_part_get_content_type (
			camel_multipart_get_parent (mp));

	start = gmime_content_field_get_parameter (parent_content_type,
						   "start");
	if (start) {
		nparts = camel_multipart_get_number (mp);	
		for (i = 0; i < nparts; i++) {
			CamelMimeBodyPart *body_part =
				camel_multipart_get_part (mp, i);

			cid = camel_mime_part_get_content_id (
				CAMEL_MIME_PART (body_part));

			if (!strcmp (cid, start)) {
				display_camel_body_part (body_part, stream,
							 root);
				return;
			}
		}

		/* Oops. Hrmph. */
		handle_multipart_mixed (wrapper, stream, root);
	}
#endif

	/* No start parameter, so it defaults to the first part. */
	body_part = camel_multipart_get_part (mp, 0);
	display_camel_body_part (body_part, stream, root);
}

/* multipart/alternative helper function --
 * Returns NULL if no displayable msg is found
 */
static CamelMimePart *
find_preferred_alternative (CamelMultipart *multipart)
{
	int i, nparts;
	CamelMimePart* html_part = NULL;
	CamelMimePart* plain_part = NULL;	

	/* Find out out many parts are in it. */
	nparts = camel_multipart_get_number (multipart);

	/* FIXME: DO LEAF-LOOKUP HERE FOR OTHER MIME-TYPES!!! */

	for (i = 0; i < nparts; i++) {
		CamelMimeBodyPart *body_part =
			camel_multipart_get_part (multipart, i);

		if (strcasecmp (MIME_TYPE_MAIN (body_part), "text") != 0)
			continue;

		if (strcasecmp (MIME_TYPE_SUB (body_part), "plain") == 0)
			plain_part = CAMEL_MIME_PART (body_part);
		else if (strcasecmp (MIME_TYPE_SUB (body_part), "html") == 0)
			html_part = CAMEL_MIME_PART (body_part);
	}

	if (html_part)
		return html_part;
	if (plain_part)
		return plain_part;
	return NULL;
}

/* The current policy for multipart/alternative is this: 
 *
 * if (we find a text/html body part)
 *     we print it
 * else if (we find a text/plain body part)
 *     we print it
 * else
 *     we print nothing
 */
static void
handle_multipart_alternative (CamelDataWrapper *wrapper,
			      GtkHTMLStreamHandle *stream,
			      CamelDataWrapper *root)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (wrapper);
	CamelMimePart *mime_part;
	gchar *whole_mime_type;

	mime_part = find_preferred_alternative (multipart);	
	if (mime_part) {
		CamelDataWrapper *contents =
			camel_medium_get_content_object (
				CAMEL_MEDIUM (mime_part));

		whole_mime_type = MIME_TYPE_WHOLE (mime_part);
		call_handler_function (contents, whole_mime_type,
				       MIME_TYPE_MAIN (mime_part),
				       stream, root);
		g_free (whole_mime_type);
	}
}

static void
handle_unknown_type (CamelDataWrapper *wrapper,
		     GtkHTMLStreamHandle *stream,
		     CamelDataWrapper *root)
{
	gchar *tag;
	char *uid = lookup_unique_id (root, wrapper);	

	tag = g_strdup_printf ("<a href=\"camel://%s\">click-me-to-save</a>\n",
			       uid);

	mail_write_html (stream, tag);
}


void
mail_write_html (GtkHTMLStreamHandle *stream, const char *data)
{
	gtk_html_stream_write (stream, data, strlen (data));
}

static char *
get_data_wrapper_text (CamelDataWrapper *data)
{
	CamelStream *memstream;
	GByteArray *ba;
	char *text;

	ba = g_byte_array_new ();
	memstream = camel_stream_mem_new_with_byte_array (ba, CAMEL_STREAM_MEM_WRITE);

	camel_data_wrapper_write_to_stream (data, memstream);
	text = g_malloc (ba->len + 1);
	memcpy (text, ba->data, ba->len);
	text[ba->len] = '\0';

	camel_stream_close (memstream);
	return text;
}

static char *
reply_body (CamelDataWrapper *data, gboolean *html)
{
	CamelMultipart *mp;
	CamelMimePart *subpart;
	int i, nparts;
	char *subtext, *old;
	const char *boundary, *disp;
	char *text = NULL;
	GMimeContentField *mime_type;

	/* We only include text, message, and multipart bodies. */
	mime_type = camel_data_wrapper_get_mime_type_field (data);

	if (strcasecmp (mime_type->type, "message") == 0)
		return get_data_wrapper_text (data);

	if (strcasecmp (mime_type->type, "text") == 0) {
		*html = !strcasecmp (mime_type->subtype, "html");
		return get_data_wrapper_text (data);
	}

	/* If it's not message and it's not text, and it's not
	 * multipart, we don't want to deal with it.
	 */
	if (strcasecmp (mime_type->type, "multipart") != 0)
		return NULL;

	mp = CAMEL_MULTIPART (data);

	if (strcasecmp (mime_type->subtype, "alternative") == 0) {
		/* Pick our favorite alternative and reply to it. */

		subpart = find_preferred_alternative (mp);
		if (!subpart)
			return NULL;

		return reply_body (camel_medium_get_content_object (CAMEL_MEDIUM (subpart)), html);
	}

	nparts = camel_multipart_get_number (mp);

	/* If any subpart is HTML, pull it out and reply to it by itself.
	 * (If we supported any other non-plain text types, we'd do the
	 * same for them here.)
	 */
	for (i = 0; i < nparts; i++) {
		subpart = CAMEL_MIME_PART (camel_multipart_get_part (mp, i));

		if (strcasecmp (MIME_TYPE_SUB (subpart), "html") == 0)
			return reply_body (camel_medium_get_content_object (CAMEL_MEDIUM (subpart)), html);
	}

	/* Otherwise, concatenate all the parts that:
	 *   - are text/plain or message
	 *   - are not explicitly tagged with non-inline disposition
	 */
	boundary = camel_multipart_get_boundary (mp);
	for (i = 0; i < nparts; i++) {
		subpart = CAMEL_MIME_PART (camel_multipart_get_part (mp, i));

		disp = camel_mime_part_get_disposition (subpart);
		if (disp && strcasecmp (disp, "inline") != 0)
			continue;

		subtext = get_data_wrapper_text (data);
		if (text) {
			old = text;
			text = g_strdup_printf ("%s\n--%s\n%s", text,
						boundary, subtext);
			g_free (subtext);
			g_free (old);
		} else
			text = subtext;
	}

	if (!text)
		return NULL;

	old = text;
	text = g_strdup_printf ("%s\n--%s--\n", text, boundary);
	g_free (old);

	return text;
}

EMsgComposer *
mail_generate_reply (CamelMimeMessage *message, gboolean to_all)
{
	CamelDataWrapper *contents;
	char *text, *subject;
	EMsgComposer *composer;
	gboolean html;
	const char *repl_to, *message_id, *references;
	GList *to, *cc;

	contents = camel_medium_get_content_object (CAMEL_MEDIUM (message));
	text = reply_body (contents, &html);

	composer = E_MSG_COMPOSER (e_msg_composer_new ());

	/* Set the quoted reply text. */
	if (text) {
		char *repl_text;

		if (html) {
			repl_text = g_strdup_printf ("<blockquote><i>\n%s\n"
						     "</i></blockquote>\n",
						     text);
		} else {
			char *s, *d, *quoted_text;
			int lines, len;

			/* Count the number of lines in the body. If
			 * the text ends with a \n, this will be one
			 * too high, but that's ok. Allocate enough
			 * space for the text and the "> "s.
			 */
			for (s = text, lines = 0; s; s = strchr (s + 1, '\n'))
				lines++;
			quoted_text = g_malloc (strlen (text) + lines * 2);

			s = text;
			d = quoted_text;

			/* Copy text to quoted_text line by line,
			 * prepending "> ".
			 */
			while (1) {
				len = strcspn (s, "\n");
				if (len == 0 && !*s)
					break;
				sprintf (d, "> %.*s\n", len, s);
				s += len;
				if (!*s++)
					break;
				d += len + 3;
			}

			/* Now convert that to HTML. */
			repl_text = text_to_html (quoted_text,
						  strlen (quoted_text),
						  &len, TRUE, FALSE);
			g_free (quoted_text);
		}
		e_msg_composer_set_body_text (composer, repl_text);
		g_free (repl_text);
		g_free (text);
	}

	/* Set the recipients */
	repl_to = camel_mime_message_get_reply_to (message);
	if (!repl_to)
		repl_to = camel_mime_message_get_from (message);
	to = g_list_append (NULL, repl_to);

	if (to_all) {
		const GList *recip;

		recip = camel_mime_message_get_recipients (message, 
			CAMEL_RECIPIENT_TYPE_TO);
		cc = g_list_copy (recip);

		recip = camel_mime_message_get_recipients (message,
			CAMEL_RECIPIENT_TYPE_CC);
		while (recip) {
			cc = g_list_append (cc, recip->data);
			recip = recip->next;
		}
	} else
		cc = NULL;

	/* Set the subject of the new message. */
	subject = camel_mime_message_get_subject (message);
	if (!subject)
		subject = g_strdup ("");
	else if (!strncasecmp (subject, "Re: ", 4))
		subject = g_strdup (subject);
	else
		subject = g_strdup_printf ("Re: %s", subject);

	e_msg_composer_set_headers (composer, to, cc, NULL, subject);
	g_list_free (to);
	g_list_free (cc);
	g_free (subject);

	/* Add In-Reply-To and References. */
	message_id = camel_medium_get_header (CAMEL_MEDIUM (message),
					      "Message-Id");
	references = camel_medium_get_header (CAMEL_MEDIUM (message),
					      "References");
	if (message_id) {
		e_msg_composer_add_header (composer, "In-Reply-To",
					   message_id);
		if (references) {
			char *reply_refs;
			reply_refs = g_strdup_printf ("%s %s", references,
						      message_id);
			e_msg_composer_add_header (composer, "References",
						   reply_refs);
			g_free (reply_refs);
		}
	} else if (references)
		e_msg_composer_add_header (composer, "References", references);

	return composer;
}

/* This is part of the temporary kludge below. */
#ifndef HAVE_MKSTEMP
#include <fcntl.h>
#include <sys/stat.h>
#endif

EMsgComposer *
mail_generate_forward (CamelMimeMessage *mime_message,
		       gboolean forward_as_attachment,
		       gboolean keep_attachments)
{
	EMsgComposer *composer;
	char *tmpfile;
	int fd;
	CamelStream *stream;

	if (!forward_as_attachment)
		g_warning ("Forward as non-attachment not implemented.");
	if (!keep_attachments)
		g_warning ("Forwarding without attachments not implemented.");

	/* For now, we kludge by writing out a temp file. Later,
	 * EMsgComposer will support attaching CamelMimeParts directly,
	 * or something. FIXME.
	 */
	tmpfile = g_strdup ("/tmp/evolution-kludge-XXXX");
#ifdef HAVE_MKSTEMP
	fd = mkstemp (tmpfile);
#else
	if (mktemp (tmpfile)) {
		fd = open (tmpfile, O_RDWR | O_CREAT | O_EXCL,
			   S_IRUSR | S_IWUSR);
	} else
		fd = -1;
#endif
	if (fd == -1) {
		g_warning ("Couldn't create temp file for forwarding");
		g_free (tmpfile);
		return NULL;
	}

	stream = camel_stream_fs_new_with_fd (fd);
	camel_data_wrapper_write_to_stream (CAMEL_DATA_WRAPPER (mime_message),
					    stream);
	camel_stream_close (stream);

	composer = E_MSG_COMPOSER (e_msg_composer_new ());
	e_msg_composer_attachment_bar_attach (composer->attachment_bar,
					      tmpfile);
	g_free (tmpfile);

	/* FIXME: should we default a subject? */

	return composer;
}
