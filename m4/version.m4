#
# These macros are used in version.in and they just set
# version information to form understandable for configure.ac and
# configure.in. Other systems define these macros differently
# before reading version.in and thus get version information
# in different form.
# 

AC_DEFUN([FREECIV_VERSION_COMMENT])

AC_DEFUN([FREECIV_VERSION_INFO],
[
 MAJOR_VERSION="$1"
 MINOR_VERSION="$2"
 PATCH_VERSION="$3"
 VERSION_LABEL="$4"
])
AC_DEFUN([FREECIV_DEVEL_VERSION], [IS_DEVEL_VERSION="$1"])
AC_DEFUN([FREECIV_BETA_VERSION], [IS_BETA_VERSION="$1"])
