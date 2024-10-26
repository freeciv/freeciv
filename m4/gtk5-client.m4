# Try to configure the GTK-4.x client (gui-gtk-5.0)

# FC_GTK5_CLIENT
# Test for GTK-5.0 libraries needed for gui-gtk-4.x

AC_DEFUN([FC_GTK5_CLIENT],
[
  if test "x$gui_gtk5" = "xyes" ||
     test "x$client" = "xall" || test "x$client" = "xauto" ; then
    PKG_CHECK_MODULES([GTK5], [gtk4 >= 4.14.0],
      [
        GTK5_CFLAGS="$GTK5_CFLAGS -DGDK_VERSION_MIN_REQUIRED=GDK_VERSION_4_8"
        GTK5_CFLAGS="$GTK5_CFLAGS -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_76"
        gui_gtk5=yes
        if test "x$client" = "xauto" ; then
          client=yes
        fi
        gui_gtk5_cflags="$GTK5_CFLAGS"
        gui_gtk5_libs="$GTK5_LIBS"
        if test "x$MINGW" = "xyes"; then
          dnl Required to compile gtk5 on Windows platform
          gui_gtk5_cflags="$gui_gtk5_cflags -mms-bitfields"
          gui_gtk5_ldflags="$gui_gtk5_ldflags $MWINDOWS_FLAG"
        fi
      ],
      [
        FC_NO_CLIENT([gtk4x], [GTK-4.x libraries not found])
      ])
  fi
])
