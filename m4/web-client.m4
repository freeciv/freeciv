# Configure checks for freeciv-web

AC_DEFUN([FC_WEB_CLIENT],
[
  AM_CONDITIONAL([FREECIV_WEB], [test "x$fcweb" = "xtrue"])

  if test "x$fcweb" = "xtrue" ; then
    AC_DEFINE([FREECIV_WEB], [1], [Build freeciv-web version])

    AC_CHECK_LIB([jansson], [json_object_set_new],
[SERVER_LIBS="${SERVER_LIBS} -ljansson"],
[AC_MSG_ERROR([cannot find libjansson])])

    AC_CHECK_HEADER([jansson.h], [],
[AC_MSG_ERROR([libjansson found but not jansson.h])])
  fi
])
