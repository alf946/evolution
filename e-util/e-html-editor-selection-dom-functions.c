/*
 * e-html-editor-selection-dom-functions.c
 *
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
 */

#include "e-html-editor-selection-dom-functions.h"

#include "e-util-enums.h"
#include "e-dom-utils.h"
#include "e-html-editor-view-dom-functions.h"

#include <string.h>

#define WEBKIT_DOM_USE_UNSTABLE_API
#include <webkitdom/WebKitDOMDOMSelection.h>
#include <webkitdom/WebKitDOMDOMWindowUnstable.h>
#include <webkitdom/WebKitDOMDocumentFragmentUnstable.h>

#define UNICODE_ZERO_WIDTH_SPACE "\xe2\x80\x8b"
#define UNICODE_NBSP "\xc2\xa0"

#define SPACES_PER_INDENTATION 4
#define SPACES_PER_LIST_LEVEL 8
#define MINIMAL_PARAGRAPH_WIDTH 5
#define WORD_WRAP_LENGTH 71

void
e_html_editor_selection_dom_replace_base64_image_src (WebKitDOMDocument *document,
                                                      const gchar *selector,
                                                      const gchar *base64_content,
                                                      const gchar *filename,
                                                      const gchar *uri)
{
	WebKitDOMElement *element;

	element = webkit_dom_document_query_selector (document, selector, NULL);

	if (WEBKIT_DOM_IS_HTML_IMAGE_ELEMENT (element))
		webkit_dom_html_image_element_set_src (
			WEBKIT_DOM_HTML_IMAGE_ELEMENT (element),
			base64_content);
	else
		webkit_dom_element_set_attribute (
			element, "background", base64_content, NULL);

	webkit_dom_element_set_attribute (element, "data-uri", uri, NULL);
	webkit_dom_element_set_attribute (element, "data-inline", "", NULL);
	webkit_dom_element_set_attribute (
		element, "data-name", filename ? filename : "", NULL);
}

/**
 * e_html_editor_selection_dom_clear_caret_position_marker:
 * @selection: an #EHTMLEditorSelection
 *
 * Removes previously set caret position marker from composer.
 */
void
e_html_editor_selection_dom_clear_caret_position_marker (WebKitDOMDocument *document)
{
	WebKitDOMElement *element;

	element = webkit_dom_document_get_element_by_id (document, "-x-evo-caret-position");

	if (element)
		remove_node (WEBKIT_DOM_NODE (element));
}
/* FIXME WK2 Rename to _create_caret_position_node */
static WebKitDOMNode *
e_html_editor_selection_dom_get_caret_position_node (WebKitDOMDocument *document)
{
	WebKitDOMElement *element;

	element	= webkit_dom_document_create_element (document, "SPAN", NULL);
	webkit_dom_element_set_id (element, "-x-evo-caret-position");
	webkit_dom_element_set_attribute (
		element, "style", "color: red", NULL);
	webkit_dom_html_element_set_inner_html (
		WEBKIT_DOM_HTML_ELEMENT (element), "*", NULL);

	return WEBKIT_DOM_NODE (element);
}

static WebKitDOMRange *
html_editor_selection_dom_get_current_range (WebKitDOMDocument *document)
{
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection_dom;
	WebKitDOMRange *range = NULL;

	window = webkit_dom_document_get_default_view (document);
	if (!window)
		return NULL;

	selection_dom = webkit_dom_dom_window_get_selection (window);
	if (!WEBKIT_DOM_IS_DOM_SELECTION (selection_dom))
		return NULL;

	if (webkit_dom_dom_selection_get_range_count (selection_dom) < 1)
		return NULL;

	range = webkit_dom_dom_selection_get_range_at (selection_dom, 0, NULL);

	return range;
}
/**
 * e_html_editor_selection_dom_save_caret_position:
 * @selection: an #EHTMLEditorSelection
 *
 * Saves current caret position in composer.
 *
 * Returns: #WebKitDOMElement that was created on caret position
 */
WebKitDOMElement *
e_html_editor_selection_dom_save_caret_position (WebKitDOMDocument *document)
{
	WebKitDOMNode *split_node;
	WebKitDOMNode *start_offset_node;
	WebKitDOMNode *caret_node;
	WebKitDOMRange *range;
	gulong start_offset;

	e_html_editor_selection_dom_clear_caret_position_marker (document);

	range = html_editor_selection_dom_get_current_range (document);
	if (!range)
		return NULL;

	start_offset = webkit_dom_range_get_start_offset (range, NULL);
	start_offset_node = webkit_dom_range_get_end_container (range, NULL);

	caret_node = e_html_editor_selection_dom_get_caret_position_node (document);

	if (WEBKIT_DOM_IS_TEXT (start_offset_node) && start_offset != 0) {
		WebKitDOMText *split_text;

		split_text = webkit_dom_text_split_text (
				WEBKIT_DOM_TEXT (start_offset_node),
				start_offset, NULL);
		split_node = WEBKIT_DOM_NODE (split_text);
	} else {
		split_node = start_offset_node;
	}

	webkit_dom_node_insert_before (
		webkit_dom_node_get_parent_node (start_offset_node),
		caret_node,
		split_node,
		NULL);

	return WEBKIT_DOM_ELEMENT (caret_node);
}

static void
fix_quoting_nodes_after_caret_restoration (WebKitDOMDOMSelection *window_selection,
                                           WebKitDOMNode *prev_sibling,
                                           WebKitDOMNode *next_sibling)
{
	WebKitDOMNode *tmp_node;

	if (!element_has_class (WEBKIT_DOM_ELEMENT (prev_sibling), "-x-evo-temp-text-wrapper"))
		return;

	webkit_dom_dom_selection_modify (
		window_selection, "move", "forward", "character");
	tmp_node = webkit_dom_node_get_next_sibling (
		webkit_dom_node_get_first_child (prev_sibling));

	webkit_dom_node_insert_before (
		webkit_dom_node_get_parent_node (prev_sibling),
		tmp_node,
		next_sibling,
		NULL);

	tmp_node = webkit_dom_node_get_first_child (prev_sibling);

	webkit_dom_node_insert_before (
		webkit_dom_node_get_parent_node (prev_sibling),
		tmp_node,
		webkit_dom_node_get_previous_sibling (next_sibling),
		NULL);

	remove_node (prev_sibling);

	webkit_dom_dom_selection_modify (
		window_selection, "move", "backward", "character");
}

static void
e_html_editor_selection_dom_move_caret_into_element (WebKitDOMDocument *document,
                                                     WebKitDOMElement *element)
{
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *window_selection;
	WebKitDOMRange *new_range;

	if (!element)
		return;

	window = webkit_dom_document_get_default_view (document);
	window_selection = webkit_dom_dom_window_get_selection (window);
	new_range = webkit_dom_document_create_range (document);

	webkit_dom_range_select_node_contents (
		new_range, WEBKIT_DOM_NODE (element), NULL);
	webkit_dom_range_collapse (new_range, FALSE, NULL);
	webkit_dom_dom_selection_remove_all_ranges (window_selection);
	webkit_dom_dom_selection_add_range (window_selection, new_range);
}

/**
 * e_html_editor_selection_restore_caret_position:
 * @selection: an #EHTMLEditorSelection
 *
 * Restores previously saved caret position in composer.
 */
void
e_html_editor_selection_dom_restore_caret_position (WebKitDOMDocument *document)
{
	WebKitDOMElement *element;
	gboolean fix_after_quoting;
	gboolean swap_direction = FALSE;

	element = webkit_dom_document_get_element_by_id (
		document, "-x-evo-caret-position");
	fix_after_quoting = element_has_class (element, "-x-evo-caret-quoting");

	if (element) {
		WebKitDOMDOMWindow *window;
		WebKitDOMNode *parent_node;
		WebKitDOMDOMSelection *window_selection;
		WebKitDOMNode *prev_sibling;
		WebKitDOMNode *next_sibling;

		if (!webkit_dom_node_get_previous_sibling (WEBKIT_DOM_NODE (element)))
			swap_direction = TRUE;

		window = webkit_dom_document_get_default_view (document);
		window_selection = webkit_dom_dom_window_get_selection (window);
		parent_node = webkit_dom_node_get_parent_node (WEBKIT_DOM_NODE (element));
		/* If parent is BODY element, we try to restore the position on the
		 * element that is next to us */
		if (WEBKIT_DOM_IS_HTML_BODY_ELEMENT (parent_node)) {
			/* Look if we have DIV on right */
			next_sibling = webkit_dom_node_get_next_sibling (
				WEBKIT_DOM_NODE (element));
			if (!WEBKIT_DOM_IS_ELEMENT (next_sibling)) {
				e_html_editor_selection_dom_clear_caret_position_marker (document);
				return;
			}

			if (element_has_class (WEBKIT_DOM_ELEMENT (next_sibling), "-x-evo-paragraph")) {
				remove_node (WEBKIT_DOM_NODE (element));

				e_html_editor_selection_dom_move_caret_into_element (
					document, WEBKIT_DOM_ELEMENT (next_sibling));

				goto out;
			}
		}

		e_html_editor_selection_dom_move_caret_into_element (document, element);

		if (fix_after_quoting) {
			prev_sibling = webkit_dom_node_get_previous_sibling (
				WEBKIT_DOM_NODE (element));
			next_sibling = webkit_dom_node_get_next_sibling (
				WEBKIT_DOM_NODE (element));
			if (!next_sibling)
				fix_after_quoting = FALSE;
		}

		remove_node (WEBKIT_DOM_NODE (element));

		if (fix_after_quoting)
			fix_quoting_nodes_after_caret_restoration (
				window_selection, prev_sibling, next_sibling);
 out:
		/* FIXME If caret position is restored and afterwards the
		 * position is saved it is not on the place where it supposed
		 * to be (it is in the beginning of parent's element. It can
		 * be avoided by moving with the caret. */
		if (swap_direction) {
			webkit_dom_dom_selection_modify (
				window_selection, "move", "forward", "character");
			webkit_dom_dom_selection_modify (
				window_selection, "move", "backward", "character");
		} else {
			webkit_dom_dom_selection_modify (
				window_selection, "move", "backward", "character");
			webkit_dom_dom_selection_modify (
				window_selection, "move", "forward", "character");
		}
	}
}

