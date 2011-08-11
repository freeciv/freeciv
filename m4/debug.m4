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
if test "x$enable_debug" = "xyes" -o "x$enable_debug" = "xchecks"; then
  AC_DEFINE([DEBUG], [1], [Extra debugging support])
  FC_C_FLAGS([-Werror], [], [EXTRA_DEBUG_CFLAGS])

  if test "x$enable_debug" = "xchecks"; then
    dnl Add additional flags as stated in ./doc/CodingStyle. Compiling the
    dnl server is OK but there are problems in a external library (gtk2)
    dnl which prevent the compilation of the client using this extended
    dnl flags (see http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=148766)
    dnl temporary fixing the problem by patching this file to compile freeciv
    dnl with this option set
    FC_C_FLAGS([-Werror -Wall -Wpointer-arith -Wcast-align ], [],
               [EXTRA_DEBUG_CFLAGS])
    FC_C_FLAGS([-Wmissing-prototypes -Wmissing-declarations], [],
               [EXTRA_DEBUG_CFLAGS])
    FC_C_FLAGS([-Wstrict-prototypes -Wnested-externs -Wl,--no-add-needed], [],
               [EXTRA_DEBUG_CFLAGS])
  fi
else
  if test "x$enable_debug" = "xno"; then
    AC_DEFINE([NDEBUG], [1], [No debugging support at all])
    FC_C_FLAGS([-O3 -fomit-frame-pointer], [], [EXTRA_DEBUG_CFLAGS])
  fi
fi
])
