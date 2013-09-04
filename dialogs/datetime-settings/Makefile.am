# $Id$

SUBDIRS = po-timezones

icons16dir = $(datadir)/icons/hicolor/16x16/apps
dist_icons16_DATA = icons/16x16/preferences-system-time.png
icons22dir = $(datadir)/icons/hicolor/22x22/apps
dist_icons22_DATA = icons/22x22/preferences-system-time.png
icons32dir = $(datadir)/icons/hicolor/32x32/apps
dist_icons32_DATA = icons/32x32/preferences-system-time.png
icons48dir = $(datadir)/icons/hicolor/48x48/apps
dist_icons48_DATA = icons/48x48/preferences-system-time.png
icons256dir = $(datadir)/icons/hicolor/256x256/apps
dist_icons256_DATA = icons/256x256/preferences-system-time.png
iconsscalabledir = $(datadir)/icons/hicolor/scalable/apps
dist_iconsscalable_DATA = icons/scalable/preferences-system-time.svg

tzdatadir = $(datadir)/xfce4/datetime-settings
dist_tzdata_DATA = backward

AM_CPPFLAGS = \
	-I${top_srcdir} \
	-DXFCE_SETTINGS_DATA_DIR=\"$(datadir)/xfce4/datetime-settings\" \
	-DDATETIME_RESOURCE_PATH=\"/org/xfce/xfce4-settings/datetime\" \
	-DSRCDIR=\"$(top_srcdir)\" \
	-DLOCALEDIR=\"$(localedir)\" \
	-DG_LOG_DOMAIN=\"xfce4-datetime-settings\" \
	$(PLATFORM_CPPFLAGS)


dbus_built_sources = timedated.c timedated.h
resource_built_sources = datetime-resources.c datetime-resources.h datetime-dialog_ui.h

if MAINTAINER_MODE
datetime-dialog_ui.h: datetime-dialog.glade
	exo-csource --static --strip-comments --strip-content --name=datetime_dialog_ui $< >$@

resource_files = $(shell glib-compile-resources --sourcedir=$(srcdir) --generate-dependencies $(srcdir)/datetime.gresource.xml)
datetime-resources.c: datetime.gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-source --c-name datetime $<
datetime-resources.h: datetime.gresource.xml $(resource_files)
	$(AM_V_GEN) glib-compile-resources --target=$@ --sourcedir=$(srcdir) --generate-header --c-name datetime $<

# This requires running d-bus session and accessible timedate1 daemon
# FIXME: need to find a way how to filter out unnecessary d-bus stuff (introspectable, properties)
#timedated1-interface.xml:
#	gdbus introspect					\
#		--xml						\
#		--system					\
#		--dest org.freedesktop.timedate1		\
#		--object-path /org/freedesktop/timedate1	\
#		 > timedated1-interface.xml

timedated.c: timedated.h
timedated.h: Makefile.am timedated1-interface.xml
	gdbus-codegen						\
		--interface-prefix org.freedesktop.		\
		--generate-c-code timedated			\
		$(srcdir)/timedated1-interface.xml

BUILT_SOURCES =                   \
	$(dbus_built_sources)         \
	$(resource_built_sources)
endif

bin_PROGRAMS = \
	xfce4-datetime-settings

xfce4_datetime_settings_SOURCES = \
	main.c                        \
	timezone-map.c                \
	timezone-map.h                \
	tz.c tz.h                     \
	datetime-dialog.c             \
	datetime-dialog.h             \
	$(dbus_built_sources)         \
	$(resource_built_sources)

xfce4_datetime_settings_CFLAGS = \
	$(GTK_CFLAGS) \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(LIBXFCE4UI_CFLAGS) \
	$(XI_CFLAGS) \
	$(LIBX11_CFLAGS) \
	$(POLKIT_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(PLATFORM_CFLAGS)

xfce4_datetime_settings_LDFLAGS = \
	-no-undefined \
	$(PLATFORM_LDFLAGS)

xfce4_datetime_settings_LDADD = \
	$(GTK_LIBS) \
	$(LIBXFCE4UTIL_LIBS) \
	$(LIBXFCE4UI_LIBS) \
	$(XI_LIBS) \
	$(LIBX11_LIBS) \
	$(POLKIT_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS) \
	-lm


@INTLTOOL_DESKTOP_RULE@

desktopdir = $(datadir)/applications
desktop_in_files = xfce-datetime.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

EXTRA_DIST = \
	$(desktop_in_files)

DISTCLEANFILES = \
	$(desktop_DATA)