static void
e_html_editor_selection_dom_insert_base64_image (WebKitDOMDocument *document,
                                                 const gchar *base64_content,
                                                 const gchar *filename,
                                                 const gchar *uri)
{
	WebKitDOMElement *element, *caret_position, *resizable_wrapper;
	WebKitDOMText *text;

	caret_position = e_html_editor_selection_dom_save_caret_position (document);

	resizable_wrapper =
		webkit_dom_document_create_element (document, "span", NULL);
	webkit_dom_element_set_attribute (
		resizable_wrapper, "class", "-x-evo-resizable-wrapper", NULL);

	element = webkit_dom_document_create_element (document, "img", NULL);
	webkit_dom_html_image_element_set_src (
		WEBKIT_DOM_HTML_IMAGE_ELEMENT (element),
		base64_content);
	webkit_dom_element_set_attribute (
		WEBKIT_DOM_ELEMENT (element), "data-uri", uri, NULL);
	webkit_dom_element_set_attribute (
		WEBKIT_DOM_ELEMENT (element), "data-inline", "", NULL);
	webkit_dom_element_set_attribute (
		WEBKIT_DOM_ELEMENT (element), "data-name",
		filename ? filename : "", NULL);
	webkit_dom_node_append_child (
		WEBKIT_DOM_NODE (resizable_wrapper),
		WEBKIT_DOM_NODE (element),
		NULL);

	webkit_dom_node_insert_before (
		webkit_dom_node_get_parent_node (
			WEBKIT_DOM_NODE (caret_position)),
		WEBKIT_DOM_NODE (resizable_wrapper),
		WEBKIT_DOM_NODE (caret_position),
		NULL);

	/* We have to again use UNICODE_ZERO_WIDTH_SPACE character to restore
	 * caret on right position */
	text = webkit_dom_document_create_text_node (
		document, UNICODE_ZERO_WIDTH_SPACE);

	webkit_dom_node_insert_before (
		webkit_dom_node_get_parent_node (
			WEBKIT_DOM_NODE (caret_position)),
		WEBKIT_DOM_NODE (text),
		WEBKIT_DOM_NODE (caret_position),
		NULL);

	e_html_editor_selection_dom_restore_caret_position (document);
}

/**
 * e_html_editor_selection_dom_unlink:
 * @selection: an #EHTMLEditorSelection
 *
 * Removes any links (&lt;A&gt; elements) from current selection or at current
 * cursor position.
 */
void
e_html_editor_selection_dom_unlink (WebKitDOMDocument *document)
{
	EHTMLEditorViewCommand command;
	WebKitDOMDOMWindow *window;
	WebKitDOMDOMSelection *selection_dom;
	WebKitDOMRange *range;
	WebKitDOMElement *link;

	window = webkit_dom_document_get_default_view (document);
	selection_dom = webkit_dom_dom_window_get_selection (window);

	range = webkit_dom_dom_selection_get_range_at (selection_dom, 0, NULL);
	link = e_html_editor_dom_node_find_parent_element (
			webkit_dom_range_get_start_container (range, NULL), "A");

	if (!link) {
		gchar *text;
		/* get element that was clicked on */
		/* FIXME WK2
		link = e_html_editor_view_get_element_under_mouse_click (view); */
		if (!WEBKIT_DOM_IS_HTML_ANCHOR_ELEMENT (link))
			link = NULL;

		text = webkit_dom_html_element_get_inner_text (
			WEBKIT_DOM_HTML_ELEMENT (link));
		webkit_dom_html_element_set_outer_html (
			WEBKIT_DOM_HTML_ELEMENT (link), text, NULL);
		g_free (text);
	} else {
		command = E_HTML_EDITOR_VIEW_COMMAND_UNLINK;
		e_html_editor_view_dom_exec_command (document, command, NULL);
	}
}

/**
 * e_html_editor_selection_dom_create_link:
 * @document: a @WebKitDOMDocument
 * @uri: destination of the new link
 *
 * Converts current selection into a link pointing to @url.
 */
void
e_html_editor_selection_dom_create_link (WebKitDOMDocument *document,
                                         const gchar *uri)
{
	EHTMLEditorViewCommand command;

	g_return_if_fail (uri != NULL && *uri != '\0');

	command = E_HTML_EDITOR_VIEW_COMMAND_CREATE_LINK;
	e_html_editor_view_dom_exec_command (document, command, uri);
}

/**
 * e_html_editor_selection_dom_get_list_format_from_node:
 * @node: an #WebKitDOMNode
 *
 * Returns block format of given list.
 *
 * Returns: #EHTMLEditorSelectionBlockFormat
 */
static EHTMLEditorSelectionBlockFormat
e_html_editor_selection_dom_get_list_format_from_node (WebKitDOMNode *node)
{
	EHTMLEditorSelectionBlockFormat format =
		E_HTML_EDITOR_SELECTION_BLOCK_FORMAT_UNORDERED_LIST;

	if (WEBKIT_DOM_IS_HTML_LI_ELEMENT (node))
		return -1;

	if (WEBKIT_DOM_IS_HTML_U_LIST_ELEMENT (node))
		return format;

	if (WEBKIT_DOM_IS_HTML_O_LIST_ELEMENT (node)) {
		gchar *type_value = webkit_dom_element_get_attribute (
			WEBKIT_DOM_ELEMENT (node), "type");

		if (!type_value)
			return E_HTML_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST;

		if (!*type_value)
			format = E_HTML_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST;
		else if (g_ascii_strcasecmp (type_value, "A") == 0)
			format = E_HTML_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST_ALPHA;
		else if (g_ascii_strcasecmp (type_value, "I") == 0)
			format = E_HTML_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST_ROMAN;
		g_free (type_value);

		return format;
	}

	return -1;
}

static gboolean
is_in_html_mode (WebKitDOMDocument *document)
{
	WebKitDOMElement *document_element;

	document_element = webkit_dom_document_get_document_element (document);

	return webkit_dom_element_has_attribute (document_element, "data-html-mode");

	/* FIXME WK2 we have to probably add a body attribute that shows,
	 * whether the composer is in HTML mode */
	/*
	EHTMLEditorView *view = e_html_editor_selection_ref_html_editor_view (selection);
	gboolean ret_val;

	g_return_val_if_fail (view != NULL, FALSE);

	ret_val = e_html_editor_view_get_html_mode (view);

	g_object_unref (view);

	return ret_val;
	*/
	return FALSE;
}

static gboolean
node_is_list_or_item (WebKitDOMNode *node)
{
	return node && (
		WEBKIT_DOM_IS_HTML_O_LIST_ELEMENT (node) ||
		WEBKIT_DOM_IS_HTML_U_LIST_ELEMENT (node) ||
		WEBKIT_DOM_IS_HTML_LI_ELEMENT (node));
}

static gboolean
node_is_list (WebKitDOMNode *node)
{
	return node && (
		WEBKIT_DOM_IS_HTML_O_LIST_ELEMENT (node) ||
		WEBKIT_DOM_IS_HTML_U_LIST_ELEMENT (node));
}

static gint
get_list_level (WebKitDOMNode *node)
{
	gint level = 0;

	while (node && !WEBKIT_DOM_IS_HTML_BODY_ELEMENT (node)) {
		if (node_is_list (node))
			level++;
		node = webkit_dom_node_get_parent_node (node);
	}

	return level;
}

static void
set_ordered_list_type_to_element (WebKitDOMElement *list,
                                  EHTMLEditorSelectionBlockFormat format)
{
	if (format == E_HTML_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST)
		webkit_dom_element_remove_attribute (list, "type");
	else if (format == E_HTML_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST_ALPHA)
		webkit_dom_element_set_attribute (list, "type", "A", NULL);
	else if (format == E_HTML_EDITOR_SELECTION_BLOCK_FORMAT_ORDERED_LIST_ROMAN)
		webkit_dom_element_set_attribute (list, "type", "I", NULL);
}

static const gchar *
get_css_alignment_value_class (EHTMLEditorSelectionAlignment alignment)
{
	if (alignment == E_HTML_EDITOR_SELECTION_ALIGNMENT_LEFT)
		return ""; /* Left is by default on ltr */

	if (alignment == E_HTML_EDITOR_SELECTION_ALIGNMENT_CENTER)
		return "-x-evo-align-center";

	if (alignment == E_HTML_EDITOR_SELECTION_ALIGNMENT_RIGHT)
		return "-x-evo-align-right";

	return "";
}

/**
 * e_html_editor_selection_get_alignment:
 * @selection: #an EHTMLEditorSelection
 *
 * Returns alignment of current paragraph
 *
 * Returns: #EHTMLEditorSelectionAlignment
 */
static EHTMLEditorSelectionAlignment
e_html_editor_selection_get_alignment (WebKitDOMDocument *document)
{
	EHTMLEditorSelectionAlignment alignment;
	gchar *value;
	WebKitDOMCSSStyleDeclaration *style;
	WebKitDOMDOMWindow *window;
	WebKitDOMElement *element;
	WebKitDOMNode *node;
	WebKitDOMRange *range;

	window = webkit_dom_document_get_default_view (document);
	range = html_editor_selection_dom_get_current_range (document);
	if (!range)
		return E_HTML_EDITOR_SELECTION_ALIGNMENT_LEFT;

	node = webkit_dom_range_get_start_container (range, NULL);
	if (!node)
		return E_HTML_EDITOR_SELECTION_ALIGNMENT_LEFT;

	if (WEBKIT_DOM_IS_ELEMENT (node))
		element = WEBKIT_DOM_ELEMENT (node);
	else
		element = webkit_dom_node_get_parent_element (node);

	style = webkit_dom_dom_window_get_computed_style (window, element, NULL);
	value = webkit_dom_css_style_declaration_get_property_value (style, "text-align");

	if (!value || !*value ||
	    (g_ascii_strncasecmp (value, "left", 4) == 0)) {
		alignment = E_HTML_EDITOR_SELECTION_ALIGNMENT_LEFT;
	} else if (g_ascii_strncasecmp (value, "center", 6) == 0) {
		alignment = E_HTML_EDITOR_SELECTION_ALIGNMENT_CENTER;
	} else if (g_ascii_strncasecmp (value, "right", 5) == 0) {
		alignment = E_HTML_EDITOR_SELECTION_ALIGNMENT_RIGHT;
	} else {
		alignment = E_HTML_EDITOR_SELECTION_ALIGNMENT_LEFT;
	}

	g_free (value);

	return alignment;
}

static void
e_html_editor_selection_dom_set_paragraph_style (WebKitDOMDocument *document,
                                                 WebKitDOMElement *element,
                                                 gint width,
                                                 gint offset,
                                                 const gchar *style_to_add)
{
	EHTMLEditorSelectionAlignment alignment;
	char *style = NULL;
	gint word_wrap_length = (width == -1) ? WORD_WRAP_LENGTH : width;

	alignment = e_html_editor_selection_get_alignment (document);

	element_add_class (element, "-x-evo-paragraph");
	element_add_class (element, get_css_alignment_value_class (alignment));
	if (!is_in_html_mode (document)) {
		style = g_strdup_printf (
			"width: %dch; word-wrap: normal; %s",
			(word_wrap_length + offset), style_to_add);
	} else {
		if (*style_to_add)
			style = g_strdup_printf ("%s", style_to_add);
	}
	if (style) {
		webkit_dom_element_set_attribute (element, "style", style, NULL);
		g_free (style);
	}
}

