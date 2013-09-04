/*
 * Copyright (C) 2010 Intel, Inc
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
 * Author: Thomas Wood <thomas.wood@intel.com>
 *
 * xfce-port:
 *
 * Copyright (C) 2013 Andreas Müller <schnitzeltony@googlemail.com>
 *
 */

#include "config.h"
#include "datetime-dialog.h"

#include <langinfo.h>
#include <sys/time.h>
#include "timezone-map.h"
#include "timedated.h"

#include <string.h>
#include <stdlib.h>
#include <libintl.h>

/* FIXME: This should be "Etc/GMT" instead */
#define DEFAULT_TZ "Europe/London"

struct _XfceDateTimeDialogClass
{
    GObjectClass parent_class;
};

struct _XfceDateTimeDialog
{
    GObject parent;

    XfceDateTimeDialogPrivate *priv;
};

struct _XfceDateTimeDialogPrivate
{
    GtkBuilder         *builder;
    GtkWidget          *map;

    GtkTreeModel       *locations;
    GtkTreeModelFilter *city_filter;

    GDateTime          *date;

    Timedate1          *dtm;
    GCancellable       *cancellable;

    gboolean            ntp_changed;
    gboolean            timezone_changed;
    gboolean            datetime_changed;

    int                 current_apply_action;
    guint               timeout_id;
    guint32             last_displayed_second;
    guint32             last_displayed_day;
};

G_DEFINE_TYPE (XfceDateTimeDialog, xfce_date_time_dialog, G_TYPE_OBJECT)

#define XFCE_DATE_TIME_DIALOG_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((o), XFCE_TYPE_DATE_TIME_DIALOG, XfceDateTimeDialogPrivate))

enum
{
    CITY_COL_CITY,
    CITY_COL_REGION,
    CITY_COL_CITY_TRANSLATED,
    CITY_COL_REGION_TRANSLATED,
    CITY_COL_ZONE,
    CITY_NUM_COLS
};

enum
{
    REGION_COL_REGION,
    REGION_COL_REGION_TRANSLATED,
    REGION_NUM_COLS
};

#define W(x) (GtkWidget*) gtk_builder_get_object (priv->builder, x)

enum
{
    APPLY_IDLE = 0,
    APPLY_TIMEZONE,
    APPLY_NTP,
    APPLY_DATETIME
};

#define APPLY_FIRST APPLY_TIMEZONE

void start_next_apply_action(XfceDateTimeDialog *xfdtdlg);

static void
xfce_date_time_dialog_dispose (GObject *object)
{
    XfceDateTimeDialogPrivate *priv = XFCE_DATE_TIME_DIALOG (object)->priv;

    if (G_LIKELY(priv->builder))
    {
        /* our creator takes care */
        priv->builder = NULL;
    }

    if (G_LIKELY(priv->date))
    {
        g_date_time_unref (priv->date);
        priv->date = NULL;
    }

    if (G_LIKELY(priv->cancellable))
    {
        g_cancellable_cancel (priv->cancellable);
        g_object_unref (priv->cancellable);
        priv->cancellable = NULL;
    }

    if (G_LIKELY(priv->dtm))
    {
        g_object_unref (priv->dtm);
        priv->dtm = NULL;
    }

    if (G_LIKELY(priv->timeout_id))
    {
        g_source_remove (priv->timeout_id);
        priv->timeout_id = 0;
    }

    G_OBJECT_CLASS (xfce_date_time_dialog_parent_class)->dispose (object);
}

static void
xfce_date_time_dialog_class_init (XfceDateTimeDialogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (XfceDateTimeDialogPrivate));

    object_class->dispose = xfce_date_time_dialog_dispose;
}

static void
xfce_date_time_dialog_init (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv;
    GError *error;

    priv = xfdtdlg->priv = XFCE_DATE_TIME_DIALOG_PRIVATE (xfdtdlg);

    priv->cancellable = g_cancellable_new ();

    /* ensure initial datetime redraw */
    priv->last_displayed_second = G_MAXUINT32;
    priv->last_displayed_day = G_MAXUINT32;

    /* setup dbus proxy */
    error = NULL;
    priv->dtm = timedate1_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                                  G_DBUS_PROXY_FLAGS_NONE,
                                                  "org.freedesktop.timedate1",
                                                  "/org/freedesktop/timedate1",
                                                  priv->cancellable,
                                                  &error);
    if (priv->dtm == NULL)
    {
        g_warning ("could not get proxy for DateTimeMechanism: %s", error->message);
        g_error_free (error);
    }
}

