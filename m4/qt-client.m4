# Try to configure the QT client (gui-qt)

dnl FC_QT_CLIENT
dnl Test for QT and needed libraries for gui-qt

AC_DEFUN([FC_QT_CLIENT],
[
  if test "x$gui_qt" = "xyes" || test "x$client" = "xall" ||
     test "x$client" = "xauto" ; then

     AC_ARG_WITH([qt-dir],
                 [  --with-qt-includes path to Qt includes],
                 [QTINC=$withval],
                 [QTINC=/usr/include/qt4])

    SAVED_LIBS=${LIBS}
    SAVED_CXXFLAGS=${CXXFLAGS}

    GUI_qt_LIBS="-lQtGui"
    GUI_qt_CXXFLAGS="-I${QTINC} -I${QTINC}/QtGui"

    LIBS="$SAVED_LIBS $GUI_qt_LIBS"
    CXXFLAGS="$SAVED_CXXFLAGS $GUI_qt_CXXFLAGS"

    AC_LANG_PUSH([C++])

    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[QApplication app(0, 0);]])], [qt_ok=yes], [qt_ok=no])

    AC_LANG_POP([C++])

    if test "$qt_ok" = yes ; then
      gui_qt=yes
      if test "x$client" = "xauto" ; then
        client=yes
      fi
    elif test "x$gui_qt" = "xyes" ; then
      AC_MSG_ERROR([specified client 'qt' not configurable])
    fi

    LIBS=${SAVED_LIBS}
    CXXFLAGS=${SAVED_CXXFLAGS}
  fi
])
