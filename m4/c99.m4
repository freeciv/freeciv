# Check for the presense of C99 features.  These may be optional or required.

# Check C99-style variadic macros (required):
#
#  #define PRINTF(msg, ...) (printf(msg, __VA_ARGS__)
#
AC_DEFUN(FC_VARIADIC_MACROS,
[
  dnl Check for variadic macros
  AC_CACHE_CHECK([for C99 variadic macros],
    [ac_cv_c99_variadic_macros],
     [AC_TRY_COMPILE(
          [#include <stdio.h>
           #define MSG(...) fprintf(stderr, __VA_ARGS__)
          ],
          [MSG("foo");
           MSG("%s", "foo");
           MSG("%s%d", "foo", 1);],
          ac_cv_c99_variadic_macros=yes,
          ac_cv_c99_variadic_macros=no)])
  if test "x${ac_cv_c99_variadic_macros}" != "xyes"; then
    AC_MSG_ERROR([A compiler supporting C99 variadic macros is required])
  fi
])