static void
update_apply_state (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    gboolean anychange = priv->ntp_changed ||
                         priv->timezone_changed ||
                         priv->datetime_changed;
    GtkWidget *widget = W ("button-apply");
    gtk_widget_set_sensitive (widget, anychange && priv->current_apply_action == APPLY_IDLE);
}

static void
save_user_change_date (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    guint mon, y, d;
    GDateTime *old_date;

    old_date = priv->date;

    mon = 1 + gtk_combo_box_get_active (GTK_COMBO_BOX (W ("month-combobox")));
    y = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (W ("year-spinbutton")));
    d = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (W ("day-spinbutton")));

    priv->date = g_date_time_new_local (y, mon, d,
                                        g_date_time_get_hour (old_date),
                                        g_date_time_get_minute (old_date),
                                        g_date_time_get_second (old_date));
    g_date_time_unref (old_date);
    priv->datetime_changed = TRUE;
    update_apply_state(xfdtdlg);
}

static void
on_user_day_changed (GtkWidget          *widget,
                     XfceDateTimeDialog *xfdtdlg)
{
    save_user_change_date (xfdtdlg);
}

static void
on_user_month_year_changed (GtkWidget          *widget,
                            XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    guint mon, y;
    guint num_days;
    GtkAdjustment *adj;
    GtkSpinButton *day_spin;

    mon = 1 + gtk_combo_box_get_active (GTK_COMBO_BOX (W ("month-combobox")));
    y = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (W ("year-spinbutton")));

    /* Check the number of days in that month */
    num_days = g_date_get_days_in_month (mon, y);

    day_spin = GTK_SPIN_BUTTON (W("day-spinbutton"));
    adj = GTK_ADJUSTMENT (gtk_spin_button_get_adjustment (day_spin));
    gtk_adjustment_set_upper (adj, num_days);

    if (gtk_spin_button_get_value_as_int (day_spin) > num_days)
        gtk_spin_button_set_value (day_spin, num_days);

    save_user_change_date (xfdtdlg);
}

static void
update_displayed_time (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    char *label;
    guint i;
    guint32 locsecond;

    locsecond = (guint32)g_date_time_get_second (priv->date) +
                ((guint32)g_date_time_get_minute (priv->date)) * 60 +
                ((guint32)g_date_time_get_hour (priv->date)) * 3600;
    if (locsecond != priv->last_displayed_second)
    {
        priv->last_displayed_second = locsecond;

        /* Update the hours label */
        label = g_date_time_format (priv->date, "%H");
        gtk_label_set_text (GTK_LABEL (W("hours_label")), label);
        g_free (label);

        /* Update the minutes label */
        label = g_date_time_format (priv->date, "%M");
        gtk_label_set_text (GTK_LABEL (W("minutes_label")), label);

        /* Update the seconds label */
        label = g_date_time_format (priv->date, "%S");
        gtk_label_set_text (GTK_LABEL (W("seconds_label")), label);
        g_free (label);
    }
}

static void
update_displayed_date (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    GtkAdjustment *adjustment;
    guint num_days;
    guint32 locday;
    GtkWidget *widget;

    locday = (guint32)g_date_time_get_day_of_year (priv->date) +
             ((guint32)g_date_time_get_year (priv->date)) * 366;

    if (locday != priv->last_displayed_day)
    {
        priv->last_displayed_day = locday;

        /* day */
        widget = W ("day-spinbutton");
        g_signal_handlers_block_by_func (widget, on_user_day_changed, xfdtdlg);
        num_days = g_date_get_days_in_month (g_date_time_get_month (priv->date),
                                             g_date_time_get_year (priv->date));
        adjustment = gtk_spin_button_get_adjustment (GTK_SPIN_BUTTON (widget));
        gtk_adjustment_set_upper (adjustment, num_days);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget),
                                   (gdouble)g_date_time_get_day_of_month (priv->date));
        g_signal_handlers_unblock_by_func (widget, on_user_day_changed, xfdtdlg);

        /* month */
        widget = W ("month-combobox");
        g_signal_handlers_block_by_func (widget, on_user_month_year_changed, xfdtdlg);
        gtk_combo_box_set_active (GTK_COMBO_BOX (widget),
                                  g_date_time_get_month (priv->date) - 1);
        g_signal_handlers_unblock_by_func (widget, on_user_month_year_changed, xfdtdlg);

        /* year */
        widget = W ("year-spinbutton");
        g_signal_handlers_block_by_func (widget, on_user_month_year_changed, xfdtdlg);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget),
                                   (gdouble)g_date_time_get_year (priv->date));
        g_signal_handlers_unblock_by_func (widget, on_user_month_year_changed, xfdtdlg);
    }
}

