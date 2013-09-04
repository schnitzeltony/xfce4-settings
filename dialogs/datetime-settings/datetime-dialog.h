/*
 * Copyright (C) 2013 Andreas Müller <schnitzeltony@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *
 */

#ifndef _DATE_TIME_DIALOG_H
#define _DATE_TIME_DIALOG_H

#include <gtk/gtk.h>

#define GETTEXT_PACKAGE_TIMEZONES GETTEXT_PACKAGE "-timezones"

/* globals */
void       xfce_date_time_dialog_setup (GObject *dlgobj, GtkBuilder *builder);

G_BEGIN_DECLS

typedef struct _XfceDateTimeDialogClass   XfceDateTimeDialogClass;
typedef struct _XfceDateTimeDialog        XfceDateTimeDialog;
typedef struct _XfceDateTimeDialogPrivate XfceDateTimeDialogPrivate;

#define XFCE_TYPE_DATE_TIME_DIALOG            (xfce_date_time_dialog_get_type ())
#define XFCE_DATE_TIME_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), XFCE_TYPE_DATE_TIME_DIALOG, XfceDateTimeDialog))
#define XFCE_DATE_TIME_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), XFCE_TYPE_DATE_TIME_DIALOG, XfceDateTimeDialogClass))
#define XFCE_IS_DATE_TIME_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), XFCE_TYPE_DATE_TIME_DIALOG))
#define XFCE_IS_DATE_TIME_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), XFCE_TYPE_DATE_TIME_DIALOG))
#define XFCE_DATE_TIME_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), XFCE_TYPE_DATE_TIME_DIALOG, XfceDateTimeDialogClass))

GType      xfce_date_time_dialog_get_type     (void) G_GNUC_CONST;

G_END_DECLS


#endif /* _DATE_TIME_DIALOG_H */
