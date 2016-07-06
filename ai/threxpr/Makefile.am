## Process this file with automake to produce Makefile.in

if AI_MOD_STATIC_THREXPR
noinst_LTLIBRARIES = libthrexprai.la
else
aimodule_LTLIBRARIES = fc_ai_threxpr.la
endif

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/common/networking \
	-I$(top_srcdir)/server/ \
	-I$(top_srcdir)/server/advisors \
	-I$(top_srcdir)/ai/default \
	-I$(top_srcdir)/dependencies/tinycthread

da_sources = \
	texaicity.c		\
	texaicity.h		\
	texaimsg.c		\
	texaimsg.h		\
	texaiplayer.c		\
	texaiplayer.h		\
	threxprai.c

if AI_MOD_STATIC_THREXPR
libthrexprai_la_SOURCES = $(da_sources)
else
fc_ai_threxpr_la_SOURCES = $(da_sources)
fc_ai_threxpr_la_LDFLAGS = -module
endif
