## Process this file with automake to produce Makefile.in

module_dirs = default

if AI_MOD_STATIC_THREADED
module_dirs += threaded
endif

if AI_MODULES
if EXP_AI_MODULES
if !AI_MOD_STATIC_THREADED
module_dirs += threaded
endif
module_dirs += stub
else
# AI module loading supported, but experimental modules not built
endif
endif

SUBDIRS = $(module_dirs)