static WebKitDOMElement *
create_list_element (WebKitDOMDocument *document,
                     EHTMLEditorSelectionBlockFormat format,
		     gint level,
                     gboolean html_mode)
{
	WebKitDOMElement *list;
	gint offset = -SPACES_PER_LIST_LEVEL;
	gboolean inserting_unordered_list =
		format == E_HTML_EDITOR_SELECTION_BLOCK_FORMAT_UNORDERED_LIST;

	list = webkit_dom_document_create_element (
		document, inserting_unordered_list  ? "UL" : "OL", NULL);

	set_ordered_list_type_to_element (list, format);

	if (level >= 0)
		offset = (level + 1) * -SPACES_PER_LIST_LEVEL;

	if (!html_mode)
		e_html_editor_selection_dom_set_paragraph_style (
			document, list, -1, offset, "");

	return list;
}

static WebKitDOMNode *
get_parent_block_node_from_child (WebKitDOMNode *node)
{
	WebKitDOMElement *parent = WEBKIT_DOM_ELEMENT (
		webkit_dom_node_get_parent_node (node));

	 if (WEBKIT_DOM_IS_HTML_ANCHOR_ELEMENT (parent) ||
	     element_has_tag (parent, "b") ||
	     element_has_tag (parent, "i") ||
	     element_has_tag (parent, "u"))
		parent = WEBKIT_DOM_ELEMENT (
			webkit_dom_node_get_parent_node (WEBKIT_DOM_NODE (parent)));

	if (element_has_class (parent, "-x-evo-temp-text-wrapper") ||
	    element_has_class (parent, "-x-evo-signature"))
		parent = WEBKIT_DOM_ELEMENT (
			webkit_dom_node_get_parent_node (WEBKIT_DOM_NODE (parent)));

	return WEBKIT_DOM_NODE (parent);
}

static void
merge_list_into_list (WebKitDOMNode *from,
                      WebKitDOMNode *to,
                      gboolean insert_before)
{
	WebKitDOMNode *item;

	if (!(to && from))
		return;

	while ((item = webkit_dom_node_get_first_child (from)) != NULL) {
		if (insert_before)
			webkit_dom_node_insert_before (
				to, item, webkit_dom_node_get_last_child (to), NULL);
		else
			webkit_dom_node_append_child (to, item, NULL);
	}

	if (!webkit_dom_node_has_child_nodes (from))
		remove_node (from);

}

static void
merge_lists_if_possible (WebKitDOMNode *list)
{
	EHTMLEditorSelectionBlockFormat format, prev, next;
	WebKitDOMNode *prev_sibling, *next_sibling;

	prev_sibling = webkit_dom_node_get_previous_sibling (WEBKIT_DOM_NODE (list));
	next_sibling = webkit_dom_node_get_next_sibling (WEBKIT_DOM_NODE (list));

	format = e_html_editor_selection_dom_get_list_format_from_node (list),
	prev = e_html_editor_selection_dom_get_list_format_from_node (prev_sibling);
	next = e_html_editor_selection_dom_get_list_format_from_node (next_sibling);

	if (format == prev && format != -1 && prev != -1)
		merge_list_into_list (prev_sibling, list, TRUE);

	if (format == next && format != -1 && next != -1)
		merge_list_into_list (next_sibling, list, FALSE);
}

static void
indent_list (WebKitDOMDocument *document)
{
	WebKitDOMElement *selection_start_marker, *selection_end_marker;
	WebKitDOMNode *item, *next_item;
	gboolean after_selection_end = FALSE;

	selection_start_marker = webkit_dom_document_query_selector (
		document, "span#-x-evo-selection-start-marker", NULL);

	selection_end_marker = webkit_dom_document_query_selector (
		document, "span#-x-evo-selection-end-marker", NULL);

	item = get_parent_block_node_from_child (
		WEBKIT_DOM_NODE (selection_start_marker));

	if (WEBKIT_DOM_IS_HTML_LI_ELEMENT (item)) {
		gboolean html_mode = is_in_html_mode (document);
		WebKitDOMElement *list;
		WebKitDOMNode *source_list = webkit_dom_node_get_parent_node (item);
		EHTMLEditorSelectionBlockFormat format;

		format = e_html_editor_selection_dom_get_list_format_from_node (source_list);

		list = create_list_element (
			document, format, get_list_level (item), html_mode);

		element_add_class (list, "-x-evo-indented");

		webkit_dom_node_insert_before (
			source_list, WEBKIT_DOM_NODE (list), item, NULL);

		while (item && !after_selection_end) {
			after_selection_end = webkit_dom_node_contains (
				item, WEBKIT_DOM_NODE (selection_end_marker));

			next_item = webkit_dom_node_get_next_sibling (item);

			webkit_dom_node_append_child (
				WEBKIT_DOM_NODE (list), item, NULL);

			item = next_item;
		}

		merge_lists_if_possible (WEBKIT_DOM_NODE (list));
	}
}

static void
e_html_editor_selection_set_indented_style (WebKitDOMDocument *document,
                                            WebKitDOMElement *element,
                                            gint width)
{
	gchar *style;
	gint word_wrap_length = (width == -1) ? WORD_WRAP_LENGTH : width;

	webkit_dom_element_set_class_name (element, "-x-evo-indented");

	if (is_in_html_mode (document))
		style = g_strdup_printf ("margin-left: %dch;", SPACES_PER_INDENTATION);
	else
		style = g_strdup_printf (
			"margin-left: %dch; word-wrap: normal; width: %dch;",
			SPACES_PER_INDENTATION, word_wrap_length);

	webkit_dom_element_set_attribute (element, "style", style, NULL);
	g_free (style);
}

static WebKitDOMElement *
e_html_editor_selection_get_indented_element (WebKitDOMDocument *document,
                                              gint width)
{
	WebKitDOMElement *element;

	element = webkit_dom_document_create_element (document, "DIV", NULL);
	e_html_editor_selection_set_indented_style (document, element, width);

	return element;
}

static void
indent_block (WebKitDOMDocument *document,
              WebKitDOMNode *block,
              gint width)
{
	WebKitDOMElement *element;

	element = e_html_editor_selection_get_indented_element (document, width);

	webkit_dom_node_insert_before (
		webkit_dom_node_get_parent_node (block),
		WEBKIT_DOM_NODE (element),
		block,
		NULL);

	/* Remove style and let the paragraph inherit it from parent */
	if (element_has_class (WEBKIT_DOM_ELEMENT (block), "-x-evo-paragraph"))
		webkit_dom_element_remove_attribute (
			WEBKIT_DOM_ELEMENT (block), "style");

	webkit_dom_node_append_child (
		WEBKIT_DOM_NODE (element),
		block,
		NULL);
}

static gint
get_indentation_level (WebKitDOMElement *element)
{
	WebKitDOMElement *parent;
	gint level = 0;

	if (element_has_class (element, "-x-evo-indented"))
		level++;

	parent = webkit_dom_node_get_parent_element (WEBKIT_DOM_NODE (element));
	/* Count level of indentation */
	while (parent && !WEBKIT_DOM_IS_HTML_BODY_ELEMENT (parent)) {
		if (element_has_class (parent, "-x-evo-indented"))
			level++;

		parent = webkit_dom_node_get_parent_element (WEBKIT_DOM_NODE (parent));
	}

	return level;
}

static WebKitDOMNode *
get_parent_indented_block (WebKitDOMNode *node)
{
	WebKitDOMNode *parent, *block = NULL;

	parent = webkit_dom_node_get_parent_node (node);
	if (element_has_class (WEBKIT_DOM_ELEMENT (parent), "-x-evo-indented"))
		block = parent;

	while (parent && !WEBKIT_DOM_IS_HTML_BODY_ELEMENT (parent)) {
		if (element_has_class (WEBKIT_DOM_ELEMENT (parent), "-x-evo-indented"))
			block = parent;
		parent = webkit_dom_node_get_parent_node (parent);
	}

	return block;
}

static WebKitDOMElement*
get_element_for_inspection (WebKitDOMRange *range)
{
	WebKitDOMNode *node;

	node = webkit_dom_range_get_end_container (range, NULL);
	/* No selection or whole body selected */
	if (WEBKIT_DOM_IS_HTML_BODY_ELEMENT (node))
		return NULL;

	return WEBKIT_DOM_ELEMENT (get_parent_indented_block (node));
}

static EHTMLEditorSelectionAlignment
e_html_editor_selection_get_alignment_from_node (WebKitDOMNode *node)
{
	EHTMLEditorSelectionAlignment alignment;
	gchar *value;
	WebKitDOMCSSStyleDeclaration *style;
	WebKitDOMDocument *document;
	WebKitDOMDOMWindow *window;

	document = webkit_dom_node_get_owner_document (node);
	window = webkit_dom_document_get_default_view (document);

	style = webkit_dom_dom_window_get_computed_style (
		window, WEBKIT_DOM_ELEMENT (node), NULL);
	value = webkit_dom_css_style_declaration_get_property_value (style, "text-align");

	if (!value || !*value ||
	    (g_ascii_strncasecmp (value, "left", 4) == 0)) {
		alignment = E_HTML_EDITOR_SELECTION_ALIGNMENT_LEFT;
	} else if (g_ascii_strncasecmp (value, "center", 6) == 0) {
		alignment = E_HTML_EDITOR_SELECTION_ALIGNMENT_CENTER;
	} else if (g_ascii_strncasecmp (value, "right", 5) == 0) {
		alignment = E_HTML_EDITOR_SELECTION_ALIGNMENT_RIGHT;
	} else {
		alignment = E_HTML_EDITOR_SELECTION_ALIGNMENT_LEFT;
	}

	g_free (value);

	return alignment;
}

/**
 * e_html_editor_selection_indent:
 * @selection: an #EHTMLEditorSelection
 *
 * Indents current paragraph by one level.
 */
