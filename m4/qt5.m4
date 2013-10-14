# Detect Qt5 headers and libraries and set flag variables

AC_DEFUN([FC_QT5],
[
  AC_LANG_PUSH([C++])

  AC_MSG_CHECKING([Qt5 headers])

  AC_ARG_WITH([qt5-includes],
    AS_HELP_STRING([--with-qt5-includes], [path to Qt5 includes]),
              [FC_QT5_COMPILETEST([$withval])],
[POTENTIAL_PATHS="/usr/include /usr/include/qt5"
dnl First test without any additional include paths to see if it works already
FC_QT_COMPILETEST
for TEST_PATH in $POTENTIAL_PATHS
do
  if test "x$qt5_headers" != "xyes" ; then
    FC_QT5_COMPILETEST($TEST_PATH)
  fi
done])

  if test "x$qt5_headers" = "xyes" ; then
    AC_MSG_RESULT([found])

    AC_MSG_CHECKING([Qt5 libraries])
    AC_ARG_WITH([qt5-libs],
      AS_HELP_STRING([--with-qt5-libs], [path to Qt5 libraries]),
                [FC_QT5_LINKTEST([$withval])],
[POTENTIAL_PATHS="/usr/lib/qt5"
dnl First test without any additional library paths to see if it works already
FC_QT5_LINKTEST
for TEST_PATH in $POTENTIAL_PATHS
do
  if test "x$qt5_libs" != "xyes" ; then
    FC_QT5_LINKTEST($TEST_PATH)
  fi
done])

  fi

  AC_LANG_POP([C++])

  if test "x$qt5_libs" = "xyes" ; then
    AC_MSG_RESULT([found])
    fc_qt5_usable=true
  else
    AC_MSG_RESULT([not found])
    fc_qt5_usable=false
  fi
])

dnl Test if Qt headers are found from given path
AC_DEFUN([FC_QT5_COMPILETEST],
[
  if test "x$1" != "x" ; then
    CPPFADD=" -I$1 -I$1/QtCore -I$1/QtGui -I$1/QtWidgets"
  else
    CPPFADD=""
  fi

  CPPFLAGS_SAVE="$CPPFLAGS"
  CPPFLAGS="${CPPFLAGS}${CPPFADD}"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[int a; QApplication app(a, 0);]])],
    [qt5_headers=yes
     FC_QT5_CPPFLAGS="${FC_QT5_CPPFLAGS}${CPPFADD}"],
    [CXXFLAGS_SAVE="${CXXFLAGS}"
     CXXFLAGS="${CXXFLAGS} -fPIC"
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[int a; QApplication app(a, 0);]])],
      [qt5_headers=yes
       FC_QT5_CPPFLAGS="${FC_QT5_CPPFLAGS}${CPPFADD}"
       FC_QT5_CXXFLAGS="${FC_QT5_CXXFLAGS} -fPIC"])
     CXXFLAGS="${CXXFLAGS_SAVE}"])

  CPPFLAGS="$CPPFLAGS_SAVE"
])

dnl Test Qt application linking with current flags
AC_DEFUN([FC_QT5_LINKTEST],
[
  if test "x$1" != "x" ; then
    LIBSADD=" -L$1 -lQt5Gui -lQt5Core -lQt5Widgets"
  else
    LIBSADD=" -lQt5Gui -lQt5Core -lQt5Widgets"
  fi

  CPPFLAGS_SAVE="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $FC_QT5_CPPFLAGS"
  CXXFLAGS_SAVE="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS $FC_QT5_CXXFLAGS"
  LIBS_SAVE="$LIBS"
  LIBS="${LIBS}${LIBSADD}"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[int a; QApplication app(a, 0);]])],
[qt5_libs=yes
 FC_QT5_LIBS="${FC_QT5_LIBS}${LIBSADD}"])
 LIBS="$LIBS_SAVE"
 CPPFLAGS="${CPPFLAGS_SAVE}"
 CXXFLAGS="${CXXFLAGS_SAVE}"
])
