# Check for the presence of behavior usually seen with tcc

# Check if valure must be returned even after exit()
#
AC_DEFUN([FC_VALUE_AFTER_EXIT],
[
  AC_CACHE_CHECK([needs to return value after exit()], [ac_cv_value_after_exit],
    [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <stdlib.h>
int func(void);
int func(void)
{
  exit(1);
}]], [])], [ac_cv_value_after_exit=no],
      [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <stdlib.h>
int func(void);
int func(void)
{
  exit(1);
  return 0;
}]], [])], [ac_cv_value_after_exit=yes], [ac_cv_value_after_exit=unknown])])])
  if test "x${ac_cv_value_after_exit}" = "xyes" ; then
    AC_DEFINE([FREECIV_RETURN_VALUE_AFTER_EXIT], [1], [[Value must be returned even after exit()]])
  fi
])