static void
e_html_editor_selection_indent (WebKitDOMDocument *document)
{
	gboolean after_selection_start = FALSE, after_selection_end = FALSE;
	WebKitDOMElement *selection_start_marker, *selection_end_marker;
	WebKitDOMNode *block;

	e_html_editor_selection_dom_save (document);

	selection_start_marker = webkit_dom_document_query_selector (
		document, "span#-x-evo-selection-start-marker", NULL);

	selection_end_marker = webkit_dom_document_query_selector (
		document, "span#-x-evo-selection-end-marker", NULL);

	/* If the selection was not saved, move it into the first child of body */
	if (!selection_start_marker || !selection_end_marker) {
		WebKitDOMHTMLElement *body;
		WebKitDOMNode *child;

		body = webkit_dom_document_get_body (document);
		child = webkit_dom_node_get_first_child (WEBKIT_DOM_NODE (body));

		add_selection_markers_into_element_start (
			document,
			WEBKIT_DOM_ELEMENT (child),
			&selection_start_marker,
			&selection_end_marker);
	}

	block = get_parent_indented_block (
		WEBKIT_DOM_NODE (selection_start_marker));
	if (!block)
		block = get_parent_block_node_from_child (
			WEBKIT_DOM_NODE (selection_start_marker));

	while (block && !after_selection_end) {
		gint ii, length, level, final_width = 0;
		gint word_wrap_length = WORD_WRAP_LENGTH;
		WebKitDOMNode *next_block;
		WebKitDOMNodeList *list;

		next_block = webkit_dom_node_get_next_sibling (block);

		list = webkit_dom_element_query_selector_all (
			WEBKIT_DOM_ELEMENT (block),
			".-x-evo-indented > *:not(.-x-evo-indented):not(li)",
			NULL);

		after_selection_end = webkit_dom_node_contains (
			block, WEBKIT_DOM_NODE (selection_end_marker));

		length = webkit_dom_node_list_get_length (list);
		if (length == 0 && node_is_list_or_item (block)) {
			indent_list (document);
			goto next;
		}

		if (length == 0) {
			if (!after_selection_start) {
				after_selection_start = webkit_dom_node_contains (
					block, WEBKIT_DOM_NODE (selection_start_marker));
				if (!after_selection_start)
					goto next;
			}

			level = get_indentation_level (WEBKIT_DOM_ELEMENT (block));

			final_width = word_wrap_length - SPACES_PER_INDENTATION * (level + 1);
			if (final_width < MINIMAL_PARAGRAPH_WIDTH &&
			    !is_in_html_mode (document))
				goto next;

			indent_block (document, block, final_width);

			if (after_selection_end)
				goto next;
		}

		for (ii = 0; ii < length; ii++) {
			WebKitDOMNode *block_to_process;

			block_to_process = webkit_dom_node_list_item (list, ii);

			after_selection_end = webkit_dom_node_contains (
				block_to_process, WEBKIT_DOM_NODE (selection_end_marker));

			if (!after_selection_start) {
				after_selection_start = webkit_dom_node_contains (
					block_to_process,
					WEBKIT_DOM_NODE (selection_start_marker));
				if (!after_selection_start)
					continue;
			}

			level = get_indentation_level (
				WEBKIT_DOM_ELEMENT (block_to_process));

			final_width = word_wrap_length - SPACES_PER_INDENTATION * (level + 1);
			if (final_width < MINIMAL_PARAGRAPH_WIDTH &&
			    !is_in_html_mode (document))
				continue;

			indent_block (document, block_to_process, final_width);

			if (after_selection_end)
				break;
		}

 next:
		g_object_unref (list);

		if (!after_selection_end)
			block = next_block;
	}

	e_html_editor_selection_dom_restore (document);
	/* FIXME WK2
	e_html_editor_view_force_spell_check_for_current_paragraph (view);

	g_object_notify (G_OBJECT (selection), "indented"); */
}

static void
unindent_list (WebKitDOMDocument *document)
{
	gboolean after = FALSE;
	WebKitDOMElement *new_list;
	WebKitDOMElement *selection_start_marker, *selection_end_marker;
	WebKitDOMNode *source_list, *source_list_clone, *current_list, *item;
	WebKitDOMNode *prev_item;

	selection_start_marker = webkit_dom_document_query_selector (
		document, "span#-x-evo-selection-start-marker", NULL);
	selection_end_marker = webkit_dom_document_query_selector (
		document, "span#-x-evo-selection-end-marker", NULL);

	if (!selection_start_marker || !selection_end_marker)
		return;

	/* Copy elements from previous block to list */
	item = get_parent_block_node_from_child (
		WEBKIT_DOM_NODE (selection_start_marker));
	source_list = webkit_dom_node_get_parent_node (item);
	new_list = WEBKIT_DOM_ELEMENT (
		webkit_dom_node_clone_node (source_list, FALSE));
	current_list = source_list;
	source_list_clone = webkit_dom_node_clone_node (source_list, FALSE);

	webkit_dom_node_insert_before (
		webkit_dom_node_get_parent_node (source_list),
		WEBKIT_DOM_NODE (source_list_clone),
		webkit_dom_node_get_next_sibling (source_list),
		NULL);

	if (element_has_class (WEBKIT_DOM_ELEMENT (source_list), "-x-evo-indented"))
		element_add_class (WEBKIT_DOM_ELEMENT (new_list), "-x-evo-indented");

	prev_item = source_list;

	while (item) {
		WebKitDOMNode *next_item = webkit_dom_node_get_next_sibling (item);

		if (WEBKIT_DOM_IS_HTML_LI_ELEMENT (item)) {
			if (after)
				prev_item = webkit_dom_node_append_child (
					source_list_clone, WEBKIT_DOM_NODE (item), NULL);
			else
				prev_item = webkit_dom_node_insert_before (
					webkit_dom_node_get_parent_node (prev_item),
					item,
					webkit_dom_node_get_next_sibling (prev_item),
					NULL);
		}

		if (webkit_dom_node_contains (item, WEBKIT_DOM_NODE (selection_end_marker)))
			after = TRUE;

		if (!next_item) {
			if (after)
				break;

			current_list = webkit_dom_node_get_next_sibling (current_list);
			next_item = webkit_dom_node_get_first_child (current_list);
		}
		item = next_item;
	}

	remove_node_if_empty (source_list_clone);
	remove_node_if_empty (source_list);
}

static void
unindent_block (WebKitDOMDocument *document,
                WebKitDOMNode *block)
{
	gboolean before_node = TRUE;
	gint word_wrap_length = WORD_WRAP_LENGTH;
	gint level, width;
	EHTMLEditorSelectionAlignment alignment;
	WebKitDOMElement *element;
	WebKitDOMElement *prev_blockquote = NULL, *next_blockquote = NULL;
	WebKitDOMNode *block_to_process, *node_clone, *child;

	block_to_process = block;

	alignment = e_html_editor_selection_get_alignment_from_node (block_to_process);

	element = webkit_dom_node_get_parent_element (block_to_process);

	if (!WEBKIT_DOM_IS_HTML_DIV_ELEMENT (element) &&
	    !element_has_class (element, "-x-evo-indented"))
		return;

	element_add_class (WEBKIT_DOM_ELEMENT (block_to_process), "-x-evo-to-unindent");

	level = get_indentation_level (element);
	width = word_wrap_length - SPACES_PER_INDENTATION * level;

	/* Look if we have previous siblings, if so, we have to
	 * create new blockquote that will include them */
	if (webkit_dom_node_get_previous_sibling (block_to_process))
		prev_blockquote = e_html_editor_selection_get_indented_element (
			document, width);

	/* Look if we have next siblings, if so, we have to
	 * create new blockquote that will include them */
	if (webkit_dom_node_get_next_sibling (block_to_process))
		next_blockquote = e_html_editor_selection_get_indented_element (
			document, width);

	/* Copy nodes that are before / after the element that we want to unindent */
	while ((child = webkit_dom_node_get_first_child (WEBKIT_DOM_NODE (element)))) {
		if (webkit_dom_node_is_equal_node (child, block_to_process)) {
			before_node = FALSE;
			node_clone = webkit_dom_node_clone_node (child, TRUE);
			remove_node (child);
			continue;
		}

		webkit_dom_node_append_child (
			before_node ?
				WEBKIT_DOM_NODE (prev_blockquote) :
				WEBKIT_DOM_NODE (next_blockquote),
			child,
			NULL);
	}

	element_remove_class (WEBKIT_DOM_ELEMENT (node_clone), "-x-evo-to-unindent");

	/* Insert blockqoute with nodes that were before the element that we want to unindent */
	if (prev_blockquote) {
		if (webkit_dom_node_has_child_nodes (WEBKIT_DOM_NODE (prev_blockquote))) {
			webkit_dom_node_insert_before (
				webkit_dom_node_get_parent_node (WEBKIT_DOM_NODE (element)),
				WEBKIT_DOM_NODE (prev_blockquote),
				WEBKIT_DOM_NODE (element),
				NULL);
		}
	}

	if (level == 1 && element_has_class (WEBKIT_DOM_ELEMENT (node_clone), "-x-evo-paragraph")) {
		e_html_editor_selection_dom_set_paragraph_style (
			document, WEBKIT_DOM_ELEMENT (node_clone), word_wrap_length, 0, "");
		element_add_class (
			WEBKIT_DOM_ELEMENT (node_clone),
			get_css_alignment_value_class (alignment));
	}

	/* Insert the unindented element */
	webkit_dom_node_insert_before (
		webkit_dom_node_get_parent_node (WEBKIT_DOM_NODE (element)),
		node_clone,
		WEBKIT_DOM_NODE (element),
		NULL);

	/* Insert blockqoute with nodes that were after the element that we want to unindent */
	if (next_blockquote) {
		if (webkit_dom_node_has_child_nodes (WEBKIT_DOM_NODE (next_blockquote))) {
			webkit_dom_node_insert_before (
				webkit_dom_node_get_parent_node (WEBKIT_DOM_NODE (element)),
				WEBKIT_DOM_NODE (next_blockquote),
				WEBKIT_DOM_NODE (element),
				NULL);
		}
	}

	/* Remove old blockquote */
	remove_node (WEBKIT_DOM_NODE (element));
}

/**
 * e_html_editor_selection_unindent:
 * @selection: an #EHTMLEditorSelection
 *
 * Unindents current paragraph by one level.
 */
