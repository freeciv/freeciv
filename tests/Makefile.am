## Process this file with automake to produce Makefile.in

SUBDIRS = .

# Currently the "check" directive creates a check-output file containing the
# results of the checks.  It might be better to actually fail the make run
# if the check fails.
check:
	rm -f check-output
	$(srcdir)/header_guard.sh $(top_srcdir) >> check-output
	$(srcdir)/va_list.sh $(top_srcdir) >> check-output
	$(srcdir)/copyright.sh $(top_srcdir) >> check-output
	cat check-output | sed "s\\$(top_srcdir)/\\\\" > check-output

check_SCRIPTS =	header_guard.sh			\
		va_list.sh
