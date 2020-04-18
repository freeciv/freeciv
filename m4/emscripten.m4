# Configure checks for emscripten client

AC_DEFUN([FC_EMSCRIPTEN],
[
  AC_MSG_CHECKING([for emscripten])
  AC_COMPILE_IFELSE(
    [AC_LANG_SOURCE(
      [[#ifndef __EMSCRIPTEN__
         error fail
        #endif
      ]])],
      [AC_MSG_RESULT([yes])
       emscripten=yes],
      [AC_MSG_RESULT([no])
       emscripten=no])
])