static void
e_html_editor_selection_unindent (WebKitDOMDocument *document)
{
	gboolean after_selection_start = FALSE, after_selection_end = FALSE;
	WebKitDOMElement *selection_start_marker, *selection_end_marker;
	WebKitDOMNode *block;

	e_html_editor_selection_dom_save (document);

	selection_start_marker = webkit_dom_document_query_selector (
		document, "span#-x-evo-selection-start-marker", NULL);
	selection_end_marker = webkit_dom_document_query_selector (
		document, "span#-x-evo-selection-end-marker", NULL);

	/* If the selection was not saved, move it into the first child of body */
	if (!selection_start_marker || !selection_end_marker) {
		WebKitDOMHTMLElement *body;

		body = webkit_dom_document_get_body (document);
		selection_start_marker = webkit_dom_document_create_element (
			document, "SPAN", NULL);
		webkit_dom_element_set_id (
			selection_start_marker, "-x-evo-selection-start-marker");
		webkit_dom_node_insert_before (
			webkit_dom_node_get_first_child (WEBKIT_DOM_NODE (body)),
			WEBKIT_DOM_NODE (selection_start_marker),
			webkit_dom_node_get_first_child (
				webkit_dom_node_get_first_child (
					WEBKIT_DOM_NODE (body))),
			NULL);
		selection_end_marker = webkit_dom_document_create_element (
			document, "SPAN", NULL);
		webkit_dom_element_set_id (
			selection_end_marker, "-x-evo-selection-end-marker");
		webkit_dom_node_insert_before (
			webkit_dom_node_get_first_child (WEBKIT_DOM_NODE (body)),
			WEBKIT_DOM_NODE (selection_end_marker),
			webkit_dom_node_get_first_child (
				webkit_dom_node_get_first_child (
					WEBKIT_DOM_NODE (body))),
			NULL);
	}

	block = get_parent_indented_block (
		WEBKIT_DOM_NODE (selection_start_marker));
	if (!block)
		block = get_parent_block_node_from_child (
			WEBKIT_DOM_NODE (selection_start_marker));

	while (block && !after_selection_end) {
		gint ii, length;
		WebKitDOMNode *next_block;
		WebKitDOMNodeList *list;

		next_block = webkit_dom_node_get_next_sibling (block);

		list = webkit_dom_element_query_selector_all (
			WEBKIT_DOM_ELEMENT (block),
			".-x-evo-indented > *:not(.-x-evo-indented):not(li)",
			NULL);

		after_selection_end = webkit_dom_node_contains (
			block, WEBKIT_DOM_NODE (selection_end_marker));

		length = webkit_dom_node_list_get_length (list);
		if (length == 0 && node_is_list_or_item (block)) {
			unindent_list (document);
			goto next;
		}

		if (length == 0) {
			if (!after_selection_start) {
				after_selection_start = webkit_dom_node_contains (
					block, WEBKIT_DOM_NODE (selection_start_marker));
				if (!after_selection_start)
					goto next;
			}

			unindent_block (document, block);

			if (after_selection_end)
				goto next;
		}

		for (ii = 0; ii < length; ii++) {
			WebKitDOMNode *block_to_process;

			block_to_process = webkit_dom_node_list_item (list, ii);

			after_selection_end = webkit_dom_node_contains (
				block_to_process,
				WEBKIT_DOM_NODE (selection_end_marker));

			if (!after_selection_start) {
				after_selection_start = webkit_dom_node_contains (
					block_to_process,
					WEBKIT_DOM_NODE (selection_start_marker));
				if (!after_selection_start)
					continue;
			}

			unindent_block (document, block_to_process);

			if (after_selection_end)
				break;
		}
 next:
		g_object_unref (list);
		block = next_block;
	}

	e_html_editor_selection_dom_restore (document);
/* FIXME WK2
	e_html_editor_view_force_spell_check_for_current_paragraph (view);

	g_object_notify (G_OBJECT (selection), "indented"); */
}

static WebKitDOMNode *
in_empty_block_in_quoted_content (WebKitDOMNode *element)
{
	WebKitDOMNode *first_child, *next_sibling;

	if (!WEBKIT_DOM_IS_HTML_DIV_ELEMENT (element))
		return NULL;

	first_child = webkit_dom_node_get_first_child (element);
	if (!WEBKIT_DOM_IS_ELEMENT (first_child))
		return NULL;

	if (!element_has_class (WEBKIT_DOM_ELEMENT (first_child), "-x-evo-quoted"))
		return NULL;

	next_sibling = webkit_dom_node_get_next_sibling (first_child);
	if (WEBKIT_DOM_IS_HTML_BR_ELEMENT (next_sibling))
		return next_sibling;

	if (!WEBKIT_DOM_IS_ELEMENT (next_sibling))
		return NULL;

	if (!element_has_id (WEBKIT_DOM_ELEMENT (next_sibling), "-x-evo-selection-start-marker"))
		return NULL;

	next_sibling = webkit_dom_node_get_next_sibling (next_sibling);
	if (WEBKIT_DOM_IS_HTML_BR_ELEMENT (next_sibling))
		return next_sibling;

	return NULL;
}

/**
 * e_html_editor_selection_save:
 * @selection: an #EHTMLEditorSelection
 *
 * Saves current cursor position or current selection range. The selection can
 * be later restored by calling e_html_editor_selection_restore().
 *
 * Note that calling e_html_editor_selection_save() overwrites previously saved
 * position.
 *
 * Note that this method inserts special markings into the HTML code that are
 * used to later restore the selection. It can happen that by deleting some
 * segments of the document some of the markings are deleted too. In that case
 * restoring the selection by e_html_editor_selection_restore() can fail. Also by
 * moving text segments (Cut & Paste) can result in moving the markings
 * elsewhere, thus e_html_editor_selection_restore() will restore the selection
 * incorrectly.
 *
 * It is recommended to use this method only when you are not planning to make
 * bigger changes to content or structure of the document (formatting changes
 * are usually OK).
 */
void
e_html_editor_selection_dom_save (WebKitDOMDocument *document)
{
	glong offset;
	WebKitDOMRange *range;
	WebKitDOMNode *container, *next_sibling, *marker_node;
	WebKitDOMNode *split_node, *parent_node;
	WebKitDOMElement *marker;

	/* First remove all markers (if present) */
	marker = webkit_dom_document_get_element_by_id (
		document, "-x-evo-selection-start-marker");
	if (marker != NULL)
		remove_node (WEBKIT_DOM_NODE (marker));

	marker = webkit_dom_document_get_element_by_id (
		document, "-x-evo-selection-end-marker");
	if (marker != NULL)
		remove_node (WEBKIT_DOM_NODE (marker));

	range = html_editor_selection_dom_get_current_range (document);

	if (!range)
		return;

	marker = webkit_dom_document_create_element (document, "SPAN", NULL);
	webkit_dom_element_set_id (marker, "-x-evo-selection-start-marker");

	container = webkit_dom_range_get_start_container (range, NULL);
	offset = webkit_dom_range_get_start_offset (range, NULL);

	if (WEBKIT_DOM_IS_TEXT (container)) {
		if (offset != 0) {
			WebKitDOMText *split_text;

			split_text = webkit_dom_text_split_text (
				WEBKIT_DOM_TEXT (container), offset, NULL);
			split_node = WEBKIT_DOM_NODE (split_text);
		} else {
			marker_node = webkit_dom_node_insert_before (
				webkit_dom_node_get_parent_node (container),
				WEBKIT_DOM_NODE (marker),
				container,
				NULL);
			goto end_marker;
		}
	} else if (WEBKIT_DOM_IS_HTML_LI_ELEMENT (container)) {
		marker_node = webkit_dom_node_insert_before (
			container,
			WEBKIT_DOM_NODE (marker),
			webkit_dom_node_get_first_child (container),
			NULL);
		goto end_marker;
	} else {
		/* Insert the selection marker on the right position in
		 * an empty paragraph in the quoted content */
		if ((next_sibling = in_empty_block_in_quoted_content (container))) {
			marker_node = webkit_dom_node_insert_before (
				container,
				WEBKIT_DOM_NODE (marker),
				next_sibling,
				NULL);
			goto end_marker;
		}
		if (!webkit_dom_node_get_previous_sibling (container)) {
			marker_node = webkit_dom_node_insert_before (
				container,
				WEBKIT_DOM_NODE (marker),
				webkit_dom_node_get_first_child (container),
				NULL);
			goto end_marker;
		} else if (!webkit_dom_node_get_next_sibling (container)) {
			marker_node = webkit_dom_node_append_child (
				container,
				WEBKIT_DOM_NODE (marker),
				NULL);
			goto end_marker;
		} else {
			if (webkit_dom_node_get_first_child (container)) {
				marker_node = webkit_dom_node_insert_before (
					container,
					WEBKIT_DOM_NODE (marker),
					webkit_dom_node_get_first_child (container),
					NULL);
				goto end_marker;
			}
			split_node = container;
		}
	}

	/* Don't save selection straight into body */
	if (WEBKIT_DOM_IS_HTML_BODY_ELEMENT (split_node))
		return;

	if (!split_node) {
		marker_node = webkit_dom_node_insert_before (
			container,
			WEBKIT_DOM_NODE (marker),
			webkit_dom_node_get_first_child (
				WEBKIT_DOM_NODE (container)),
			NULL);
	} else {
		marker_node = WEBKIT_DOM_NODE (marker);
		parent_node = webkit_dom_node_get_parent_node (split_node);

		webkit_dom_node_insert_before (
			parent_node, marker_node, split_node, NULL);
	}

 end_marker:
	marker = webkit_dom_document_create_element (document, "SPAN", NULL);
	webkit_dom_element_set_id (marker, "-x-evo-selection-end-marker");

	if (webkit_dom_range_get_collapsed (range, NULL)) {
		webkit_dom_node_insert_before (
			/* Selection start marker */
			webkit_dom_node_get_parent_node (marker_node),
			WEBKIT_DOM_NODE (marker),
			webkit_dom_node_get_next_sibling (marker_node),
			NULL);
		return;
	}

	container = webkit_dom_range_get_end_container (range, NULL);
	offset = webkit_dom_range_get_end_offset (range, NULL);

	if (WEBKIT_DOM_IS_TEXT (container)) {
		if (offset != 0) {
			WebKitDOMText *split_text;

			split_text = webkit_dom_text_split_text (
				WEBKIT_DOM_TEXT (container), offset, NULL);
			split_node = WEBKIT_DOM_NODE (split_text);
		} else {
			marker_node = webkit_dom_node_insert_before (
				webkit_dom_node_get_parent_node (container),
				WEBKIT_DOM_NODE (marker),
				container,
				NULL);
			goto check;

		}
	} else if (WEBKIT_DOM_IS_HTML_LI_ELEMENT (container)) {
		webkit_dom_node_append_child (
			container, WEBKIT_DOM_NODE (marker), NULL);
		return;
	} else {
		/* Insert the selection marker on the right position in
		 * an empty paragraph in the quoted content */
		if ((next_sibling = in_empty_block_in_quoted_content (container))) {
			webkit_dom_node_insert_before (
				container,
				WEBKIT_DOM_NODE (marker),
				next_sibling,
				NULL);
			return;
		}
		if (!webkit_dom_node_get_previous_sibling (container)) {
			split_node = webkit_dom_node_get_parent_node (container);
		} else if (!webkit_dom_node_get_next_sibling (container)) {
			split_node = webkit_dom_node_get_parent_node (container);
			split_node = webkit_dom_node_get_next_sibling (split_node);
		} else
			split_node = container;
	}

	/* Don't save selection straight into body */
	if (WEBKIT_DOM_IS_HTML_BODY_ELEMENT (split_node)) {
		marker = webkit_dom_document_get_element_by_id (
			document, "-x-evo-selection-start-marker");
		remove_node (WEBKIT_DOM_NODE (marker));
		return;
	}

	marker_node = WEBKIT_DOM_NODE (marker);

	if (split_node) {
		parent_node = webkit_dom_node_get_parent_node (split_node);

		webkit_dom_node_insert_before (
			parent_node, marker_node, split_node, NULL);
	} else
		webkit_dom_node_append_child (
			WEBKIT_DOM_NODE (container), marker_node, NULL);

 check:
	if ((next_sibling = webkit_dom_node_get_next_sibling (marker_node))) {
		if (!WEBKIT_DOM_IS_ELEMENT (next_sibling))
			next_sibling = webkit_dom_node_get_next_sibling (next_sibling);
		/* If the selection is collapsed ensure that the selection start marker
		 * is before the end marker */
		if (next_sibling && WEBKIT_DOM_IS_ELEMENT (next_sibling) &&
		    element_has_id (WEBKIT_DOM_ELEMENT (next_sibling), "-x-evo-selection-start-marker")) {
			webkit_dom_node_insert_before (
				webkit_dom_node_get_parent_node (marker_node),
				next_sibling,
				marker_node,
				NULL);
		}
	}
}