static void
on_user_region_changed (GtkComboBox     *box,
                        XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    GtkTreeModelFilter *modelfilter;

    modelfilter = GTK_TREE_MODEL_FILTER (W("city-modelfilter"));

    gtk_tree_model_filter_refilter (modelfilter);
    /* not a change which can be applied without further interaction */
}

static void
on_user_city_changed (GtkComboBox        *box,
                      XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    GtkTreeIter iter;
    gchar *zone;
    TzLocation *location;

    if (gtk_combo_box_get_active_iter (box, &iter))
    {
        gtk_tree_model_get (gtk_combo_box_get_model (box), &iter,
                            CITY_COL_ZONE, &zone, -1);

        xfdts_timezone_map_set_timezone (XFDTS_TIMEZONE_MAP (priv->map), zone);
        g_free (zone);

        priv->timezone_changed = TRUE;
        update_apply_state(xfdtdlg);
    }
}

static void
update_displayed_timezone (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    GtkWidget *widget;
    gchar **split;
    GtkTreeIter iter;
    GtkTreeModel *model;
    TzLocation *current_location;

    /* tz.c updates the local timezone, which means the spin buttons can be
     * updated with the current time of the new location */

    current_location = xfdts_timezone_map_get_location (XFDTS_TIMEZONE_MAP (priv->map));
    split = g_strsplit (current_location->zone, "/", 2);

    /* remove underscores */
    g_strdelimit (split[1], "_", ' ');

    /* update region combo */
    widget = GTK_WIDGET (W ("region_combobox"));
    model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
    gtk_tree_model_get_iter_first (model, &iter);

    do
    {
        gchar *string;

        gtk_tree_model_get (model, &iter, CITY_COL_CITY, &string, -1);

        if (!g_strcmp0 (string, split[0]))
        {
            g_free (string);
            gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);
            break;
        }
        g_free (string);
    }
    while (gtk_tree_model_iter_next (model, &iter));

    /* update city combo */
    widget = GTK_WIDGET (W ("city_combobox"));
    model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (W ("city-modelfilter")));
    gtk_tree_model_get_iter_first (model, &iter);

    do
    {
        gchar *string;

        gtk_tree_model_get (model, &iter, CITY_COL_CITY, &string, -1);

        if (!g_strcmp0 (string, split[1]))
        {
            g_free (string);
            gtk_combo_box_set_active_iter (GTK_COMBO_BOX (widget), &iter);
            break;
        }
        g_free (string);
    }
    while (gtk_tree_model_iter_next (model, &iter));

    g_strfreev (split);
}

static void
on_user_map_location_changed (XfdtsTimezoneMap   *map,
                              TzLocation         *location,
                              XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    GtkWidget *region_combo, *city_combo;

    g_debug ("location changed to %s/%s", location->country, location->zone);

    /* Update the combo boxes */
    region_combo = W("region_combobox");
    city_combo = W("city_combobox");

    g_signal_handlers_block_by_func (region_combo, on_user_region_changed, xfdtdlg);
    g_signal_handlers_block_by_func (city_combo, on_user_city_changed, xfdtdlg);

    update_displayed_timezone (xfdtdlg);

    g_signal_handlers_unblock_by_func (region_combo, on_user_region_changed, xfdtdlg);
    g_signal_handlers_unblock_by_func (city_combo, on_user_city_changed, xfdtdlg);

    priv->timezone_changed = TRUE;
    update_apply_state(xfdtdlg);
}

