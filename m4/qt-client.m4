# Try to configure the Qt-client (gui-qt)

dnl FC_QT_CLIENT
dnl Test for Qt and needed libraries for gui-qt

AC_DEFUN([FC_QT_CLIENT],
[
if test "x$gui_qt" = "xyes" || test "x$client" = "xall" ||
   test "x$client" = "xauto" ; then

  if test "x$cxx_works" = "xyes" ; then

    FC_QT5

    if test x$fc_qt5_usable = xtrue ; then
      gui_qt_cppflags=$FC_QT5_CPPFLAGS
      gui_qt_cxxflags=$FC_QT5_CXXFLAGS
      gui_qt_libs=$FC_QT5_LIBS
    else
      AC_MSG_RESULT([not found])
    fi

  else
    AC_MSG_RESULT([not found])
  fi

  if test "x$fc_qt5_usable" = "xtrue" ; then
    gui_qt=yes
    if test "x$client" = "xauto" ; then
      client=yes
    fi
  elif test "x$gui_qt" = "xyes" ; then
    AC_MSG_ERROR([selected client 'qt' cannot be built])
  fi
fi
])
