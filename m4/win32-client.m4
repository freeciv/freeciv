# Try to configure the Win32 client (gui-win32)

# FC_WIN32_CLIENT
# Test for Win32 and needed libraries for gui-win32

AC_DEFUN([FC_WIN32_CLIENT],
[
  if test "$client" = "win32" || test "$client" = "yes" ; then
    if test "$MINGW32" = "yes"; then
      AC_CHECK_LIB([z], [gzgets],
        [
          AC_CHECK_HEADER([zlib.h],
            [
              AC_CHECK_LIB([png], [png_read_image],
                [
                  AC_CHECK_HEADER([png.h],
                    [
                      found_client=yes
                      client=win32
                      CLIENT_LIBS="-lwsock32 -lcomctl32  -lpng -lz -mwindows"
                    ],
                    [
                      FC_NO_CLIENT([win32], [libpng-dev is needed])
                    ])
                ],
                [
                  FC_NO_CLIENT([win32], [libpng is needed])
                ], [-lz])
            ],
            [
              FC_NO_CLIENT([win32], [zlib-dev is needed])
            ])
        ],
        [
          FC_NO_CLIENT([win32], [zlib is needed])
        ])
    else
      FC_NO_CLIENT([win32], [mingw32 is needed])
    fi
  fi
])
