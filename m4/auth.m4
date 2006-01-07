# Do checks for Freeciv authentication support
#
# Called without any parameters.

AC_DEFUN([FC_CHECK_AUTH],
[
  dnl  no=do not compile in authentication,  yes=compile in auth,  *=error
  AC_ARG_ENABLE(auth, 
  [  --enable-auth        compile in authentication],
  [case "${enableval}" in
    yes) auth=true ;;
    no)  auth=false ;;
    *)   AC_MSG_ERROR(bad value ${enableval} for --enable-auth) ;;
   esac], [auth=false])

  if test x$auth = xtrue; then

    AC_CHECK_HEADER(mysql/mysql.h, , 
                    [AC_MSG_WARN([couldn't find mysql header: disabling auth]);
                     auth=false])

    dnl we need to set -L correctly, we will check once in standard locations
    dnl then we will check with other LDFLAGS. if none of these work, we fail.
 
    AC_CHECK_LIB(mysqlclient, mysql_query, 
		 [AUTH_LIBS="-lmysqlclient $AUTH_LIBS"],
                 [AC_MSG_WARN([couldn't find mysql libs in normal locations]);
                  auth=false])

    fc_preauth_LDFLAGS=$LDFLAGS
    fc_mysql_lib_loc="-L/usr/lib/mysql -L/usr/local/lib/mysql \
                      -L$HOME/lib -L$HOME/lib/mysql"

    for __ldpath in $fc_mysql_lib_loc; do
      unset ac_cv_lib_mysqlclient_mysql_query
      LDFLAGS="$LDFLAGS $__ldpath"
      auth=true

      AC_CHECK_LIB(mysqlclient, mysql_query,
                   [AUTH_LIBS="-lmysqlclient $AUTH_LIBS";
                    AC_MSG_WARN([had to add $__ldpath to LDFLAGS])],
                    [AC_MSG_WARN([couldn't find mysql libs in $__ldpath]);
                     auth=false])

      if test x$auth = xtrue; then
        break
      else
        LDFLAGS=$fc_preauth_LDFLAGS
      fi

    done

    if test x$auth = xfalse; then
      AC_MSG_WARN([can't find mysql -- disabling authentication])
    fi

    AC_SUBST(LDFLAGS)
    AC_SUBST(AUTH_LIBS)
  fi

  if test x$auth = xtrue; then
    AC_DEFINE(HAVE_AUTH, 1, [can compile with authentication])
  fi

])
