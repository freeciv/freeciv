# Detect Qt headers and libraries and set flag variables

AC_DEFUN([FC_QT],
[
  AC_LANG_PUSH([C++])

  AC_MSG_CHECKING([Qt headers])

  AC_ARG_WITH([qt-includes],
              [  --with-qt-includes path to Qt includes],
              [FC_QT_COMPILETEST([$withval])],
[POTENTIAL_PATHS="/usr/include /usr/include/qt4"
dnl First test without any additional include paths to see if it works already
FC_QT_COMPILETEST
for TEST_PATH in $POTENTIAL_PATHS
do
  if test "x$qt_headers" != "xyes" ; then
    FC_QT_COMPILETEST($TEST_PATH)
  fi
done])

  if test "x$qt_headers" = "xyes" ; then
    AC_MSG_RESULT([found])

    AC_MSG_CHECKING([Qt libraries])
    AC_ARG_WITH([qt-libs],
                [  --with-qt-libs path to Qt libraries],
                [FC_QT_LINKTEST([$withval])],
[POTENTIAL_PATHS="/usr/lib/qt4"
dnl First test without any additional library paths to see if it works already
FC_QT_LINKTEST
for TEST_PATH in $POTENTIAL_PATHS
do
  if test "x$qt_libs" != "xyes" ; then
    FC_QT_LINKTEST($TEST_PATH)
  fi
done])

  fi

  AC_LANG_POP([C++])

  if test "x$qt_libs" = "xyes" ; then
    AC_MSG_RESULT([found])
    fc_qt_usable=true
  else
    AC_MSG_RESULT([not found])
    fc_qt_usable=false
  fi
])

dnl Test if Qt headers are found from given path
AC_DEFUN([FC_QT_COMPILETEST],
[
  if test "x$1" != "x" ; then
    CPPFADD=" -I$1 -I$1/QtCore -I$1/QtGui"
  else
    CPPFADD=""
  fi

  CPPFLAGS_SAVE="$CPPFLAGS"
  CPPFLAGS="${CPPFLAGS}${CPPFADD}"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[QApplication app(0, 0);]])],
    [qt_headers=yes
     FC_QT_CPPFLAGS="${FC_QT_CPPFLAGS}${CPPFADD}"])
  CPPFLAGS="$CPPFLAGS_SAVE"
])

dnl Test Qt application linking with current flags
AC_DEFUN([FC_QT_LINKTEST],
[
  if test "x$1" != "x" ; then
    LIBSADD=" -L$1 -lQtGui -lQtCore"
  else
    LIBSADD=" -lQtGui -lQtCore"
  fi

  CPPFLAGS_SAVE="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $FC_QT_CPPFLAGS"
  LIBS_SAVE="$LIBS"
  LIBS="${LIBS}${LIBSADD}"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[QApplication app(0, 0);]])],
[qt_libs=yes
 FC_QT_LIBS="${FC_QT_LIBS}${LIBSADD}"])
 LIBS="$LIBS_SAVE"
 CPPFLAGS="$CPPFLAGS_SAVE"
])
