# Check for Freeciv MagickWand support used to create images of the map
#
# Called without any parameters.

AC_DEFUN([FC_MAPIMG_MAGICKWAND],
[
  if test "x$enable_mapimg" = "xauto" || test "x$mapimg_magickwand" = "xyes" ; then

    FC_CHECK_MAGICKWAND(
    [
      MAPIMG_WAND_CFLAGS="$WAND_CFLAGS"
      MAPIMG_WAND_LIBS="$WAND_LIBS"

      AC_SUBST(MAPIMG_WAND_CFLAGS)
      AC_SUBST(MAPIMG_WAND_LIBS)

      AC_DEFINE(HAVE_MAPIMG_MAGICKWAND,1,[ ])
      mapimg_magickwand=yes
    ],
    [
      if test "x$mapimg_magickwand" = "xyes" ; then
        AC_MSG_ERROR([mapimg toolkit magickwandclient not available])
      fi
      mapimg_magickwand=no
    ])
  fi
])
