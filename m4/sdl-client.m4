# Try to configure the SDL client (gui-sdl)

dnl FC_SDL_CLIENT
dnl Test for SDL and needed libraries for gui-sdl

AC_DEFUN(FC_SDL_CLIENT,
[
  if test "$client" = sdl || test "$client" = yes ; then
    AM_PATH_SDL([1.1.4], [sdl_found="yes"], [sdl_found="no"])
    if test "$sdl_found" = yes; then
      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      ac_save_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $SDL_CFLAGS"
      CFLAGS="$CFLAGS $SDL_CFLAGS"
      LIBS="$LIBS $SDL_LIBS"
      AC_CHECK_LIB([SDL_image], [IMG_Load],
                   [sdl_image_found="yes"], [sdl_image_found="no"])
      if test "$sdl_image_found" = "yes"; then
        AC_CHECK_HEADER([SDL/SDL_image.h],
                        [sdl_image_h_found="yes"], [sdl_image_h_found="no"])
    	if test "$sdl_image_h_found" = yes; then
	  AC_CHECK_LIB([SDL_ttf], [TTF_OpenFont],
                       [sdl_ttf_found="yes"], [sdl_ttf_found="no"])
          if test "$sdl_ttf_found" = yes; then
            AC_CHECK_HEADER([SDL/SDL_ttf.h],
                            [sdl_ttf_h_found="yes"], [sdl_ttf_h_found="no"])
	    if test "$sdl_ttf_h_found" = yes; then
	      LIBS=""
	      CLIENT_CFLAGS="$SDL_CFLAGS"
	      CLIENT_LIBS="$SDL_LIBS -lSDL_image -lSDL_ttf"
	      found_client=yes
	    elif test "$client" = "sdl"; then
	      AC_MSG_ERROR([specified client 'sdl' not configurable (SDL_ttf-devel is needed)])
	    fi
          elif test "$client" = "sdl"; then
            AC_MSG_ERROR([specified client 'sdl' not configurable (SDL_ttf is needed)])
          fi
	elif test "$client" = "sdl"; then
	    AC_MSG_ERROR([specified client 'sdl' not configurable (SDL_image-devel is needed)])
	fi
      elif test "$client" = "sdl"; then
        AC_MSG_ERROR([specified client 'sdl' not configurable (SDL_image is needed)])
      fi
      CPPFLAGS="$ac_save_CPPFLAGS"
      CFLAGS="$ac_save_CFLAGS"
      LIBS="$ac_save_LIBS"
    fi

    if test "$found_client" = yes; then
      client=sdl
    elif test "$client" = "sdl"; then
      AC_MSG_ERROR([specified client 'sdl' not configurable (SDL >= 1.1.4 is needed)])
    fi
  fi
])
