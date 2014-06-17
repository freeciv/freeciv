# Try to configure the GTK+-3.0 client (gui-gtk-3.0)

# FC_GTK3_CLIENT
# Test for GTK+-3.0 libraries needed for gui-gtk-3.0

AC_DEFUN([FC_GTK3_CLIENT],
[
  if test "x$gui_gtk3" = "xyes" || test "x$client" = "xauto" ||
     test "x$client" = "xall" ; then
    AM_PATH_GTK_3_0([3.4.0],
      [
        gui_gtk3=yes
        if test "x$client" = "xauto" ; then
          client=yes
        fi
        gui_gtk3_cflags="$GTK3_CFLAGS"
        gui_gtk3_libs="$GTK3_LIBS"
        if test "x$MINGW32" = "xyes"; then
          dnl Required to compile gtk3 on Windows platform
          gui_gtk3_cflags="$gui_gtk3_cflags -mms-bitfields"
          gui_gtk3_ldflags="$gui_gtk3_ldflags -mwindows"
        fi
      ],
      [
        FC_NO_CLIENT([gtk3], [GTK+-3.0 libraries not found])
      ])
  fi
])
