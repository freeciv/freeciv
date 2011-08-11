AC_DEFUN([FC_GPROF], [
AC_ARG_ENABLE(gprof,
[  --enable-gprof            turn on profiling [[default=no]]],
[case "${enableval}" in
  yes) enable_gprof=yes ;;
  no)  enable_gprof=no ;;
  *)   AC_MSG_ERROR([bad value ${enableval} for --enable-gprof]) ;;
esac], [enable_gprof=no])

dnl -g is added by AC_PROG_CC if the compiler understands it
if test "x$enable_gprof" = "xyes"; then
  FC_C_FLAGS([-pg], [], [EXTRA_DEBUG_CFLAGS])
fi
])