static gboolean
is_selection_position_node (WebKitDOMNode *node)
{
	WebKitDOMElement *element;

	if (!node || !WEBKIT_DOM_IS_ELEMENT (node))
		return FALSE;

	element = WEBKIT_DOM_ELEMENT (node);

	return element_has_id (element, "-x-evo-caret-position") ||
	       element_has_id (element, "-x-evo-selection-start-marker") ||
	       element_has_id (element, "-x-evo-selection-end-marker");
}

/**
 * e_html_editor_selection_restore:
 * @selection: an #EHTMLEditorSelection
 *
 * Restores cursor position or selection range that was saved by
 * e_html_editor_selection_save().
 *
 * Note that calling this function without calling e_html_editor_selection_save()
 * before is a programming error and the behavior is undefined.
 */
void
e_html_editor_selection_dom_restore (WebKitDOMDocument *document)
{
	WebKitDOMElement *marker;
	WebKitDOMNode *selection_start_marker, *selection_end_marker;
	WebKitDOMRange *range;
	WebKitDOMDOMSelection *dom_selection;
	WebKitDOMDOMWindow *window;

	window = webkit_dom_document_get_default_view (document);
	dom_selection = webkit_dom_dom_window_get_selection (window);
	range = webkit_dom_dom_selection_get_range_at (dom_selection, 0, NULL);
	if (!range)
		return;

	selection_start_marker = webkit_dom_range_get_start_container (range, NULL);
	if (selection_start_marker) {
		gboolean ok = FALSE;
		selection_start_marker =
			webkit_dom_node_get_next_sibling (selection_start_marker);

		ok = is_selection_position_node (selection_start_marker);

		if (ok) {
			ok = FALSE;
			if (webkit_dom_range_get_collapsed (range, NULL)) {
				selection_end_marker = webkit_dom_node_get_next_sibling (
					selection_start_marker);

				ok = is_selection_position_node (selection_end_marker);
				if (ok) {
					remove_node (selection_start_marker);
					remove_node (selection_end_marker);

					return;
				}
			}
		}
	}

	range = webkit_dom_document_create_range (document);
	if (!range)
		return;

	marker = webkit_dom_document_get_element_by_id (
		document, "-x-evo-selection-start-marker");
	if (!marker) {
		marker = webkit_dom_document_get_element_by_id (
			document, "-x-evo-selection-end-marker");
		if (marker)
			remove_node (WEBKIT_DOM_NODE (marker));
		return;
	}

	webkit_dom_range_set_start_after (range, WEBKIT_DOM_NODE (marker), NULL);
	remove_node (WEBKIT_DOM_NODE (marker));

	marker = webkit_dom_document_get_element_by_id (
		document, "-x-evo-selection-end-marker");
	if (!marker) {
		marker = webkit_dom_document_get_element_by_id (
			document, "-x-evo-selection-start-marker");
		if (marker)
			remove_node (WEBKIT_DOM_NODE (marker));
		return;
	}

	webkit_dom_range_set_end_before (range, WEBKIT_DOM_NODE (marker), NULL);
	remove_node (WEBKIT_DOM_NODE (marker));

	webkit_dom_dom_selection_remove_all_ranges (dom_selection);
	webkit_dom_dom_selection_add_range (dom_selection, range);
}

static gint
find_where_to_break_line (WebKitDOMNode *node,
                          gint max_len,
			  gint word_wrap_length)
{
	gchar *str, *text_start;
	gunichar uc;
	gint pos;
	gint last_space = 0;
	gint length;
	gint ret_val = 0;
	gchar* position;

	text_start =  webkit_dom_character_data_get_data (WEBKIT_DOM_CHARACTER_DATA (node));
	length = g_utf8_strlen (text_start, -1);

	pos = 1;
	last_space = 0;
	str = text_start;
	do {
		uc = g_utf8_get_char (str);
		if (!uc) {
			g_free (text_start);
			if (pos <= max_len)
				return pos;
			else
				return last_space;
		}

		/* If last_space is zero then the word is longer than
		 * word_wrap_length characters, so continue until we find
		 * a space */
		if ((pos > max_len) && (last_space > 0)) {
			if (last_space > word_wrap_length) {
				g_free (text_start);
				return last_space - 1;
			}

			if (last_space > max_len) {
				if (g_unichar_isspace (g_utf8_get_char (text_start)))
					ret_val = 1;

				g_free (text_start);
				return ret_val;
			}

			if (last_space == max_len - 1) {
				uc = g_utf8_get_char (str);
				if (g_unichar_isspace (uc) || str[0] == '-')
					last_space++;
			}

			g_free (text_start);
			return last_space;
		}

		if (g_unichar_isspace (uc) || str[0] == '-')
			last_space = pos;

		pos += 1;
		str = g_utf8_next_char (str);
	} while (*str);

	position = g_utf8_offset_to_pointer (text_start, max_len);

	if (g_unichar_isspace (g_utf8_get_char (position))) {
		ret_val = max_len + 1;
	} else {
		if (last_space == 0) {
			/* If word is longer than word_wrap_length, we cannot wrap it */
			ret_val = length;
		} else if (last_space < max_len) {
			ret_val = last_space;
		} else {
			if (length > word_wrap_length)
				ret_val = last_space;
			else
				ret_val = 0;
		}
	}

	g_free (text_start);

	return ret_val;
}

/**
 * e_html_editor_selection_is_collapsed:
 * @selection: an #EHTMLEditorSelection
 *
 * Returns if selection is collapsed.
 *
 * Returns: Whether the selection is collapsed (just caret) or not (someting is selected).
 */
static gboolean
e_html_editor_selection_is_collapsed (WebKitDOMDocument *document)
{
	WebKitDOMRange *range;

	range = html_editor_selection_dom_get_current_range (document);
	if (!range)
		return TRUE;

	return webkit_dom_range_get_collapsed (range, NULL);
}

/**
 * e_html_editor_selection_get_string:
 * @selection: an #EHTMLEditorSelection
 *
 * Returns currently selected string.
 *
 * Returns: A pointer to content of current selection. The string is owned by
 * #EHTMLEditorSelection and should not be free'd.
 */
static gchar *
e_html_editor_selection_get_string (WebKitDOMDocument *document)
{
	WebKitDOMRange *range;

	range = html_editor_selection_dom_get_current_range (document);
	if (!range)
		return NULL;

	return webkit_dom_range_get_text (range);
}

