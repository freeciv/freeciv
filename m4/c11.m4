# Check for the presence of C11 features.

# Check for C11 _Static_assert
#
AC_DEFUN([FC_C11_STATIC_ASSERT],
[
  AC_CACHE_CHECK([for C11 static assert], [ac_cv_c11_static_assert],
    [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <assert.h>
]], [[ _Static_assert(1, "1 is not true"); ]])],
[ac_cv_c11_static_assert=yes], [ac_cv_c11_static_assert=no])])
  if test "x${ac_cv_c11_static_assert}" = "xyes" ; then
    AC_DEFINE([C11_STATIC_ASSERT], [1], [C11 static assert supported])
  fi
])
