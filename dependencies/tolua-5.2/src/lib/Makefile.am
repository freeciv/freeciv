noinst_LTLIBRARIES = libtolua.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/dependencies/tolua-5.2/include \
	$(LUA_CFLAGS)

libtolua_la_SOURCES = \
	tolua_event.c \
	tolua_event.h \
	tolua_is.c \
	tolua_map.c \
	tolua_push.c \
	tolua_to.c


if !SYS_TOLUA_CMD

EXTRA_DIST = \
	dist-stamp

# To trick 'make dist' to build tolua, needed later to generate source files
dist-stamp : libtolua.la
	touch dist-stamp

DISTCLEANFILES = dist-stamp

endif
