# Try to configure the GTK+-1.2 client (gui-gtk)

# FC_GTK_CLIENT
# Test for GTK+-1.2 libraries needed for gui-gtk

AC_DEFUN([FC_GTK_CLIENT],
[
  if test "$client" = gtk || test "$client" = yes ; then
    AM_PATH_GTK(1.2.5,
      [
        AM_PATH_GDK_IMLIB(1.9.2,
          [
            client=gtk
            CLIENT_CFLAGS="$GDK_IMLIB_CFLAGS"
            CLIENT_LIBS="$GDK_IMLIB_LIBS"
          ],
          [
            FC_NO_CLIENT([gtk], [Imlib is needed])
          ])
      ],
      [
        FC_NO_CLIENT([gtk], [GTK libraries not found])
      ])
  fi
])
