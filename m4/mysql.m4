#
# FC_CHECK_MYSQL([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND[, VERSION]]])
# Modified version of AX_LIB_MYSQL(), for freeciv use.
# Header of original AX_LIB_MYSQL() below.
# Upstream version is that of 2009-04-27 from
# https://git.savannah.gnu.org/gitweb/?p=autoconf-archive.git;a=history;f=m4/ax_lib_mysql.m4

# ===========================================================================
#              http://autoconf-archive.cryp.to/ax_lib_mysql.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_LIB_MYSQL([MINIMUM-VERSION])
#
# DESCRIPTION
#
#   This macro provides tests of availability of MySQL client library of
#   particular version or newer.
#
#   AX_LIB_MYSQL macro takes only one argument which is optional. If there
#   is no required version passed, then macro does not run version test.
#
#   The --with-mysql option takes one of three possible values:
#
#   no - do not check for MySQL client library
#
#   yes - do check for MySQL library in standard locations (mysql_config
#   should be in the PATH)
#
#   path - complete path to mysql_config utility, use this option if
#   mysql_config can't be found in the PATH
#
#   This macro calls:
#
#     AC_SUBST(MYSQL_CFLAGS)
#     AC_SUBST(MYSQL_LDFLAGS)
#     AC_SUBST(MYSQL_VERSION)
#
#   And sets:
#
#     HAVE_MYSQL
#
# LICENSE
#
#   Copyright (c) 2008 Mateusz Loskot <mateusz@loskot.net>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved.

AC_DEFUN([FC_CHECK_MYSQL],
[
  AC_ARG_WITH([mysql-prefix],
    AS_HELP_STRING([--with-mysql-prefix=PFX], [Prefix where MySql is installed (optional)]),
[mysql_prefix="$withval"], [mysql_prefix=""])

  mysql_cflags=""
  mysql_ldflags=""
  MYSQL_VERSION=""

  dnl
  dnl Check MySQL libraries (libmysql)
  dnl

  if test -z "$MYSQL_CONFIG" -o test; then
    AC_PATH_PROGS([MYSQL_CONFIG],
      [mysql_config mariadb-config mariadb_config], [no])
  fi

  if test "$MYSQL_CONFIG" != "no"; then
    AC_MSG_CHECKING([for MySQL libraries])

    mysql_cflags="`$MYSQL_CONFIG --cflags`"
    mysql_ldflags="`$MYSQL_CONFIG --libs`"
    MYSQL_VERSION=`$MYSQL_CONFIG --version`

    # Remove NDEBUG from MYSQL_CFLAGS
    mysql_cflags=`echo $mysql_cflags | $SED -e 's/-DNDEBUG//g'`

    found_mysql="yes"
    AC_MSG_RESULT([yes])

  fi

  dnl
  dnl Check if required version of MySQL is available
  dnl


  mysql_version_req=ifelse([$3], [], [], [$3])

  if test "$found_mysql" = "yes" -a -n "$mysql_version_req"; then

    AC_MSG_CHECKING([if MySQL version is >= $mysql_version_req])

    dnl Decompose required version string of MySQL
    dnl and calculate its number representation
    mysql_version_req_major=`expr $mysql_version_req : '\([[0-9]]*\)'`
    mysql_version_req_minor=`expr $mysql_version_req : '[[0-9]]*\.\([[0-9]]*\)'`
    mysql_version_req_micro=`expr $mysql_version_req : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
    if test "x$mysql_version_req_micro" = "x"; then
      mysql_version_req_micro="0"
    fi

    mysql_version_req_number=`expr $mysql_version_req_major \* 1000000 \
                              \+ $mysql_version_req_minor \* 1000 \
                              \+ $mysql_version_req_micro`

    dnl Decompose version string of installed MySQL
    dnl and calculate its number representation
    mysql_version_major=`expr $MYSQL_VERSION : '\([[0-9]]*\)'`
    mysql_version_minor=`expr $MYSQL_VERSION : '[[0-9]]*\.\([[0-9]]*\)'`
    mysql_version_micro=`expr $MYSQL_VERSION : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
    if test "x$mysql_version_micro" = "x"; then
      mysql_version_micro="0"
    fi

    mysql_version_number=`expr $mysql_version_major \* 1000000 \
                          \+ $mysql_version_minor \* 1000 \
                          \+ $mysql_version_micro`

    mysql_version_check=`expr $mysql_version_number \>\= $mysql_version_req_number`
    if test "$mysql_version_check" = "1"; then
      AC_MSG_RESULT([yes])
    else
      AC_MSG_RESULT([no])
    fi
  fi

  AC_SUBST([MYSQL_VERSION])
  AC_SUBST([mysql_cflags])
  AC_SUBST([mysql_ldflags])

  if test "x$found_mysql" = "xyes" ; then
    ifelse([$1], , :, [$1])
  else
    ifelse([$2], , :, [$2])
  fi
])
