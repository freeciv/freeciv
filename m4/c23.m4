# Check for the presence of C23 features.

# Check for C23 nullptr, and define FREECIV_HAVE_C23_NULLPTR if it's available
#
AC_DEFUN([FC_C23_NULLPTR],
[
  AC_LANG_PUSH([C])
  AC_CHECK_HEADERS([stddef])
  AC_CACHE_CHECK([for C23 nullptr], [ac_cv_c23_nullptr],
    [AC_LINK_IFELSE([AC_LANG_PROGRAM(
[[#ifdef HAVE_STDDEF
#include <stddef>
#endif]],
 [[ int *var = nullptr; ]])],
[ac_cv_c23_nullptr=yes], [ac_cv_c23_nullptr=no])])
  if test "x${ac_cv_c23_nullptr}" = "xyes" ; then
    AC_DEFINE([FREECIV_HAVE_C23_NULLPTR], [1], [C23 nullptr available])
  fi
  AC_LANG_POP([C])
])
