# Check for Freeciv Authentication using sqlite3
#
# Called without any parameters.

AC_DEFUN([FC_FCDB_SQLITE3],
[
  if test "x$fcdb_all" = "xyes" || test "x$fcdb_sqlite3" = "xyes" ; then

    FC_CHECK_SQLITE3(
    [
      FCDB_SQLITE3_CFLAGS="$SQLITE3_CFLAGS"
      FCDB_SQLITE3_LIBS="$SQLITE3_LDFLAGS"

      AC_SUBST(FCDB_SQLITE3_CFLAGS)
      AC_SUBST(FCDB_SQLITE3_LIBS)

      AC_DEFINE(HAVE_FCDB_SQLITE3,1,[ ])
      fcdb_sqlite3=yes
    ],
    [
      if test "x$fcdb_sqlite3" = "xyes" ; then
        AC_MSG_ERROR([fcdb database sqlite3 not available])
      fi
      fcdb_sqlite3=no
    ])

  fi

  AM_CONDITIONAL(FCDB_SQLITE3, test "x$fcdb_sqlite3" = "xyes")
])