static void
get_initial_timezone (XfceDateTimeDialog *xfdtdlg)
{
   XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
   const gchar *timezone;

    if (priv->dtm)
        timezone = timedate1_get_timezone (priv->dtm);
    else
        timezone = NULL;

    if (timezone == NULL ||
        !xfdts_timezone_map_set_timezone (XFDTS_TIMEZONE_MAP (priv->map), timezone))
    {
        g_warning ("Timezone '%s' is unhandled, setting %s as default", timezone ? timezone : "(null)", DEFAULT_TZ);
        xfdts_timezone_map_set_timezone (XFDTS_TIMEZONE_MAP (priv->map), DEFAULT_TZ);
    }
    update_displayed_timezone (xfdtdlg);
}

/* load region and city tree models */
struct get_region_data
{
    GtkListStore *region_store;
    GtkListStore *city_store;
    GHashTable *table;
};

/* Slash look-alikes that might be used in translations */
#define TRANSLATION_SPLIT                                                        \
        "\342\201\204"        /* FRACTION SLASH */                               \
        "\342\210\225"        /* DIVISION SLASH */                               \
        "\342\247\270"        /* BIG SOLIDUS */                                  \
        "\357\274\217"        /* FULLWIDTH SOLIDUS */                            \
        "/"

static void
get_regions (TzLocation             *loc,
             struct get_region_data *data)
{
    gchar *zone;
    gchar **split;
    gchar **split_translated;
    gchar *translated_city;

    zone = g_strdup (loc->zone);
    g_strdelimit (zone, "_", ' ');
    split = g_strsplit (zone, "/", 2);
    g_free (zone);

    /* Load the translation for it */
    zone = g_strdup (dgettext (GETTEXT_PACKAGE_TIMEZONES, loc->zone));
    g_strdelimit (zone, "_", ' ');
    split_translated = g_regex_split_simple ("[\\x{2044}\\x{2215}\\x{29f8}\\x{ff0f}/]", zone, 0, 0);
    g_free (zone);

    if (!g_hash_table_lookup_extended (data->table, split[0], NULL, NULL))
    {
        g_hash_table_insert (data->table, g_strdup (split[0]),
                             GINT_TO_POINTER (1));
        gtk_list_store_insert_with_values (data->region_store, NULL, 0,
                                           REGION_COL_REGION, split[0],
                                           REGION_COL_REGION_TRANSLATED, split_translated[0], -1);
    }

    /* g_regex_split_simple() splits too much for us, and would break
     * America/Argentina/Buenos_Aires into 3 strings, so rejoin the city part */
    translated_city = g_strjoinv ("/", split_translated + 1);

    gtk_list_store_insert_with_values (data->city_store, NULL, 0,
                                       CITY_COL_CITY, split[1],
                                       CITY_COL_CITY_TRANSLATED, translated_city,
                                       CITY_COL_REGION, split[0],
                                       CITY_COL_REGION_TRANSLATED, split_translated[0],
                                       CITY_COL_ZONE, loc->zone,
                                       -1);

    g_free (translated_city);
    g_strfreev (split);
    g_strfreev (split_translated);
}

static gboolean
city_model_filter_func (GtkTreeModel *model,
                        GtkTreeIter  *iter,
                        GtkComboBox  *combo)
{
    GtkTreeModel *combo_model;
    GtkTreeIter combo_iter;
    gchar *active_region = NULL;
    gchar *city_region = NULL;
    gboolean result;

    if (gtk_combo_box_get_active_iter (combo, &combo_iter) == FALSE)
        return FALSE;

    combo_model = gtk_combo_box_get_model (combo);
    gtk_tree_model_get (combo_model, &combo_iter,
                        CITY_COL_CITY, &active_region, -1);

    gtk_tree_model_get (model, iter,
                        CITY_COL_REGION, &city_region, -1);

    if (g_strcmp0 (active_region, city_region) == 0)
        result = TRUE;
    else
        result = FALSE;

    g_free (city_region);
    g_free (active_region);

    return result;
}


static void
load_regions_model (GtkListStore *regions, GtkListStore *cities)
{
    struct get_region_data data;
    TzDB *db;
    GHashTable *table;

    db = tz_load_db ();
    table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    data.table = table;
    data.region_store = regions;
    data.city_store = cities;

    g_ptr_array_foreach (db->locations, (GFunc) get_regions, &data);

    g_hash_table_destroy (table);

    tz_db_free (db);

    /* sort the models */
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (regions),
                                          REGION_COL_REGION_TRANSLATED,
                                          GTK_SORT_ASCENDING);
}

