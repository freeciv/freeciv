# Check for the presence of C11 features.

# Check for C11 _Static_assert
#
AC_DEFUN([FC_C11_STATIC_ASSERT],
[
  AC_CACHE_CHECK([for C11 static assert], [ac_cv_c11_static_assert],
    [AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <assert.h>
]], [[ _Static_assert(1, "1 is not true"); ]])],
[ac_cv_c11_static_assert=yes], [ac_cv_c11_static_assert=no])])
  if test "x${ac_cv_c11_static_assert}" = "xyes" ; then
    AC_DEFINE([C11_STATIC_ASSERT], [1], [C11 static assert supported])
  fi
])

AC_DEFUN([FC_C11_AT_QUICK_EXIT],
[
  AC_CACHE_CHECK([for C11 at_quick_exit()], [ac_cv_c11_at_quick_exit],
    [AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdlib.h>
static void func(void)
{}
]], [[ at_quick_exit(func); ]])],
[ac_cv_c11_at_quick_exit=yes], [ac_cv_c11_at_quick_exit=no])])
  if test "x${ac_cv_c11_at_quick_exit}" = "xyes" ; then
    AC_DEFINE([HAVE_AT_QUICK_EXIT], [1], [C11 at_quick_exit() available])
  fi
])
