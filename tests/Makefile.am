## Process this file with automake to produce Makefile.in

# Currently the "src-check" directive creates a check-output file containing
# the results of the checks.  It might be better to actually fail the make run
# if the check fails.
src-check:
	rm -f check-output
	$(srcdir)/header_guard.sh $(top_srcdir) >> check-output
	$(srcdir)/va_list.sh $(top_srcdir) >> check-output
	$(srcdir)/copyright.sh $(top_srcdir) >> check-output
	cat check-output | sed "s\\$(top_srcdir)/\\\\" > check-output

.PHONY: src-check

CLEANFILES = check-output

EXTRA_DIST =					\
		copyright.sh			\
		header_guard.sh			\
		va_list.sh
