AC_DEFUN([FC_CHECK_AUTH], [
USER_DB_LIB=""
USER_DB_DEP=""
AC_ARG_ENABLE(auth,
[  --enable-auth[[=lib]]     enable authentication 
                                 [[default userdb lib=server/userdb/libuserdb.a]]],
[
  auth=true
  user_db_lib=${enableval}
  if test "x$user_db_lib" = "" || test "x$user_db_lib" = "xyes" ; then
     user_db_lib="userdb/libuserdb.a"
  fi
], [
  auth=false
  user_db_lib=""
])
if test "x$auth" = "xtrue" ; then
  AC_DEFINE(AUTHENTICATION_ENABLED, 1, [authentication support])
  USER_DB_LIB=$user_db_lib

  if test "x$user_db_lib" = "xuserdb/libuserdb.a" ; then
    USER_DB_DEP=$user_db_lib
  fi
fi
AC_SUBST(USER_DB_LIB)
AC_SUBST(USER_DB_DEP)
])
