## Process this file with automake to produce Makefile.in

# Currently the "src-check" directive creates a check-output file containing
# the results of the checks. It might be better to actually fail the make run
# if the check fails.
src-check:
	$(srcdir)/src-check.sh

CLEANFILES = check-output

EXTRA_DIST =	check_macros.sh			\
		copyright.sh			\
		fcintl.sh			\
		generate_rs_not_broken.sh	\
		generate_rs_save.sh		\
		header_guard.sh			\
		rulesets_save.sh.in		\
		rulesets_upgrade.sh.in		\
		rs_test_res/ruleset_is.lua	\
		rs_test_res/ruleset_list_dist.txt \
		rs_test_res/ruleset_list_opt.txt \
		rs_test_res/ruleset_loads.sh.in	\
		rs_test_res/generate_ruleset_loads.sh \
		rulesets_autohelp.sh.in		\
		src-check.sh			\
		trailing_spaces.sh		\
		va_list.sh
