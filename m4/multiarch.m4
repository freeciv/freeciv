dnl Put the multiarch tuple of the host architecture in $MULTIARCH_TUPLE if it
dnl can be found.
AC_DEFUN([FC_MULTIARCH_TUPLE], [
  # GCC has the --print-multiarch option
  AS_IF(test "x$GCC" = "xyes", [
    # unless it is an old version
    AS_IF(($CC --print-multiarch >/dev/null 2>/dev/null),
      [MULTIARCH_TUPLE=`$CC --print-multiarch`],
      [MULTIARCH_TUPLE="$host_cpu-$host_os"])],
    [MULTIARCH_TUPLE="$host_cpu-$host_os"])])
