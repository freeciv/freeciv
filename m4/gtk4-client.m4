# Try to configure the GTK-4.0 client (gui-gtk-4.0)

# FC_GTK4_CLIENT
# Test for GTK-4.0 libraries needed for gui-gtk-4.0

AC_DEFUN([FC_GTK4_CLIENT],
[
  # Add check "x$client" = "xauto" when this becomes supported client
  if test "x$gui_gtk4" = "xyes" || test "x$client" = "xall" ; then
    PKG_CHECK_MODULES([GTK4], [gtk4 >= 4.0.0],
      [
        GTK4_CFLAGS="$GTK4_CFLAGS -DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_0"
        GTK4_CFLAGS="$GTK4_CFLAGS -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_66"
        gui_gtk4=yes
        if test "x$client" = "xauto" ; then
          client=yes
        fi
        gui_gtk4_cflags="$GTK4_CFLAGS"
        gui_gtk4_libs="$GTK4_LIBS"
        if test "x$MINGW" = "xyes"; then
          dnl Required to compile gtk4 on Windows platform
          gui_gtk4_cflags="$gui_gtk4_cflags -mms-bitfields"
          gui_gtk4_ldflags="$gui_gtk4_ldflags $MWINDOWS_FLAG"
        fi
      ],
      [
        FC_NO_CLIENT([gtk4], [GTK-4.0 libraries not found])
      ])
  fi
])
