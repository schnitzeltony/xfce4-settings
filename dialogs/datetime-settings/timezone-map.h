/*
 * Copyright (C) 2010 Intel, Inc
 *
 * (Back)Ported to xfce/gtk2
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
 * Author: Thomas Wood <thomas.wood@intel.com>
 *
 */


#ifndef _TIMEZONE_MAP_H
#define _TIMEZONE_MAP_H

#include <gtk/gtk.h>
#include "tz.h"

G_BEGIN_DECLS

#define XFDTS_TYPE_TIMEZONE_MAP xfdts_timezone_map_get_type()

#define XFDTS_TIMEZONE_MAP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
  XFDTS_TYPE_TIMEZONE_MAP, XfdtsTimezoneMap))

#define XFDTS_TIMEZONE_MAP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), \
  XFDTS_TYPE_TIMEZONE_MAP, XfdtsTimezoneMapClass))

#define XFDTS_IS_TIMEZONE_MAP(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
  XFDTS_TYPE_TIMEZONE_MAP))

#define XFDTS_IS_TIMEZONE_MAP_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), \
  XFDTS_TYPE_TIMEZONE_MAP))

#define XFDTS_TIMEZONE_MAP_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), \
  XFDTS_TYPE_TIMEZONE_MAP, XfdtsTimezoneMapClass))

typedef struct _XfdtsTimezoneMap XfdtsTimezoneMap;
typedef struct _XfdtsTimezoneMapClass XfdtsTimezoneMapClass;
typedef struct _XfdtsTimezoneMapPrivate XfdtsTimezoneMapPrivate;

struct _XfdtsTimezoneMap
{
  GtkWidget parent;

  XfdtsTimezoneMapPrivate *priv;
};

struct _XfdtsTimezoneMapClass
{
  GtkWidgetClass parent_class;
};

GType xfdts_timezone_map_get_type (void) G_GNUC_CONST;

XfdtsTimezoneMap *xfdts_timezone_map_new (void);

gboolean xfdts_timezone_map_set_timezone (XfdtsTimezoneMap *map,
                                          const gchar   *timezone);
TzLocation * xfdts_timezone_map_get_location (XfdtsTimezoneMap *map);

G_END_DECLS

#endif /* _TIMEZONE_MAP_H */
