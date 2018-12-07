BUILT_SOURCES = manager-dbus-glue.h device-dbus-glue.h $(MARSHALFILES) $(interfaces_DATA)
noinst_HEADERS = $(BUILT_SOURCES)

CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = manager.xml device.xml fprintd-marshal.list

libexec_PROGRAMS = fprintd
noinst_LTLIBRARIES = libfprintd.la

AM_CFLAGS =						\
	$(WARN_CFLAGS)					\
	$(FPRINT_CFLAGS)				\
	$(DAEMON_CFLAGS)				\
	-DG_LOG_DOMAIN=\""fprintd"\"			\
	-DLOCALEDIR=\""$(datadir)/locale"\"		\
	-DPLUGINDIR=\""$(libdir)/fprintd/modules"\"	\
	-DFILE_STORAGE_PATH=\""$(localstatedir)/lib/fprint"\"

libfprintd_la_SOURCES =				\
	manager.c device.c			\
	$(MARSHALFILES)				\
	fprintd.h
libfprintd_la_LIBADD = $(FPRINT_LIBS) $(DAEMON_LIBS)
libfprintd_la_LDFLAGS = -no-undefined

fprintd_SOURCES =				\
	main.c					\
	loop.c loop.h				\
	file_storage.c file_storage.h storage.h
fprintd_LDADD = libfprintd.la

interfaces_DATA = net.reactivated.Fprint.Manager.xml net.reactivated.Fprint.Device.xml
net.reactivated.Fprint.Manager.xml: manager.xml
	cat $< > $@
net.reactivated.Fprint.Device.xml: device.xml
	cat $< > $@
interfacesdir = $(datadir)/dbus-1/interfaces/

manager-dbus-glue.h: manager.xml
	dbus-binding-tool --prefix=fprint_manager --mode=glib-server $< --output=$@

device-dbus-glue.h: device.xml
	dbus-binding-tool --prefix=fprint_device --mode=glib-server $< --output=$@

MARSHALFILES = fprintd-marshal.c fprintd-marshal.h
GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`

fprintd-marshal.h: fprintd-marshal.list
	( $(GLIB_GENMARSHAL) --prefix=fprintd_marshal $(srcdir)/fprintd-marshal.list --header > fprintd-marshal.h )
fprintd-marshal.c: fprintd-marshal.h
	( $(GLIB_GENMARSHAL) --prefix=fprintd_marshal $(srcdir)/fprintd-marshal.list --body --header > fprintd-marshal.c )

