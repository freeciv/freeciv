# Do checks for Freeciv authentication support
#
# Called without any parameters.

AC_DEFUN([FC_CHECK_AUTH],
[
  # If the user calls --without-auth this will fail.
  AC_ARG_WITH([auth],
	      [  --with-auth=lib         Specify authentication database library],
  	      [USER_DB_LIB="$withval"], [USER_DB_LIB="yes"])

  if test "$USER_DB_LIB" = "yes" || test "$USER_DB_LIB" = "no"; then
    USER_DB_LIB="userdb/libuserdb.a"
  fi
  USER_DB_DEP="$USER_DB_LIB"

  AC_SUBST(USER_DB_LIB)
  AC_SUBST(USER_DB_DEP)
])
