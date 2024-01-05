## Process this file with automake to produce Makefile.in

ENUMS_GEN = \
		actions_enums_gen.h	\
		manual_enums_gen.h	\
		terrain_enums_gen.h

EXTRA_DIST = \
		actions_enums.def	\
		manual_enums.def	\
		terrain_enums.def	\
		$(ENUMS_GEN)

BUILT_SOURCES = $(ENUMS_GEN)

$(ENUMS_GEN) : %_enums_gen.h: %_enums.def
	@if ! $(AM_V_P) ; then echo "  genenum  $@" ; fi
	$(AM_V_at) $(top_srcdir)/gen_headers/generate_enums.py $(srcdir)/$@ $<
