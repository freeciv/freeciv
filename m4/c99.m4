# Check for the presense of C99 features.  These may be optional or required.

# Check C99-style variadic macros (required):
#
#  #define PRINTF(msg, ...) (printf(msg, __VA_ARGS__)
#
AC_DEFUN([FC_VARIADIC_MACROS],
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

# Check C99-style variable-sized arrays (required):
#
#   char concat_str[strlen(s1) + strlen(s2) + 1];
#
AC_DEFUN([FC_VARIABLE_ARRAYS],
[
  dnl Check for variable arrays
  AC_CACHE_CHECK([for C99 variable arrays],
    [ac_cv_c99_variable_arrays],
    [AC_TRY_COMPILE(
        [],
        [char *s1 = "foo", *s2 = "bar";
         char s3[strlen(s1) + strlen(s2) + 1];
         sprintf(s3, "%s%s", s1, s2);],
        ac_cv_c99_variable_arrays=yes,
        ac_cv_c99_variable_arrays=no)])
  if test "x${ac_cv_c99_variable_arrays}" != "xyes"; then
    AC_MSG_ERROR([A compiler supporting C99 variable arrays is required])
  fi
])
