#serial AM1
dnl From Bruno Haible.

AC_DEFUN([AM_LANGINFO_CODESET],
[
  AC_CACHE_CHECK([for nl_langinfo and CODESET], am_cv_langinfo_codeset,
    [AC_TRY_LINK([#include <langinfo.h>],
      [char* cs = nl_langinfo(CODESET);],
      am_cv_langinfo_codeset=yes,
      am_cv_langinfo_codeset=no)
    ])
  if test $am_cv_langinfo_codeset = yes; then
    AC_DEFINE(HAVE_LANGINFO_CODESET, 1,
      [Define if you have <langinfo.h> and nl_langinfo(CODESET).])
  fi
])

AC_DEFUN([AM_LIBCHARSET],
[
  AC_CACHE_CHECK([for libcharset], am_cv_libcharset,
    [lc_save_LIBS="$LIBS"
     LIBS="$LIBS $LIBICONV"
     AC_TRY_LINK([#include <libcharset.h>],
      [locale_charset()],
      am_cv_libcharset=yes,
      am_cv_libcharset=no) 
      LIBS="$lc_save_LIBS" 
    ])
  if test $am_cv_libcharset = yes; then
    AC_DEFINE(HAVE_LIBCHARSET, 1,
      [Define if you have <libcharset.h> and locale_charset().])
  fi
])
