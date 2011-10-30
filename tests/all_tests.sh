#!/bin/bash

# define the log file
DATE=`date +%Y%m%d%H%M%S`
LOGFILE="./all_tests-${DATE}.log"

# define the scripts to be run
SCRIPTS="
check_macros.sh
copyright.sh
fcintl.sh
header_guard.sh
va_list.sh
"

# create the logfile
touch $LOGFILE

# run all scripts
for II in $SCRIPTS; do
  echo -e "running '$II' ...\n"	| tee -a $LOGFILE
  ./$II ../			| tee -a $LOGFILE
  echo -e "\n\n"		| tee -a $LOGFILE
done