static void
update_datetime_widget_sensivity (XfceDateTimeDialog *xfdtdlg,
                                      gboolean            sensitive)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;

    gtk_widget_set_sensitive (W("table1"), sensitive);
    gtk_widget_set_sensitive (W("table2"), sensitive);
}

static void
on_user_time_changed (GtkButton          *button,
                      XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    const gchar *widget_name;
    gint direction;
    GDateTime *old_date;

    old_date = priv->date;

    widget_name = gtk_buildable_get_name (GTK_BUILDABLE (button));

    if (strstr (widget_name, "up"))
        direction = 1;
    else
        direction = -1;

    if (widget_name[0] == 'h')
    {
        priv->date = g_date_time_add_hours (old_date, direction);
    }
    else if (widget_name[0] == 'm')
    {
        priv->date = g_date_time_add_minutes (old_date, direction);
    }
    else
    {
        priv->date = g_date_time_add_seconds (old_date, (gdouble)direction);
    }
    g_date_time_unref (old_date);

    update_displayed_time (xfdtdlg);
    update_displayed_date (xfdtdlg);
    priv->datetime_changed = TRUE;
    update_apply_state(xfdtdlg);
}

static void
on_user_ntp_changed (GObject            *gobject,
                     XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;

    priv->ntp_changed = TRUE;
    update_apply_state(xfdtdlg);

    GtkToggleButton* button = GTK_TOGGLE_BUTTON (W ("network_time_switch"));
    update_datetime_widget_sensivity (xfdtdlg,
                                      !gtk_toggle_button_get_active (button));
}

static void
update_displayed_ntp (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    gboolean using_ntp, can_ftp;
    GtkWidget *switch_widget;

    can_ftp = FALSE;
    using_ntp = FALSE;
    if (G_LIKELY(priv->dtm != NULL))
    {
        can_ftp = timedate1_get_can_ntp (priv->dtm);
        if (can_ftp)
        {
            using_ntp = timedate1_get_ntp (priv->dtm);
        }
    }
    switch_widget = W("network_time_switch");

    /* enable ntp only if properly installed */
    gtk_widget_set_sensitive (switch_widget, can_ftp);

    /* avoid our own feedback on changing button state */
    g_signal_handlers_block_by_func (switch_widget, on_user_ntp_changed, xfdtdlg);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (switch_widget), using_ntp);
    g_signal_handlers_unblock_by_func (switch_widget, on_user_ntp_changed, xfdtdlg);

    update_datetime_widget_sensivity (xfdtdlg, !using_ntp);
}

static void
on_system_ntp_changed (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;

    priv->ntp_changed = FALSE;
    /* in case ntp is activated, user's datetime changes are ignored */
    if (timedate1_get_ntp (priv->dtm))
        priv->datetime_changed = FALSE;

    update_displayed_ntp (xfdtdlg);
    update_apply_state (xfdtdlg);
}

static void
update_timezone_changed_final (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;

    priv->timezone_changed = FALSE;
    update_apply_state(xfdtdlg);

    if (!priv->datetime_changed)
    {
        if (priv->date)
            g_date_time_unref (priv->date);
        priv->date = g_date_time_new_now_local ();
        update_displayed_time (xfdtdlg);
    }
}

static void
on_system_timezone_changed (XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    GtkWidget *region_combo, *city_combo;

    region_combo = W("region_combobox");
    city_combo = W("city_combobox");

    g_signal_handlers_block_by_func (region_combo, on_user_region_changed, xfdtdlg);
    g_signal_handlers_block_by_func (city_combo, on_user_city_changed, xfdtdlg);
    g_signal_handlers_block_by_func (priv->map, on_user_map_location_changed, xfdtdlg);

    get_initial_timezone (xfdtdlg);

    g_signal_handlers_unblock_by_func (region_combo, on_user_region_changed, xfdtdlg);
    g_signal_handlers_unblock_by_func (city_combo, on_user_city_changed, xfdtdlg);
    g_signal_handlers_unblock_by_func (priv->map, on_user_map_location_changed, xfdtdlg);

    update_timezone_changed_final (xfdtdlg);
}

