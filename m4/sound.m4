AC_DEFUN([FC_CHECK_SOUND],[
 AC_ARG_ENABLE(esd,
   [  --disable-esd           Do not try to use Esound],
   USE_SOUND=no, USE_SOUND_ESD=yes)

 AC_ARG_ENABLE(sdl-mixer,
   [  --disable-sdl-mixer     Do not try to use the SDL mixer],
   USE_SOUND=no, USE_SOUND_SDL=yes)

 AC_ARG_ENABLE(alsa,
   [  --disable-alsa          Do not try to use ALSA],
   USE_SOUND=no, USE_SOUND_ALSA=yes)

 AC_ARG_ENABLE(winmm,
   [  --disable-winmm         Do not try to use WinMM for sound],
   USE_SOUND=no, USE_SOUND_WINMM=yes)

 if test "x$USE_SOUND_ESD" = "xyes"; then
  dnl Add esound support to client
  ESD_VERSION=0.0.20
  AM_PATH_ESD($ESD_VERSION, ESD=yes, ESD=no)
  if test "x$ESD" != "xno"; then
     SOUND_CFLAGS="$SOUND_CFLAGS $ESD_CFLAGS"
     SOUND_LIBS="$SOUND_LIBS $ESD_LIBS"
     AC_DEFINE(ESD, 1, [Esound support])
     AC_MSG_CHECKING(building ESOUND support)
     AC_MSG_RESULT(yes)
  fi
 fi

 if test "x$USE_SOUND_SDL" = "xyes"; then
  dnl Add SDL support to client
  SDL_VERSION=1.0.0
  AM_PATH_SDL($SDL_VERSION, SDL=yes, SDL=no)
  if test "x$SDL" != "xno"; then
    AC_CHECK_HEADER(SDL/SDL_mixer.h, SDL_mixer_h=1, SDL_mixer_h=0)
    AC_CHECK_LIB(SDL_mixer, Mix_OpenAudio, SDL_mixer=yes)
    AC_MSG_CHECKING(building SDL_mixer support)
    if test "x$SDL_mixer_h" = "x1"; then
      if test "x$SDL_mixer" = "xyes"; then
        SOUND_CFLAGS="$SOUND_CFLAGS $SDL_CFLAGS"
        SOUND_LIBS="$SOUND_LIBS $SDL_LIBS -lSDL_mixer"
        AC_DEFINE(SDL, 1, [SDL_Mixer support])
        AC_MSG_RESULT(yes)
      else
        AC_MSG_RESULT([no, found header but not library!])
      fi
    else
      AC_MSG_RESULT([no, install SDL_mixer first: http://www.libsdl.org/projects/SDL_mixer/index.html])
      SDL_mixer="xno"
    fi
  fi
 fi

 if test "x$USE_SOUND_ALSA" = "xyes"; then
  dnl Add ALSA support to client
  AM_ALSA_SUPPORT(ALSA=yes, ALSA=no)
  if test "x$ALSA" != "xno"; then
    SOUND_CFLAGS="$SOUND_CFLAGS $ALSA_CFLAGS"
    SOUND_LIBS="$SOUND_LIBS $ALSA_LIB"
    AC_DEFINE(ALSA, 1, [ALSA support])
    AC_MSG_CHECKING(building ALSA support)
    AC_MSG_RESULT(yes)
  fi
 fi

 if test "x$USE_SOUND_WINMM" = "xyes"; then
  dnl Add WinMM sound support to client
  if test x"$MINGW32" = "xyes"; then
    SOUND_LIBS="$SOUND_LIBS -lwinmm"
    AC_DEFINE(WINMM, 1, [Windows MultiMedia sound support])
    WINMM="yes"
  fi
 fi
])
