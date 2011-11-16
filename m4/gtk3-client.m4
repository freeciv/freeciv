# Try to configure the GTK+-3.0 client (gui-gtk-3.0)

# FC_GTK3_CLIENT
# Test for GTK+-3.0 libraries needed for gui-gtk-3.0

AC_DEFUN([FC_GTK3_CLIENT],
[
  if test "x$gui_gtk3" = "xyes" || test "x$client" = "xauto" ||
     test "x$client" = "xall" ; then
    AM_PATH_GTK_3_0([3.0.0],
      [
        gui_gtk3=yes
        if test "x$client" = "xauto" ; then
          client=yes
        fi
        GUI_gtk3_CFLAGS="$GTK3_CFLAGS"
        GUI_gtk3_LIBS="$GTK3_LIBS"
        if test "x$MINGW32" = "xyes"; then
          dnl Required to compile gtk3 on Windows platform
          GUI_gtk3_CFLAGS="$GUI_gtk3_CFLAGS -mms-bitfields"
          GUI_gtk3_LDFLAGS="$GUI_gtk3_LDFLAGS -mwindows"
        fi
      ],
      [
        FC_NO_CLIENT([gtk3], [GTK+-3.0 libraries not found])
      ])
  fi
])