static void
on_system_timedate_props_changed (GDBusProxy          *proxy,
                                  GVariant            *changed_properties,
                                  const gchar        **invalidated_properties,
                                  XfceDateTimeDialog  *xfdtdlg)
{
    GError *error;
    GVariant *variant;
    GVariant *v;
    guint i;

    if (invalidated_properties != NULL)
    {
        for (i = 0; invalidated_properties[i] != NULL; i++)
        {
            error = NULL;
            /* See https://bugs.freedesktop.org/show_bug.cgi?id=37632 for the reason why we're doing this */
            variant = g_dbus_proxy_call_sync (proxy,
                                      "org.freedesktop.DBus.Properties.Get",
                                      g_variant_new ("(ss)", "org.freedesktop.timedate1", invalidated_properties[i]),
                                      G_DBUS_CALL_FLAGS_NONE,
                                      -1,
                                      NULL,
                                      &error);
            if (variant == NULL)
            {
                g_warning ("Failed to get property '%s': %s", invalidated_properties[i], error->message);
                g_error_free (error);
            }
            else
            {
                g_variant_get (variant, "(v)", &v);
                g_dbus_proxy_set_cached_property (proxy, invalidated_properties[i], v);
                g_variant_unref (variant);
            }
        }
    }
}

static void
callback_set_time (GObject      *source,
                   GAsyncResult *res,
                   gpointer      user_data)
{
    XfceDateTimeDialog *xfdtdlg = user_data;
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    GError *error;

    error = NULL;
    if (timedate1_call_set_time_finish (priv->dtm,
                                         res,
                                         &error))
        priv->datetime_changed = FALSE;
    else
    {
        g_warning ("Could not set system time: %s", error->message);
        g_error_free (error);
    }
    /* we are last */
    priv->current_apply_action = APPLY_IDLE;
    start_next_apply_action(xfdtdlg);
}

static void
callback_set_timezone (GObject      *source,
                       GAsyncResult *res,
                       gpointer      user_data)
{
    XfceDateTimeDialog *xfdtdlg = user_data;
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    GError *error;

    error = NULL;
    if (timedate1_call_set_timezone_finish (priv->dtm,
                                            res,
                                            &error))
    {
        update_timezone_changed_final (xfdtdlg);
    }
    else
    {
        g_warning ("Could not set system timezone: %s", error->message);
        g_error_free (error);
    }

    /* continue next action */
    priv->current_apply_action = APPLY_NTP;
    start_next_apply_action(xfdtdlg);
}

static void
callback_set_using_ntp (GObject      *source,
                        GAsyncResult *res,
                        gpointer      user_data)
{
    XfceDateTimeDialog *xfdtdlg = user_data;
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    GError *error;

    error = NULL;
    if (timedate1_call_set_ntp_finish (priv->dtm,
                                        res,
                                        &error))
    {
        priv->ntp_changed = FALSE;
        /* in case of ntp reenable, user's datetime changes must be ignored
         * note that we cannot use timedate1_get_ntp because it reports old value
         */
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W ("network_time_switch"))))
            priv->datetime_changed = FALSE;
    }
    else
    {
        g_warning ("Could not set system to use NTP: %s", error->message);
        g_error_free (error);
    }
    priv->current_apply_action = APPLY_DATETIME;
    start_next_apply_action(xfdtdlg);
}

static gboolean
callback_timer (gpointer user_data)
{
    XfceDateTimeDialog *xfdtdlg = user_data;
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;

    if (!priv->datetime_changed)
    {
        if (priv->date)
            g_date_time_unref (priv->date);
        priv->date = g_date_time_new_now_local ();
        update_displayed_time (xfdtdlg);
        update_displayed_date (xfdtdlg);
    }
    return TRUE;
}

