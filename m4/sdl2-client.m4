# Try to configure the SDL2 client (gui-sdl2)

dnl FC_SDL2_CLIENT
dnl Test for SDL and needed libraries for gui-sdl2

AC_DEFUN([FC_SDL2_CLIENT],
[
  if test "x$gui_sdl2" = "xyes" || test "x$client" = "xall" ||
     test "x$client" = "xauto" ; then
    if test "x$SDL_mixer" = "xsdl" ; then
      if test "x$gui_sdl2" = "xyes"; then
        AC_MSG_ERROR([specified client 'sdl2' not configurable (cannot use SDL_mixer with it))])
      fi
      sdl2_found=no
    else
      AM_PATH_SDL2([2.0.0], [sdl2_found="yes"], [sdl2_found="no"])
    fi
    if test "$sdl2_found" = yes; then
      GUI_sdl2_CFLAGS="$SDL2_CFLAGS"
      GUI_sdl2_LIBS="$SDL2_LIBS"
      FC_SDL2_PROJECT([SDL2_image], [IMG_Load], [SDL/SDL_image.h])
      if test "x$sdl2_h_found" = "xyes" ; then
        AC_CHECK_FT2([2.1.3], [freetype_found="yes"],[freetype_found="no"])
        if test "$freetype_found" = yes; then
          GUI_sdl2_CFLAGS="$GUI_sdl2_CFLAGS $FT2_CFLAGS"
          GUI_sdl2_LIBS="$GUI_sdl2_LIBS $FT2_LIBS"
          found_sdl2_client=yes
        elif test "x$gui_sdl2" = "xyes"; then
          AC_MSG_ERROR([specified client 'sdl2' not configurable (FreeType2 >= 2.1.3 is needed (www.freetype.org))])
        fi    
      elif test "x$gui_sdl2" = "xyes"; then
        if test "x$missing_2_project" = "x" ; then
          missing_2_project="SDL2_image"
        fi
        if test "x$sdl2_lib_found" = "xyes" ; then
          missing_type="-devel"
        else
          missing_type=""
        fi
        AC_MSG_ERROR([specified client 'sdl2' not configurable (${missing_2_project}${missing_type} is needed (www.libsdl.org))])
      fi
    fi

    if test "$found_sdl2_client" = yes; then
      gui_sdl2=yes
      if test "x$client" = "xauto" ; then
        client=yes
      fi

      dnl Check for libiconv (which is usually included in glibc, but may
      dnl be distributed separately).
      AM_ICONV
      AM_LIBCHARSET
      AM_LANGINFO_CODESET
      GUI_sdl2_LIBS="$LIBICONV $GUI_sdl2_LIBS"

      dnl Check for some other libraries - needed under BeOS for instance.
      dnl These should perhaps be checked for in all cases?
      AC_CHECK_LIB(socket, connect, GUI_sdl2_LIBS="-lsocket $GUI_sdl2_LIBS")
      AC_CHECK_LIB(bind, gethostbyaddr, GUI_sdl2_LIBS="-lbind $GUI_sdl2_LIBS")

    elif test "x$gui_sdl2" = "xyes"; then
      AC_MSG_ERROR([specified client 'sdl2' not configurable (SDL2 >= 2.0.0 is needed (www.libsdl.org))])
    fi
  fi
])

AC_DEFUN([FC_SDL2_PROJECT],
[
  ac_save_CPPFLAGS="$CPPFLAGS"
  ac_save_CFLAGS="$CFLAGS"
  ac_save_LIBS="$LIBS"
  CPPFLAGS="$CPPFLAGS $SDL2_CFLAGS"
  CFLAGS="$CFLAGS $SDL2_CFLAGS"
  LIBS="$LIBS $SDL2_LIBS"
  AC_CHECK_LIB([$1], [$2],
               [sdl2_lib_found="yes"], [sdl2_lib_found="no"
sdl2_h_found="no"])
  if test "x$sdl2_lib_found" = "xyes" ; then
    AC_CHECK_HEADER([$3],
                    [sdl2_h_found="yes"
GUI_sdl2_LIBS="${GUI_sdl2_LIBS} -l$1"], [sdl2_h_found="no"])
  fi
  CPPFLAGS="$ac_save_CPPFLAGS"
  CFLAGS="$ac_save_CFLAGS"
  LIBS="$ac_save_LIBS"
])
