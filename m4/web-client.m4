# Configure checks for freeciv-web

AC_DEFUN([FC_WEB_CLIENT],
[
  AM_CONDITIONAL([FREECIV_WEB], [test "x$fcweb" = "xtrue"])

  if test "x$fcweb" = "xtrue" ; then
    AC_DEFINE([FREECIV_WEB], [1], [Build freeciv-web version])
  fi
])

AC_DEFUN([FC_WEB_OPTIONS],
[
AC_ARG_ENABLE([json],
  AS_HELP_STRING([--enable-json], [enable json network protocol]),
[case "${enableval}" in
  yes|no) json_enabled=${enableval} ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-json]) ;;
esac], [json_enabled=no])

if test "x$json_enabled" = "xyes" ; then
  AC_CHECK_LIB([jansson], [json_object_set_new],
[LIBS="${LIBS} -ljansson"],
[AC_MSG_ERROR([cannot find libjansson])])

  AC_CHECK_HEADER([jansson.h], [],
[AC_MSG_ERROR([libjansson found but not jansson.h])])

  AC_DEFINE([FREECIV_JSON_CONNECTION], [1], [jansson network protocol in use])
fi
])
