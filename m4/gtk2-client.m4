# Try to configure the GTK+-2.0 client (gui-gtk-2.0)

# FC_GTK_CLIENT
# Test for GTK+-2.0 libraries needed for gui-gtk-2.0

AC_DEFUN([FC_GTK2_CLIENT],
[
  if test "$client" = "gtk-2.0" || test "$client" = yes ; then
    AM_PATH_GTK_2_0(2.2.1,
      [
        client="gtk-2.0"
        CLIENT_CFLAGS="$GTK_CFLAGS"
        CLIENT_LIBS="$GTK_LIBS"
        if test x"$MINGW32" = "xyes"; then
          dnl Required to compile gtk2 on Windows platform
          CFLAGS="$CFLAGS -mms-bitfields"
          CLIENT_LDFLAGS="$LDFLAGS -mwindows"
        fi
      ],
      [
        FC_NO_CLIENT([gtk-2.0], [GTK+-2.0 libraries not found])
      ])
  fi
])
