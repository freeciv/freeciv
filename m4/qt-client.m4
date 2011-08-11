# Try to configure the Qt-client (gui-qt)

dnl FC_QT_CLIENT
dnl Test for Qt and needed libraries for gui-qt

AC_DEFUN([FC_QT_CLIENT],
[

dnl FIXME: In its current state Qt-client is never selected automatically.
dnl        When Qt-client matures to usable state enable auto-selection by
dnl        replacing below if-check with one that is currently commented out.
dnl if test "x$gui_qt" = "xyes" || test "x$client" = "xall" ||
dnl   test "x$client" = "xauto" ; then

if test "x$gui_qt" = "xyes" || test "x$client" = "all" ; then

  if test "x$cxx_works" = "xyes" ; then

    AC_LANG_PUSH([C++])

    AC_MSG_CHECKING([Qt headers])

    AC_ARG_WITH([qt-includes],
                [  --with-qt-includes path to Qt includes],
                [FC_QT_CLIENT_COMPILETEST([$withval])],
[POTENTIAL_PATHS="/usr/include /usr/include/qt4"
dnl First test without any additional include paths to see if it works already
FC_QT_CLIENT_COMPILETEST
for TEST_PATH in $POTENTIAL_PATHS
do
  if test "x$qt_headers" != "xyes" ; then
    FC_QT_CLIENT_COMPILETEST($TEST_PATH)
  fi
done])

    if test "x$qt_headers" = "xyes" ; then
      AC_MSG_RESULT([found])

      AC_MSG_CHECKING([Qt libraries])
      AC_ARG_WITH([qt-libs],
                  [  --with-qt-libs path to Qt libraries],
                  [FC_QT_CLIENT_LINKTEST([$withval])],
[POTENTIAL_PATHS="/usr/lib/qt4"
dnl First test without any additional library paths to see if it works already
FC_QT_CLIENT_LINKTEST
for TEST_PATH in $POTENTIAL_PATHS
do
  if test "x$qt_libs" != "xyes" ; then
    FC_QT_CLIENT_LINKTEST($TEST_PATH)
  fi
done])

    fi

    AC_LANG_POP([C++])

    if test "x$qt_libs" = "xyes" ; then
      AC_MSG_RESULT([found])
    else
      AC_MSG_RESULT([not found])
    fi
  else
    AC_MSG_RESULT([not found])
  fi

  if test "x$qt_libs" = xyes ; then
    gui_qt=yes
    if test "x$client" = "xauto" ; then
      client=yes
    fi
  elif test "x$gui_qt" = "xyes" ; then
    AC_MSG_ERROR([selected client 'qt' cannot be built])
  fi
fi
])

dnl Test if Qt headers are found from given path
AC_DEFUN([FC_QT_CLIENT_COMPILETEST],
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
     GUI_qt_CPPFLAGS="${GUI_qt_CPPFLAGS}${CPPFADD}"])
  CPPFLAGS="$CPPFLAGS_SAVE"
])

dnl Test Qt application linking with current flags
AC_DEFUN([FC_QT_CLIENT_LINKTEST],
[
  if test "x$1" != "x" ; then
    LIBSADD=" -L$1 -lQtGui -lQtCore"
  else
    LIBSADD=" -lQtGui -lQtCore"
  fi

  CPPFLAGS_SAVE="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $GUI_qt_CPPFLAGS"
  LIBS_SAVE="$LIBS"
  LIBS="${LIBS}${LIBSADD}"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[QApplication app(0, 0);]])],
[qt_libs=yes
 GUI_qt_LIBS="${GUI_qt_LIBS}${LIBSADD}"])
 LIBS="$LIBS_SAVE"
 CPPFLAGS="$CPPFLAGS_SAVE"
])
