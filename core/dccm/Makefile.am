AM_CFLAGS = -Wall \
	-DLOCALSTATEDIR=\""$(localstatedir)"\"

libexec_PROGRAMS = dccm synce-serial-chat

BUILT_SOURCES = synce-device-signals-marshal.h \
		synce-device-signals-marshal.c \
		synce-device-manager-control-signals-marshal.h \
		synce-device-manager-control-signals-marshal.c \
		synce-device-dbus.h \
		synce-device-dbus.c \
		synce-device-manager-control-dbus.h \
		synce-device-manager-control-dbus.c \
		synce-device-manager-dbus.c \
		synce-device-manager-dbus.h

CLEANFILES = $(BUILT_SOURCES)

dccm_SOURCES = log.c log.h \
		synce-device.c synce-device.h synce-device-internal.h \
		synce-device-rndis.c synce-device-rndis.h \
		synce-device-legacy.c synce-device-legacy.h \
		synce-connection-broker.c synce-connection-broker.h \
		utils.c utils.h \
		synce-device-manager.c synce-device-manager.h \
		synce-device-manager-control.c synce-device-manager-control.h \
		main.c 

nodist_dccm_SOURCES = $(BUILT_SOURCES)

dccm_CPPFLAGS = \
	-I$(top_srcdir)/lib/utils \
	-I$(top_builddir)/lib/utils \
	$(GLIB_CFLAGS) \
	$(GUDEV_CFLAGS) -DG_UDEV_API_IS_SUBJECT_TO_CHANGE

dccm_LDADD = \
	$(top_builddir)/lib/libsynce.la \
	$(GLIB_LIBS) \
	$(GUDEV_LIBS)

synce_serial_chat_SOURCES = synce-serial-chat.c

EXTRA_DIST = synce-device-signals-marshal.list \
		synce-device.xml \
		synce-device-manager-control-signals-marshal.list \
		synce-device-manager-control.xml \
		synce-device-manager.xml \
		synce-unlock

bin_SCRIPTS = synce-unlock

%-signals-marshal.h: %-signals-marshal.list
	glib-genmarshal --header --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.h

%-signals-marshal.c: %-signals-marshal.list
	glib-genmarshal --body --prefix=$(subst -,_,$*)_marshal $< > $*-signals-marshal.c


%-dbus.c %-dbus.h: %.xml
	gdbus-codegen --interface-prefix org.synce.dccm. --generate-c-code $*-dbus --c-namespace SynceDbus $<

