# Do checks for Freeciv authentication support
#
# Called without any parameters.

AC_DEFUN([FC_CHECK_AUTH],
[
  # If the user calls --without-auth this will fail.
  AC_ARG_WITH([auth],
	      AC_HELP_STRING([--with-auth=lib],
                             [Specify authentication database library]),
  	      [USER_DB_LIB="$withval"])

  if test "$USER_DB_LIB" = "" || test "$USER_DB_LIB" = "yes"; then
    USER_DB_LIB="userdb/libuserdb.a"
  fi
  USER_DB_DEP="$USER_DB_LIB"

  AC_SUBST(USER_DB_LIB)
  AC_SUBST(USER_DB_DEP)
])