static WebKitDOMElement *
wrap_lines (WebKitDOMNode *paragraph,
	    WebKitDOMDocument *document,
	    gboolean remove_all_br,
	    gint word_wrap_length)
{
	WebKitDOMNode *node, *start_node;
	WebKitDOMNode *paragraph_clone;
	WebKitDOMDocumentFragment *fragment;
	WebKitDOMElement *element;
	WebKitDOMNodeList *wrap_br;
	gboolean has_selection;
	gint len, ii, br_count;
	gulong length_left;
	glong paragraph_char_count;
	gchar *text_content;

	has_selection = !e_html_editor_selection_is_collapsed (document);

	if (has_selection) {
		gchar *text_content;

		text_content = e_html_editor_selection_get_string (document);
		paragraph_char_count = g_utf8_strlen (text_content, -1);
		g_free (text_content);

		fragment = webkit_dom_range_clone_contents (
			html_editor_selection_dom_get_current_range (document), NULL);

		/* Select all BR elements or just ours that are used for wrapping.
		 * We are not removing user BR elements when this function is activated
		 * from Format->Wrap Lines action */
		wrap_br = webkit_dom_document_fragment_query_selector_all (
			fragment,
			remove_all_br ? "br" : "br.-x-evo-wrap-br",
			NULL);
		br_count = webkit_dom_node_list_get_length (wrap_br);
		/* And remove them */
		for (ii = 0; ii < br_count; ii++)
			remove_node (webkit_dom_node_list_item (wrap_br, ii));
		g_object_unref (wrap_br);
	} else {
		if (!webkit_dom_node_has_child_nodes (paragraph))
			return WEBKIT_DOM_ELEMENT (paragraph);

		paragraph_clone = webkit_dom_node_clone_node (paragraph, TRUE);
		element = webkit_dom_element_query_selector (
			WEBKIT_DOM_ELEMENT (paragraph_clone),
			"span#-x-evo-caret-position",
			NULL);
		text_content = webkit_dom_node_get_text_content (paragraph_clone);
		paragraph_char_count = g_utf8_strlen (text_content, -1);
		if (element)
			paragraph_char_count--;
		g_free (text_content);

		/* When we wrap, we are wrapping just the text after caret, text
		 * before the caret is already wrapped, so unwrap the text after
		 * the caret position */
		element = webkit_dom_element_query_selector (
			WEBKIT_DOM_ELEMENT (paragraph_clone),
			"span#-x-evo-selection-end-marker",
			NULL);

		if (element) {
			WebKitDOMNode *nd = WEBKIT_DOM_NODE (element);

			while (nd) {
				WebKitDOMNode *next_nd = webkit_dom_node_get_next_sibling (nd);
				if (WEBKIT_DOM_IS_HTML_BR_ELEMENT (nd)) {
					if (remove_all_br)
						remove_node (nd);
					else if (element_has_class (WEBKIT_DOM_ELEMENT (nd), "-x-evo-wrap-br"))
						remove_node (nd);
				}
				nd = next_nd;
			}
		}
	}

	if (has_selection) {
		node = WEBKIT_DOM_NODE (fragment);
		start_node = node;
	} else {
		webkit_dom_node_normalize (paragraph_clone);
		node = webkit_dom_node_get_first_child (paragraph_clone);
		if (node) {
			text_content = webkit_dom_node_get_text_content (node);
			if (g_strcmp0 ("\n", text_content) == 0)
				node = webkit_dom_node_get_next_sibling (node);
			g_free (text_content);
		}

		/* We have to start from the end of the last wrapped line */
		element = webkit_dom_element_query_selector (
			WEBKIT_DOM_ELEMENT (paragraph_clone),
			"span#-x-evo-selection-start-marker",
			NULL);

		if (element) {
			WebKitDOMNode *nd = WEBKIT_DOM_NODE (element);

			while ((nd = webkit_dom_node_get_previous_sibling (nd))) {
				if (WEBKIT_DOM_IS_HTML_BR_ELEMENT (nd)) {
					element = WEBKIT_DOM_ELEMENT (nd);
					break;
				} else
					element = NULL;
			}
		}

		if (element) {
			node = webkit_dom_node_get_next_sibling (WEBKIT_DOM_NODE (element));
			start_node = paragraph_clone;
		} else
			start_node = node;
	}

	len = 0;
	while (node) {
		gint offset = 0;

		if (WEBKIT_DOM_IS_TEXT (node)) {
			const gchar *newline;
			WebKitDOMNode *next_sibling;

			/* If there is temporary hidden space we remove it */
			text_content = webkit_dom_node_get_text_content (node);
			if (strstr (text_content, UNICODE_ZERO_WIDTH_SPACE)) {
				if (g_str_has_prefix (text_content, UNICODE_ZERO_WIDTH_SPACE))
					webkit_dom_character_data_delete_data (
						WEBKIT_DOM_CHARACTER_DATA (node),
						0,
						1,
						NULL);
				if (g_str_has_suffix (text_content, UNICODE_ZERO_WIDTH_SPACE))
					webkit_dom_character_data_delete_data (
						WEBKIT_DOM_CHARACTER_DATA (node),
						g_utf8_strlen (text_content, -1) - 1,
						1,
						NULL);
				g_free (text_content);
				text_content = webkit_dom_node_get_text_content (node);
			}
			newline = g_strstr_len (text_content, -1, "\n");

			next_sibling = node;
			while (newline) {
				next_sibling = WEBKIT_DOM_NODE (webkit_dom_text_split_text (
					WEBKIT_DOM_TEXT (next_sibling),
					g_utf8_pointer_to_offset (text_content, newline),
					NULL));

				if (!next_sibling)
					break;

				element = webkit_dom_document_create_element (
					document, "BR", NULL);
				element_add_class (element, "-x-evo-temp-wrap-text-br");

				webkit_dom_node_insert_before (
					webkit_dom_node_get_parent_node (next_sibling),
					WEBKIT_DOM_NODE (element),
					next_sibling,
					NULL);

				g_free (text_content);

				text_content = webkit_dom_node_get_text_content (next_sibling);
				if (g_str_has_prefix (text_content, "\n")) {
					webkit_dom_character_data_delete_data (
						WEBKIT_DOM_CHARACTER_DATA (next_sibling), 0, 1, NULL);
					g_free (text_content);
					text_content =
						webkit_dom_node_get_text_content (next_sibling);
				}
				newline = g_strstr_len (text_content, -1, "\n");
			}
			g_free (text_content);
		} else {
			if (is_selection_position_node (node)) {
				node = webkit_dom_node_get_next_sibling (node);
				continue;
			}

			/* If element is ANCHOR we wrap it separately */
			if (WEBKIT_DOM_IS_HTML_ANCHOR_ELEMENT (node)) {
				glong anchor_length;

				text_content = webkit_dom_node_get_text_content (node);
				anchor_length = g_utf8_strlen (text_content, -1);
				if (len + anchor_length > word_wrap_length) {
					element = webkit_dom_document_create_element (
						document, "BR", NULL);
					element_add_class (element, "-x-evo-wrap-br");
					webkit_dom_node_insert_before (
						webkit_dom_node_get_parent_node (node),
						WEBKIT_DOM_NODE (element),
						node,
						NULL);
					len = anchor_length;
				} else
					len += anchor_length;

				g_free (text_content);
				/* If there is space after the anchor don't try to
				 * wrap before it */
				node = webkit_dom_node_get_next_sibling (node);
				if (WEBKIT_DOM_IS_TEXT (node)) {
					text_content = webkit_dom_node_get_text_content (node);
					if (g_strcmp0 (text_content, " ") == 0) {
						node = webkit_dom_node_get_next_sibling (node);
						len++;
					}
					g_free (text_content);
				}
				continue;
			}

			/* When we are not removing user-entered BR elements (lines wrapped by user),
			 * we need to skip those elements */
			if (!remove_all_br && WEBKIT_DOM_IS_HTML_BR_ELEMENT (node)) {
				if (!element_has_class (WEBKIT_DOM_ELEMENT (node), "-x-evo-wrap-br")) {
					len = 0;
					node = webkit_dom_node_get_next_sibling (node);
					continue;
				}
			}
			goto next_node;
		}

		/* If length of this node + what we already have is still less
		 * then word_wrap_length characters, then just join it and continue to next
		 * node */
		length_left = webkit_dom_character_data_get_length (
			WEBKIT_DOM_CHARACTER_DATA (node));

		if ((length_left + len) < word_wrap_length) {
			len += length_left;
			goto next_node;
		}

		/* wrap until we have something */
		while ((length_left + len) > word_wrap_length) {
			gint max_length;

			max_length = word_wrap_length - len;
			if (max_length < 0)
				max_length = word_wrap_length;
			/* Find where we can line-break the node so that it
			 * effectively fills the rest of current row */
			offset = find_where_to_break_line (
				node, max_length, word_wrap_length);

			element = webkit_dom_document_create_element (document, "BR", NULL);
			element_add_class (element, "-x-evo-wrap-br");

			if (offset > 0 && offset <= word_wrap_length) {
				if (offset != length_left)
					webkit_dom_text_split_text (
						WEBKIT_DOM_TEXT (node), offset, NULL);

				if (webkit_dom_node_get_next_sibling (node)) {
					gchar *nd_content;
					WebKitDOMNode *nd = webkit_dom_node_get_next_sibling (node);

					nd = webkit_dom_node_get_next_sibling (node);
					nd_content = webkit_dom_node_get_text_content (nd);
					if (nd_content && *nd_content) {
						if (g_str_has_prefix (nd_content, " "))
							webkit_dom_character_data_replace_data (
								WEBKIT_DOM_CHARACTER_DATA (nd), 0, 1, "", NULL);
						g_free (nd_content);
						nd_content = webkit_dom_node_get_text_content (nd);
						if (g_strcmp0 (nd_content, UNICODE_NBSP) == 0)
							remove_node (nd);
						g_free (nd_content);
					}

					webkit_dom_node_insert_before (
						webkit_dom_node_get_parent_node (node),
						WEBKIT_DOM_NODE (element),
						nd,
						NULL);
				} else {
					webkit_dom_node_append_child (
						webkit_dom_node_get_parent_node (node),
						WEBKIT_DOM_NODE (element),
						NULL);
				}
			} else if (offset > word_wrap_length) {
				if (offset != length_left)
					webkit_dom_text_split_text (
						WEBKIT_DOM_TEXT (node), offset + 1, NULL);

				if (webkit_dom_node_get_next_sibling (node)) {
					gchar *nd_content;
					WebKitDOMNode *nd = webkit_dom_node_get_next_sibling (node);

					nd = webkit_dom_node_get_next_sibling (node);
					nd_content = webkit_dom_node_get_text_content (nd);
					if (nd_content && *nd_content) {
						if (g_str_has_prefix (nd_content, " "))
							webkit_dom_character_data_replace_data (
								WEBKIT_DOM_CHARACTER_DATA (nd), 0, 1, "", NULL);
						g_free (nd_content);
						nd_content = webkit_dom_node_get_text_content (nd);
						if (g_strcmp0 (nd_content, UNICODE_NBSP) == 0)
							remove_node (nd);
						g_free (nd_content);
					}

					webkit_dom_node_insert_before (
						webkit_dom_node_get_parent_node (node),
						WEBKIT_DOM_NODE (element),
						nd,
						NULL);
				} else {
					webkit_dom_node_append_child (
						webkit_dom_node_get_parent_node (node),
						WEBKIT_DOM_NODE (element),
						NULL);
				}
				len = 0;
				break;
			} else {
				webkit_dom_node_insert_before (
					webkit_dom_node_get_parent_node (node),
					WEBKIT_DOM_NODE (element),
					node,
					NULL);
			}
			length_left = webkit_dom_character_data_get_length (
				WEBKIT_DOM_CHARACTER_DATA (node));

			len = 0;
		}
		len += length_left - offset;
 next_node:
		if (WEBKIT_DOM_IS_HTML_LI_ELEMENT (node))
			len = 0;

		/* Move to next node */
		if (webkit_dom_node_has_child_nodes (node)) {
			node = webkit_dom_node_get_first_child (node);
		} else if (webkit_dom_node_get_next_sibling (node)) {
			node = webkit_dom_node_get_next_sibling (node);
		} else {
			if (webkit_dom_node_is_equal_node (node, start_node))
				break;

			node = webkit_dom_node_get_parent_node (node);
			if (node)
				node = webkit_dom_node_get_next_sibling (node);
		}
	}

	if (has_selection) {
		gchar *html;

		/* Create a wrapper DIV and put the processed content into it */
		element = webkit_dom_document_create_element (document, "DIV", NULL);
		element_add_class (element, "-x-evo-paragraph");
		webkit_dom_node_append_child (
			WEBKIT_DOM_NODE (element),
			WEBKIT_DOM_NODE (start_node),
			NULL);

		webkit_dom_node_normalize (WEBKIT_DOM_NODE (element));
		/* Get HTML code of the processed content */
		html = webkit_dom_html_element_get_inner_html (WEBKIT_DOM_HTML_ELEMENT (element));

		/* Overwrite the current selection be the processed content */
		e_html_editor_selection_insert_html (selection, html);

		g_free (html);

		return NULL;
	} else {
		webkit_dom_node_normalize (paragraph_clone);

		node = webkit_dom_node_get_parent_node (paragraph);
		if (node) {
			/* Replace paragraph with wrapped one */
			webkit_dom_node_replace_child (
				node, paragraph_clone, paragraph, NULL);
		}

		return WEBKIT_DOM_ELEMENT (paragraph_clone);
	}
}

static WebKitDOMElement *
e_html_editor_selection_get_paragraph_element (WebKitDOMDocument *document,
                                               gint width,
                                               gint offset)
{
	WebKitDOMElement *element;

	element = webkit_dom_document_create_element (document, "DIV", NULL);
	e_html_editor_selection_dom_set_paragraph_style (document, element, width, offset, "");

	return element;
}

