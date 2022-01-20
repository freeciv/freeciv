# Check for Freeciv Authentication using ODBC
#
# Called without any parameters.

AC_DEFUN([FC_FCDB_ODBC],
[
  if test "x$fcdb_all" = "xyes" || test "x$fcdb_odbc" = "xyes" ; then

    AC_CHECK_LIB([odbc], [SQLConnect],
      [AC_CHECK_HEADERS(
        [sql.h sqltypes.h sqlext.h],
        [FCDB_ODBC_LIBS="-lodbc"
         AC_DEFINE([HAVE_FCDB_ODBC], [1], [Have ODBC database backend])
         found_odbc=yes])])

    if test "x$found_odbc" != "xyes" ; then
      if test "x$fcdb_odbc" != "xyes" ; then
        AC_MSG_ERROR([Could not find odbc devel files])
      fi
      fcdb_odbc=no
    else
      fcdb_odbc=yes
    fi

  fi

  AM_CONDITIONAL(FCDB_ODBC, test "x$fcdb_odbc" = "xyes")
])
