## Process this file with automake to produce Makefile.in

bin_PROGRAMS = civmanual
AM_CPPFLAGS = -I$(top_srcdir)/server -I$(top_srcdir)/utility -I$(top_srcdir)/common -I$(top_srcdir)/ai -I../intl -I$(top_srcdir)/common/aicore -I$(top_srcdir)/server/generator -I$(top_srcdir)/client -I$(top_srcdir)/client/include

## Above, note -I../intl instead of -I$(top_srcdir/intl) is deliberate.

civmanual_SOURCES = \
		civmanual.c

# This is a bit of a hack.  The program links in with the server lib
# but also uses some files from the client.
civmanual_DEPENDENCIES = ../utility/libcivutility.a ../common/libcivcommon.a \
      ../ai/libcivai.a ../utility/libcivutility.a ../server/libcivserver.a \
      ../utility/libcivutility.a ../common/aicore/libaicore.a \
      ../client/helpdata.o \
      ../server/generator/libgenerator.a
civmanual_LDADD        = ../utility/libcivutility.a ../common/libcivcommon.a \
      ../ai/libcivai.a ../utility/libcivutility.a ../server/libcivserver.a \
      @INTLLIBS@ ../client/helpdata.o \
      ../utility/libcivutility.a ../common/libcivcommon.a ../ai/libcivai.a \
      ../utility/libcivutility.a ../server/libcivserver.a \
      ../utility/libcivutility.a \
      ../common/aicore/libaicore.a \
      ../server/generator/libgenerator.a \
      $(SERVER_LIBS) $(AUTH_LIBS)
