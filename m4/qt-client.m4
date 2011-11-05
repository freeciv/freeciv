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

    FC_QT

    if test x$fc_qt_usable = xtrue ; then
      GUI_qt_CPPFLAGS=$FC_QT_CPPFLAGS
      GUI_qt_LIBS=$FC_QT_LIBS
    fi

  else
    AC_MSG_RESULT([not found])
  fi

  if test "x$fc_qt_usable" = "xtrue" ; then
    gui_qt=yes
    if test "x$client" = "xauto" ; then
      client=yes
    fi
  elif test "x$gui_qt" = "xyes" ; then
    AC_MSG_ERROR([selected client 'qt' cannot be built])
  fi
fi
])
