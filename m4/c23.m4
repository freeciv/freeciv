# Check for the presence of C23 features.

# Check for C23 nullptr that can be passed as sentinel
# and define FREECIV_HAVE_C23_NULLPTR if such is available
#
AC_DEFUN([FC_C23_NULLPTR],
[
  AC_LANG_PUSH([C])
  AC_CHECK_HEADERS([stddef.h])
  AC_CACHE_CHECK([for C23 nullptr], [ac_cv_c23_nullptr],
    [_cflags_="$CFLAGS"
     CFLAGS="$EXTRA_DEBUG_CFLAGS $CFLAGS"
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
[[#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif
void sentinental(...) __attribute__((__sentinel__(0)));]],
 [[ sentinental(nullptr); ]])],
[ac_cv_c23_nullptr=yes], [ac_cv_c23_nullptr=no])]
  CFLAGS="$_cflags_")
  if test "x${ac_cv_c23_nullptr}" = "xyes" ; then
    AC_DEFINE([FREECIV_HAVE_C23_NULLPTR], [1], [C23 nullptr available])
  fi
  AC_LANG_POP([C])
])