void
start_next_apply_action(XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;
    TzLocation *current_location;
    GTimeZone *tz;
    GDateTime *loctime, *utctime;
    gboolean using_ntp;
    gint64 unixtime_sec;

    current_location = xfdts_timezone_map_get_location (XFDTS_TIMEZONE_MAP (priv->map));
    switch(priv->current_apply_action)
    {
    case APPLY_TIMEZONE:
        if (priv->timezone_changed)
        {
            timedate1_call_set_timezone (priv->dtm,
                                         current_location->zone,
                                         TRUE,
                                         priv->cancellable,
                                         callback_set_timezone,
                                         xfdtdlg);
        }
        else
        {
            priv->current_apply_action = APPLY_NTP;
            return start_next_apply_action(xfdtdlg);
        }
        break;
    case APPLY_NTP:
        if (priv->ntp_changed)
        {
            using_ntp = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W ("network_time_switch")));
            timedate1_call_set_ntp (priv->dtm,
                                    using_ntp,
                                    TRUE,
                                    priv->cancellable,
                                    callback_set_using_ntp,
                                    xfdtdlg);
        }
        else
        {
            priv->current_apply_action = APPLY_DATETIME;
            return start_next_apply_action(xfdtdlg);
        }
        break;
    case APPLY_DATETIME:
        if (priv->datetime_changed)
        {
            tz = g_time_zone_new (current_location->zone);
            if (tz != NULL)
            {
                loctime = g_date_time_new (tz,
                                           g_date_time_get_year (priv->date),
                                           g_date_time_get_month (priv->date),
                                           g_date_time_get_day_of_month (priv->date),
                                           g_date_time_get_hour (priv->date),
                                           g_date_time_get_minute (priv->date),
                                           g_date_time_get_second (priv->date));
                if (loctime != NULL)
                {
                    utctime = g_date_time_to_utc (loctime);
                    if (utctime != NULL)
                    {
                        /* timedated expects number of microseconds since 1 Jan 1970 UTC */
                        unixtime_sec = g_date_time_to_unix (loctime);
                        timedate1_call_set_time (priv->dtm,
                                                 unixtime_sec * 1000000,
                                                 FALSE,
                                                 TRUE,
                                                 priv->cancellable,
                                                 callback_set_time,
                                                 xfdtdlg);
                        g_date_time_unref (utctime);
                    }
                    else
                        g_warning("Couldn't create utc time");
                    g_date_time_unref (loctime);
                }
                else
                    g_warning("Couldn't create local time");
                g_time_zone_unref (tz);
            }
            else
                g_warning("Couldn't create timezone: %s", current_location->zone);
        }
        else
        {
            priv->current_apply_action = APPLY_IDLE;
        }
        break;
    }

    /* update GUI */
    update_apply_state(xfdtdlg);
    update_datetime_widget_sensivity (xfdtdlg,
                                      priv->current_apply_action == APPLY_IDLE &&
                                        !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (W ("network_time_switch"))));
}

static void
on_user_apply (GtkButton          *button,
               XfceDateTimeDialog *xfdtdlg)
{
    XfceDateTimeDialogPrivate *priv = xfdtdlg->priv;

    priv->current_apply_action = APPLY_FIRST;
    start_next_apply_action(xfdtdlg);
}

