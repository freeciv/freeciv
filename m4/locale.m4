#serial 2

AC_DEFUN([FC_LIBCHARSET],
[
  AC_CACHE_CHECK([for libcharset], am_cv_libcharset,
    [lc_save_LIBS="$LIBS"
     LIBS="$LIBS $LIBICONV"
     AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <libcharset.h>]], [[locale_charset()]])], [am_cv_libcharset=yes],
     [LIBS="$LIBS -lcharset"
      AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <libcharset.h>]], [[locale_charset()]])],
      [dnl Link against libcharset also when building actual freeciv
       lc_save_LIBS="$lc_save_LIBS -lcharset"
       am_cv_libcharset=yes],
      [am_cv_libcharset=no])])
     LIBS="$lc_save_LIBS"
    ])
  if test $am_cv_libcharset = yes; then
    AC_DEFINE([HAVE_LIBCHARSET], [1],
      [Define if you have <libcharset.h> and locale_charset().])
  fi
])
