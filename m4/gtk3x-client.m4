# Try to configure the GTK-3x client (gui-gtk-4.0)

# FC_GTK3X_CLIENT
# Test for GTK-3.98.4 libraries needed for gui-gtk-4.0

AC_DEFUN([FC_GTK3X_CLIENT],
[
  # Add checks "x$client" = "xauto" and "x$client" = "xall"
  # when this becomes supported client
  if test "x$gui_gtk3x" = "xyes" ; then
    PKG_CHECK_MODULES([GTK3X], [gtk4 >= 3.98.4],
      [
        GTK3X_CFLAGS="$GTK3X_CFLAGS -DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_94"
        GTK3X_CFLAGS="$GTK3X_CFLAGS -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_64"
        gui_gtk3x=yes
        if test "x$client" = "xauto" ; then
          client=yes
        fi
        gui_gtk3x_cflags="$GTK3X_CFLAGS"
        gui_gtk3x_libs="$GTK3X_LIBS"
        if test "x$MINGW" = "xyes"; then
          dnl Required to compile gtk3 on Windows platform
          gui_gtk3x_cflags="$gui_gtk3x_cflags -mms-bitfields"
          gui_gtk3x_ldflags="$gui_gtk3x_ldflags $MWINDOWS_FLAG"
        fi
      ],
      [
        FC_NO_CLIENT([gtk3x], [GTK-3.9x libraries not found])
      ])
  fi
])
