/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-pilot-settings.h
 *
 * Copyright (C) 2001  JP Rosevear
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: JP Rosevear
 */

#ifndef _E_PILOT_SETTINGS_H_
#define _E_PILOT_SETTINGS_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gnome.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define E_TYPE_PILOT_SETTINGS			(e_pilot_settings_get_type ())
#define E_PILOT_SETTINGS(obj)			(GTK_CHECK_CAST ((obj), E_TYPE_PILOT_SETTINGS, EPilotSettings))
#define E_PILOT_SETTINGS_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), E_TYPE_PILOT_SETTINGS, EPilotSettingsClass))
#define E_IS_PILOT_SETTINGS(obj)			(GTK_CHECK_TYPE ((obj), E_TYPE_PILOT_SETTINGS))
#define E_IS_PILOT_SETTINGS_CLASS(klass)		(GTK_CHECK_CLASS_TYPE ((obj), E_TYPE_PILOT_SETTINGS))


typedef struct _EPilotSettings        EPilotSettings;
typedef struct _EPilotSettingsPrivate EPilotSettingsPrivate;
typedef struct _EPilotSettingsClass   EPilotSettingsClass;

#define E_PILOT_SETTINGS_TABLE_ROWS 2
#define E_PILOT_SETTINGS_TABLE_COLS 2

struct _EPilotSettings {
	GtkTable parent;

	EPilotSettingsPrivate *priv;
};

struct _EPilotSettingsClass {
	GtkTableClass parent_class;
};


GtkType    e_pilot_settings_get_type (void);
GtkWidget *e_pilot_settings_new      (void);

gboolean e_pilot_settings_get_secret (EPilotSettings *ps);
void e_pilot_settings_set_secret (EPilotSettings *ps, gboolean secret);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _E_PILOT_SETTINGS_H_ */
