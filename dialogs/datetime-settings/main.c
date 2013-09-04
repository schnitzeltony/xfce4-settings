/*
 *  Copyright (c) 2013 Andreas Müller <schnitzeltony@googlemail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <glib.h>
#include <gtk/gtk.h>

#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4util/libxfce4util.h>

#include "datetime-dialog.h"
#include "datetime-dialog_ui.h"

/* Option entries */
static GdkNativeWindow opt_socket_id = 0;
static gboolean opt_version = FALSE;
static GOptionEntry option_entries[] =
{
    { "socket-id", 's', G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT, &opt_socket_id, N_("Settings manager socket"), N_("SOCKET ID") },
    { "version", 'v', 0, G_OPTION_ARG_NONE, &opt_version, N_("Version information"), NULL },
    { NULL }
};


static void
datetime_settings_dialog_response (GtkWidget *dialog,
                                   gint       response_id)
{
    if (response_id == GTK_RESPONSE_HELP)
        xfce_dialog_show_help (GTK_WINDOW (dialog), "xfce4-settings", "datetime", NULL);
    else
        gtk_main_quit ();
}

gint
main (gint argc, gchar **argv)
{
    GObject    *dialog, *plug_child, *dlgobj;
    GtkWidget  *plug;
    GtkBuilder *builder;
    GError     *error = NULL;

    /* setup translation domains */
    bindtextdomain (GETTEXT_PACKAGE_TIMEZONES, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE_TIMEZONES, "UTF-8");
    xfce_textdomain (GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    /* initialize Gtk+ */
    if (!gtk_init_with_args (&argc, &argv, "", option_entries, GETTEXT_PACKAGE, &error))
    {
        if (G_LIKELY (error))
        {
            /* print error */
            g_print ("%s: %s.\n", G_LOG_DOMAIN, error->message);
            g_print (_("Type '%s --help' for usage."), G_LOG_DOMAIN);
            g_print ("\n");

            /* cleanup */
            g_error_free (error);
        }
        else
        {
            g_error ("Unable to open display.");
        }

        return EXIT_FAILURE;
    }

    /* print version information */
    if (G_UNLIKELY (opt_version))
    {
        g_print ("%s %s (Xfce %s)\n\n", G_LOG_DOMAIN, PACKAGE_VERSION, xfce_version_string ());
        g_print ("%s\n", "Copyright (c) 2013");
        g_print ("\t%s\n\n", _("The Xfce development team. All rights reserved."));
        g_print (_("Please report bugs to <%s>."), PACKAGE_BUGREPORT);
        g_print ("\n");

        return EXIT_SUCCESS;
    }

    /* hook to make sure the libxfce4ui library is linked */
    if (xfce_titled_dialog_get_type () == 0)
        return EXIT_FAILURE;

    /* load the gtk user interface file*/
    builder = gtk_builder_new ();
    if (gtk_builder_add_from_string (builder, datetime_dialog_ui,
                                     datetime_dialog_ui_length, &error) != 0)
    {
        dlgobj = g_object_new (XFCE_TYPE_DATE_TIME_DIALOG, NULL);
        xfce_date_time_dialog_setup (dlgobj, builder);

        if (G_UNLIKELY (opt_socket_id == 0))
        {
            /* build the dialog */
            dialog = gtk_builder_get_object (builder, "dialog");
            g_signal_connect (dialog, "response",
                              G_CALLBACK (datetime_settings_dialog_response), NULL);
            gtk_window_present (GTK_WINDOW (dialog));

            /* To prevent the settings dialog to be saved in the session */
            gdk_set_sm_client_id ("FAKE ID");

            gtk_main ();
        }
        else
        {
            /* Create plug widget */
            plug = gtk_plug_new (opt_socket_id);
            g_signal_connect (plug, "delete-event", G_CALLBACK (gtk_main_quit), NULL);
            gtk_widget_show (plug);

            /* Stop startup notification */
            gdk_notify_startup_complete ();

            /* Get plug child widget */
            plug_child = gtk_builder_get_object (builder, "plug-child");
            gtk_widget_reparent (GTK_WIDGET (plug_child), plug);
            gtk_widget_show (GTK_WIDGET (plug_child));

            /* To prevent the settings dialog to be saved in the session */
            gdk_set_sm_client_id ("FAKE ID");

            /* Enter main loop */
            gtk_main ();
        }
        g_object_unref(dlgobj);
    }
    else
    {
        g_error ("Failed to load the UI file: %s.", error->message);
        g_error_free (error);
    }

    /* Release Builder */
    g_object_unref (G_OBJECT (builder));

    return EXIT_SUCCESS;
}