static WebKitDOMElement *
e_html_editor_selection_put_node_into_paragraph (WebKitDOMDocument *document,
                                                 WebKitDOMNode *node,
                                                 WebKitDOMNode *caret_position)
{
	WebKitDOMRange *range;
	WebKitDOMElement *container;

	range = webkit_dom_document_create_range (document);
	container = e_html_editor_selection_get_paragraph_element (document, -1, 0);
	webkit_dom_range_select_node (range, node, NULL);
	webkit_dom_range_surround_contents (range, WEBKIT_DOM_NODE (container), NULL);
	/* We have to move caret position inside this container */
	webkit_dom_node_append_child (WEBKIT_DOM_NODE (container), caret_position, NULL);

	return container;
}

/**
 * e_html_editor_selection_wrap_lines:
 * @selection: an #EHTMLEditorSelection
 *
 * Wraps all lines in current selection to be 71 characters long.
 */
#if 0
void
e_html_editor_selection_wrap_lines (WebKitDOMDocument *document)
{
	WebKitDOMRange *range;
	WebKitDOMElement *active_paragraph, *caret;

	caret = e_html_editor_selection_dom_save_caret_position (document);
	if (e_html_editor_selection_is_collapsed (selection)) {
		WebKitDOMNode *end_container;
		WebKitDOMNode *parent;
		WebKitDOMNode *paragraph;
		gchar *text_content;

		/* We need to save caret position and restore it after
		 * wrapping the selection, but we need to save it before we
		 * start to modify selection */
		range = html_editor_selection_get_current_range (selection);
		if (!range)
			return;

		end_container = webkit_dom_range_get_common_ancestor_container (range, NULL);

		/* Wrap only text surrounded in DIV and P tags */
		parent = webkit_dom_node_get_parent_node(end_container);
		if (WEBKIT_DOM_IS_HTML_DIV_ELEMENT (parent) ||
		    WEBKIT_DOM_IS_HTML_PARAGRAPH_ELEMENT (parent)) {
			element_add_class (
				WEBKIT_DOM_ELEMENT (parent), "-x-evo-paragraph");
			paragraph = parent;
		} else {
			WebKitDOMElement *parent_div =
				e_html_editor_dom_node_find_parent_element (parent, "DIV");

			if (element_has_class (parent_div, "-x-evo-paragraph")) {
				paragraph = WEBKIT_DOM_NODE (parent_div);
			} else {
				if (!caret)
					return;

				/* We try to select previous sibling */
				paragraph = webkit_dom_node_get_previous_sibling (
					WEBKIT_DOM_NODE (caret));
				if (paragraph) {
					/* When there is just text without container
					 * we have to surround it with paragraph div */
					if (WEBKIT_DOM_IS_TEXT (paragraph))
						paragraph = WEBKIT_DOM_NODE (
							e_html_editor_selection_put_node_into_paragraph (
								selection, document, paragraph,
								WEBKIT_DOM_NODE (caret)));
				} else {
					/* When some weird element is selected, return */
					e_html_editor_selection_clear_caret_position_marker (selection);
					return;
				}
			}
		}

		if (!paragraph)
			return;

		webkit_dom_element_remove_attribute (
			WEBKIT_DOM_ELEMENT (paragraph), "style");
		webkit_dom_element_set_id (
			WEBKIT_DOM_ELEMENT (paragraph), "-x-evo-active-paragraph");

		text_content = webkit_dom_node_get_text_content (paragraph);
		/* If there is hidden space character in the beginning we remove it */
		if (strstr (text_content, UNICODE_ZERO_WIDTH_SPACE)) {
			if (g_str_has_prefix (text_content, UNICODE_ZERO_WIDTH_SPACE)) {
				WebKitDOMNode *node;

				node = webkit_dom_node_get_first_child (paragraph);

				webkit_dom_character_data_delete_data (
					WEBKIT_DOM_CHARACTER_DATA (node),
					0,
					1,
					NULL);
			}
			if (g_str_has_suffix (text_content, UNICODE_ZERO_WIDTH_SPACE)) {
				WebKitDOMNode *node;

				node = webkit_dom_node_get_last_child (paragraph);

				webkit_dom_character_data_delete_data (
					WEBKIT_DOM_CHARACTER_DATA (node),
					g_utf8_strlen (text_content, -1) -1,
					1,
					NULL);
			}
		}
		g_free (text_content);

		wrap_lines (
			NULL, paragraph, document, FALSE,
			selection->priv->word_wrap_length);

	} else {
		e_html_editor_selection_save_caret_position (selection);
		/* If we have selection -> wrap it */
		wrap_lines (
			selection, NULL, document, FALSE,
			selection->priv->word_wrap_length);
	}

	active_paragraph = webkit_dom_document_get_element_by_id (
		document, "-x-evo-active-paragraph");
	/* We have to move caret on position where it was before modifying the text */
	e_html_editor_selection_restore_caret_position (selection);

	/* Set paragraph as non-active */
	if (active_paragraph)
		webkit_dom_element_remove_attribute (
			WEBKIT_DOM_ELEMENT (active_paragraph), "id");
}
#endif
static WebKitDOMElement *
e_html_editor_selection_wrap_paragraph_length (WebKitDOMDocument *document,
                                               WebKitDOMElement *paragraph,
                                               gint length)
{
	g_return_val_if_fail (WEBKIT_DOM_IS_ELEMENT (paragraph), NULL);
	g_return_val_if_fail (length >= MINIMAL_PARAGRAPH_WIDTH, NULL);

	return wrap_lines (WEBKIT_DOM_NODE (paragraph), document, FALSE, length);
}

static gint
get_citation_level (WebKitDOMNode *node)
{
	WebKitDOMNode *parent = webkit_dom_node_get_parent_node (node);
	gint level = 0;

	while (parent && !WEBKIT_DOM_IS_HTML_BODY_ELEMENT (parent)) {
		if (WEBKIT_DOM_IS_HTML_QUOTE_ELEMENT (parent) &&
		    webkit_dom_element_has_attribute (WEBKIT_DOM_ELEMENT (parent), "type"))
			level++;

		parent = webkit_dom_node_get_parent_node (parent);
	}

	return level;
}

static void
e_html_editor_selection_wrap_paragraphs_in_document (WebKitDOMDocument *document)
{
	WebKitDOMNodeList *list;
	gint ii, length;

	list = webkit_dom_document_query_selector_all (
		document, "div.-x-evo-paragraph:not(#-x-evo-input-start)", NULL);

	length = webkit_dom_node_list_get_length (list);

	for (ii = 0; ii < length; ii++) {
		gint quote, citation_level;
		WebKitDOMNode *node = webkit_dom_node_list_item (list, ii);

		citation_level = get_citation_level (node);
		quote = citation_level ? citation_level * 2 : 0;

		if (node_is_list (node)) {
			WebKitDOMNode *item = webkit_dom_node_get_first_child (node);

			while (item && WEBKIT_DOM_IS_HTML_LI_ELEMENT (item)) {
				e_html_editor_selection_wrap_paragraph_length (
					document, WEBKIT_DOM_ELEMENT (item), WORD_WRAP_LENGTH - quote);
				item = webkit_dom_node_get_next_sibling (item);
			}
		} else {
			e_html_editor_selection_wrap_paragraph_length (
				document, WEBKIT_DOM_ELEMENT (node), WORD_WRAP_LENGTH - quote);
		}
	}
	g_object_unref (list);
}

static WebKitDOMElement *
e_html_editor_selection_wrap_paragraph (WebKitDOMDocument *document,
                                        WebKitDOMElement *paragraph)
{
	gint indentation_level, citation_level, quote;
	gint final_width, offset = 0;

	g_return_val_if_fail (WEBKIT_DOM_IS_ELEMENT (paragraph), NULL);

	indentation_level = get_indentation_level (paragraph);
	citation_level = get_citation_level (WEBKIT_DOM_NODE (paragraph));

	if (node_is_list_or_item (WEBKIT_DOM_NODE (paragraph))) {
		gint list_level = get_list_level (WEBKIT_DOM_NODE (paragraph));
		indentation_level = 0;

		if (list_level > 0)
			offset = list_level * -SPACES_PER_LIST_LEVEL;
		else
			offset = -SPACES_PER_LIST_LEVEL;
	}

	quote = citation_level ? citation_level * 2 : 0;

	final_width = WORD_WRAP_LENGTH - quote + offset;
	final_width -= SPACES_PER_INDENTATION * indentation_level;

	return e_html_editor_selection_wrap_paragraph_length (
		document, WEBKIT_DOM_ELEMENT (paragraph), final_width);
}

void
e_html_editor_selection_dom_scroll_to_caret (WebKitDOMDocument *document)
{
	glong element_top, element_left;
	glong window_top, window_left, window_right, window_bottom;
	WebKitDOMDOMWindow *window;
	WebKitDOMElement *selection_start_marker;

	e_html_editor_selection_dom_save (document);

	selection_start_marker = webkit_dom_document_get_element_by_id (
		document, "-x-evo-selection-start-marker");
	if (!selection_start_marker)
		return;

	window = webkit_dom_document_get_default_view (document);

	window_top = webkit_dom_dom_window_get_scroll_y (window);
	window_left = webkit_dom_dom_window_get_scroll_x (window);
	window_bottom = window_top + webkit_dom_dom_window_get_inner_height (window);
	window_right = window_left + webkit_dom_dom_window_get_inner_width (window);

	element_left = webkit_dom_element_get_offset_left (selection_start_marker);
	element_top = webkit_dom_element_get_offset_top (selection_start_marker);

	/* Check if caret is inside viewport, if not move to it */
	if (!(element_top >= window_top && element_top <= window_bottom &&
	     element_left >= window_left && element_left <= window_right)) {
		webkit_dom_element_scroll_into_view (selection_start_marker, TRUE);
	}

	e_html_editor_selection_dom_restore (selection);
}

/**
 * e_html_editor_selection_insert_html:
 * @selection: an #EHTMLEditorSelection
 * @html_text: an HTML code to insert
 *
 * Insert @html_text into document at current cursor position. When a text range
 * is selected, it will be replaced by @html_text.
 */
void
e_html_editor_selection_insert_html (WebKitDOMDocument *document,
                                     const gchar *html_text)
{
	g_return_if_fail (html_text != NULL);

	command = E_HTML_EDITOR_VIEW_COMMAND_INSERT_HTML;
	if (is_in_html_mode (document)) {
		e_html_editor_view_dom_exec_command (document, command, html_text);
		e_html_editor_view_check_magic_links (view, FALSE);
		e_html_editor_view_force_spell_check (view);

		e_html_editor_selection_dom_scroll_to_caret (selection);
	} else
		e_html_editor_view_convert_and_insert_html_to_plain_text (
			view, html_text);
}