void
xfce_date_time_dialog_setup (GObject *dlgobj, GtkBuilder *builder)
{
    XfceDateTimeDialog *xfdtdlg;
    XfceDateTimeDialogPrivate *priv;
    gchar *objects[] = { "datetime-panel", "region-liststore",
                         "city-liststore", "month-liststore",
                         "city-modelfilter", "city-modelsort", NULL };
    char *buttons[] = { "hour_up_button", "hour_down_button",
                        "min_up_button",  "min_down_button",
                        "second_up_button",  "second_down_button" };
    GtkWidget *widget;
    GError *err = NULL;
    GtkTreeModelFilter *city_modelfilter;
    GtkTreeModelSort *city_modelsort;
    GtkAdjustment *adjustment;
    guint i, num_days;
    int ret;

    xfdtdlg = XFCE_DATE_TIME_DIALOG(dlgobj);
    priv = XFCE_DATE_TIME_DIALOG_PRIVATE (xfdtdlg);
    priv->builder = builder;

    /* set up apply-button */
    g_signal_connect (W ("button-apply"), "clicked",
                      G_CALLBACK (on_user_apply), xfdtdlg);
    update_apply_state (xfdtdlg);

    /* set up network time button */
    g_signal_connect (W ("network_time_switch"), "toggled",
                      G_CALLBACK (on_user_ntp_changed), xfdtdlg);
    update_displayed_ntp (xfdtdlg);

    /* set up time editing widgets */
    for (i = 0; i < G_N_ELEMENTS (buttons); i++)
    {
        g_signal_connect (W (buttons[i]), "clicked",
                          G_CALLBACK (on_user_time_changed), xfdtdlg);
    }

    /* set up date editing widgets */
    priv->date = g_date_time_new_now_local ();

    /* Force the direction for the time, so that the time
    * is presented correctly for RTL languages */
    gtk_widget_set_direction (W ("table2"), GTK_TEXT_DIR_LTR);

    g_signal_connect (G_OBJECT (W ("month-combobox")), "changed",
                      G_CALLBACK (on_user_month_year_changed), xfdtdlg);

    num_days = g_date_get_days_in_month (g_date_time_get_month (priv->date),
                                         g_date_time_get_year (priv->date));
    adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (g_date_time_get_day_of_month (priv->date), 1,
                                                     num_days + 1, 1, 10, 0));
    gtk_spin_button_set_adjustment (GTK_SPIN_BUTTON (W ("day-spinbutton")),
                                    adjustment);
    g_signal_connect (G_OBJECT (W("day-spinbutton")), "value-changed",
                      G_CALLBACK (on_user_day_changed), xfdtdlg);

    adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (g_date_time_get_year (priv->date),
                                                     0.0, G_MAXDOUBLE, 1,
                                                     10, 0));
    gtk_spin_button_set_adjustment (GTK_SPIN_BUTTON (W("year-spinbutton")),
                                    adjustment);
    g_signal_connect (G_OBJECT (W("year-spinbutton")), "value-changed",
                      G_CALLBACK (on_user_month_year_changed), xfdtdlg);

    /* set up timezone map */
    priv->map = widget = GTK_WIDGET (xfdts_timezone_map_new ());
    gtk_widget_show (widget);

    gtk_container_add (GTK_CONTAINER (W ("aspectmap")),
                       widget);

    update_displayed_date (xfdtdlg);
    update_displayed_time (xfdtdlg);
    update_apply_state (xfdtdlg);

    priv->locations = GTK_TREE_MODEL (W ("region-liststore"));

    load_regions_model (GTK_LIST_STORE (priv->locations),
                        GTK_LIST_STORE (W ("city-liststore")));

    city_modelfilter = GTK_TREE_MODEL_FILTER (W ("city-modelfilter"));

    city_modelsort = GTK_TREE_MODEL_SORT (W ("city-modelsort"));
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (city_modelsort), 
										  CITY_COL_CITY_TRANSLATED,
                                          GTK_SORT_ASCENDING);

    widget = GTK_WIDGET (W ("region_combobox"));
    gtk_tree_model_filter_set_visible_func (city_modelfilter,
                                            (GtkTreeModelFilterVisibleFunc) city_model_filter_func,
                                            widget,
                                            NULL);

    /* After the initial setup, so we can be sure that
    * the model is filled up */
    get_initial_timezone (xfdtdlg);

    widget = GTK_WIDGET (W ("region_combobox"));
    g_signal_connect (widget, "changed", G_CALLBACK (on_user_region_changed), xfdtdlg);

    widget = GTK_WIDGET (W ("city_combobox"));
    g_signal_connect (widget, "changed", G_CALLBACK (on_user_city_changed), xfdtdlg);

    g_signal_connect (priv->map, "location-changed",
                      G_CALLBACK (on_user_map_location_changed), xfdtdlg);

    /* Watch changes of timedated remote service properties */
    if (priv->dtm)
    {
        g_signal_connect (priv->dtm, "g-properties-changed",
                          G_CALLBACK (on_system_timedate_props_changed), xfdtdlg);
        g_signal_connect_swapped (priv->dtm, "notify::ntp",
                                  G_CALLBACK (on_system_ntp_changed), xfdtdlg);
        g_signal_connect_swapped (priv->dtm, "notify::timezone",
                                  G_CALLBACK (on_system_timezone_changed), xfdtdlg);
    }
    /* We ignore UTC <--> LocalRTC changes at the moment */

    /* start update timer */
    priv->timeout_id = g_timeout_add_seconds (1,
                                              callback_timer,
                                              xfdtdlg);
}
