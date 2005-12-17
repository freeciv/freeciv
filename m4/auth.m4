# Do checks for Freeciv authentication support
#
# Called without any parameters.

AC_DEFUN([FC_CHECK_AUTH],
[
  dnl  no=do not compile in authentication,  yes=compile in auth,  *=error
  AC_ARG_ENABLE(auth, 
  [  --enable-auth        do not compile in authentication],
  [case "${enableval}" in
    yes) auth=true ;;
    no)  auth=false ;;
    *)   AC_MSG_ERROR(bad value ${enableval} for --enable-auth) ;;
   esac], [auth=false])

  if test x$auth = xtrue; then

    AC_CHECK_HEADER(mysql/mysql.h, , 
                    [AC_MSG_WARN([couldn't find mysql header: disabling auth]);
                     auth=false])

    AC_CHECK_LIB(mysqlclient, mysql_query, 
		 [AUTH_LIBS="-lmysqlclient $AUTH_LIBS";
                  AUTH_DEPS="$AUTH_LIBS"],
                 [AC_MSG_WARN([couldn't find mysql libs: disabling auth]);
                  auth=false])

    AC_SUBST(AUTH_LIBS)
    AC_SUBST(AUTH_DEPS)
  fi

  if test x$auth = xtrue; then
    AC_DEFINE(HAVE_AUTH, 1, [can compile with authentication])
  fi

])
