# Try to configure the GTK+-2.0 client (gui-gtk-2.0)

# FC_GTK2_CLIENT
# Test for GTK+-2.0 libraries needed for gui-gtk-2.0

AC_DEFUN([FC_GTK2_CLIENT],
[
  if test "x$gui_gtk2" = "xyes" || test "x$client" = "xauto" ||
     test "x$client" = "xall" ; then
    AM_PATH_GTK_2_0(2.12.0,
      [
        gui_gtk2=yes
        if test "x$client" = "xauto" ; then
          client=yes
        fi
        gui_gtk2_cflags="$GTK2_CFLAGS"
        gui_gtk2_libs="$GTK2_LIBS"
        if test "x$MINGW32" = "xyes"; then
          dnl Required to compile gtk2 on Windows platform
          gui_gtk2_cflags="$gui_gtk2_cflags -mms-bitfields"
          gui_gtk2_ldflags="$gui_gtk2_ldflags -mwindows"
        fi
      ],
      [
        FC_NO_CLIENT([gtk2], [GTK+-2.0 libraries not found])
      ])
  fi
])
