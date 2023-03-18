# Check for the presence of C++20 features.

# Check for C++20 capture 'this' by value
#
AC_DEFUN([FC_CXX20_CAPTURE_THIS],
[
  if test "x$cxx_works" = "xyes" ; then
    AC_CACHE_CHECK([for C++20 capture this], [ac_cv_cxx20_capture_this],
      [AC_LANG_PUSH([C++])
       _CXXFLAGS="$CXXFLAGS"
       CXXFLAGS="$CXXFLAGS $WERROR_CXX_TEST_FLAGS"
       AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[class me {
void top(); };
void me::top() { [=, this]() {}; };
]])],
[ac_cv_cxx20_capture_this=yes], [ac_cv_cxx20_capture_this=no])
       CXXFLAGS="$_CXXFLAGS"
       AC_LANG_POP([C++])])
    if test "x${ac_cv_cxx20_capture_this}" = "xyes" ; then
      AC_DEFINE([FREECIV_HAVE_CXX20_CAPTURE_THIS], [1], [C++20 capture 'this' supported])
    fi
  fi
])
