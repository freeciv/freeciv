AC_DEFUN(FC_DEBUG, [
AC_ARG_ENABLE(debug,
[  --enable-debug[[=no/some/yes]]  turn on debugging [[default=some]]],
[case "${enableval}" in   
  yes) enable_debug=yes ;;
  some) enable_debug=some ;;
  no)  enable_debug=no ;;
  *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
esac], [enable_debug=some])

EXTRA_GCC_DEBUG_CFLAGS=""

dnl -g is added by AC_PROG_CC if the compiler understands it
if test "x$enable_debug" = "xyes"; then
  AC_DEFINE(DEBUG, 1, [Define if you want extra debugging.])
  EXTRA_GCC_DEBUG_CFLAGS="-Werror"
else
  if test "x$enable_debug" = "xno"; then
    AC_DEFINE(NDEBUG, 1, [Define if you want no debug support.])
  fi
fi
])
