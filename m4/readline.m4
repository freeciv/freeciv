
dnl FC_CHECK_READLINE_RUNTIME(EXTRA-LIBS, ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)
dnl
dnl This tests whether readline works at runtime.  Here, "works"
dnl means "doesn't dump core", as some versions do if linked
dnl against wrong ncurses library.  Compiles with LIBS modified 
dnl to included -lreadline and parameter EXTRA-LIBS.
dnl Should already have checked that header and library exist.
dnl
AC_DEFUN(FC_CHECK_READLINE_RUNTIME,
[AC_MSG_CHECKING(whether readline works at runtime)
templibs="$LIBS"
LIBS="-lreadline $1 $LIBS"
AC_TRY_RUN([
/*
 * testrl.c
 * File revision 0
 * Check to make sure that readline works at runtime.
 * (Specifically, some readline packages link against a wrong 
 * version of ncurses library and dump core at runtime.)
 * (c) 2000 Jacob Lundberg, jacob@chaos2.org
 */

#include <stdio.h>
/* We assume that the presence of readline has already been verified. */
#include <readline/readline.h>
#include <readline/history.h>

/* Setup for readline. */
#define TEMP_FILE "./conftest.readline.runtime"

static void handle_readline_input_callback(char *line) {
/* Generally taken from freeciv-1.11.4/server/sernet.c. */
  if(line) {
    if(*line)
      add_history(line);
    /* printf(line); */
  }
}

int main(void) {
/* Try to init readline and see if it barfs. */
  using_history();
  read_history(TEMP_FILE);
  rl_initialize();
  rl_callback_handler_install("_ ", handle_readline_input_callback);
  rl_callback_handler_remove();  /* needed to re-set terminal */
  return(0);
}
],
[AC_MSG_RESULT(yes)
  [$2]],
[AC_MSG_RESULT(no)
  [$3]],
[AC_MSG_RESULT(unknown: cross-compiling)
  [$2]])
LIBS="$templibs"
])

