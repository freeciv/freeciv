## Process this file with automake to produce Makefile.in

if AI_MODULES
if EXP_AI_MODULES
module_dirs = stub threaded
else
# AI module loading supported, but experimental modules not built
module_dirs =
endif
else
module_dirs =
endif

SUBDIRS = default $(module_dirs)
