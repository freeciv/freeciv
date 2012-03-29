AC_DEFUN([FC_DEBUG], [
AC_ARG_ENABLE(debug,
[  --enable-debug[[=no/some/yes/checks]] turn on debugging [[default=some]]],
[case "${enableval}" in
  yes)    enable_debug=yes ;;
  some)   enable_debug=some ;;
  checks) enable_debug=checks ;;
  no)     enable_debug=no ;;
  *)      AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
esac], [enable_debug=some])

dnl -g is added by AC_PROG_CC if the compiler understands it

dnl ==========================================================================
dnl debug level == no
if test "x$enable_debug" = "xno"; then
  AC_DEFINE([NDEBUG], [1], [No debugging support at all])
  FC_C_FLAGS([-O3 -fomit-frame-pointer], [], [EXTRA_DEBUG_CFLAGS])
  if test "x$cxx_works" = "xyes" ; then
    FC_CXX_FLAGS([-O3 -fomit-frame-pointer], [], [EXTRA_DEBUG_CXXFLAGS])
  fi
fi

dnl ==========================================================================
dnl debug level >= some
if test "x$enable_debug" = "xsome" -o "x$enable_debug" = "xyes" -o \
        "x$enable_debug" = "xchecks"; then
  FC_C_FLAGS([-Wall -Wpointer-arith -Wcast-align ],
             [], [EXTRA_DEBUG_CFLAGS])
  if test "x$cxx_works" = "xyes" ; then
    FC_CXX_FLAGS([-Wall -Wpointer-arith -Wcast-align ],
                 [], [EXTRA_DEBUG_CXXFLAGS])
  fi
fi

dnl ==========================================================================
dnl debug level >= yes
if test "x$enable_debug" = "xyes" -o "x$enable_debug" = "xchecks"; then
  AC_DEFINE([DEBUG], [1], [Extra debugging support])
  AC_DEFINE([LUA_USE_APICHECK], [1], [Lua Api checks])

  FC_C_FLAGS([-Werror -Wmissing-prototypes -Wmissing-declarations \
              -Wformat -Wformat-security -Wnested-externs -Wno-deprecated-declarations],
             [], [EXTRA_DEBUG_CFLAGS])
  if test "x$cxx_works" = "xyes" ; then
    FC_CXX_FLAGS([-Werror -Wmissing-prototypes -Wmissing-declarations \
                  -Wformat -Wformat-security -Wno-deprecated-declarations],
                 [], [EXTRA_DEBUG_CXXFLAGS])
  fi

  dnl backtrace log callback needs "-rdynamic" in order to work well.
  FC_LD_FLAGS([-rdynamic -Wl,--no-add-needed], [], [EXTRA_DEBUG_LDFLAGS])
fi

dnl ==========================================================================
dnl debug level >= checks
if test "x$enable_debug" = "xchecks"; then
  dnl Add additional flags as stated in ./doc/HACKING. Compiling the
  dnl server is OK but there are problems in a external library (gtk2)
  dnl which prevent the compilation of the client using this extended
  dnl flags (see http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=148766)
  dnl temporary fixing the problem by patching this file to compile freeciv
  dnl with this option set
  FC_C_FLAGS([-Wstrict-prototypes], [], [EXTRA_DEBUG_CFLAGS])
fi

])
