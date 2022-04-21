# Detect Qt6 headers and libraries and set flag variables for Mac OS X 10.10+

AC_DEFUN([FC_QT6_DARWIN],
[
  AC_ARG_WITH([qt6-framework],
    AS_HELP_STRING([--with-qt6-framework], [path to root of Qt6 framework (MacOS, autodetected if wasn't specified)]),
    [qt6_path="${withval}"])

  AC_ARG_WITH([qt6_framework_bin],
    AS_HELP_STRING([--with-qt6-framework-bin], [path to binares of Qt6 framework (MacOS X, autodetected if wasn't specified)]))

  AC_CHECK_PROG([QTPATHS], [qtpaths], [qtpaths], [no])

  if test "x$qt6_path" != "x" || test "x$QTPATHS" != "xno" ; then

    AC_MSG_CHECKING([Qt6 framework])

    if test "x$qt6_framework_bin" = "x" ; then
      if test "x$QTPATHS" != "xno" ; then
        qt6_framework_bin="$($QTPATHS --binaries-dir)"
      fi
    fi
    if test "x$qt6_path" = "x" ; then
      qt6_path="$($QTPATHS --install-prefix)"
    fi

    if test "x$qt6_path" != "x" ; then
      AC_LANG_PUSH([C++])
      FC_QT6_DARWIN_COMPILETEST([$qt6_path])
      if test "x$qt6_headers" = "xyes" ; then
        FC_QT6_DARWIN_LINKTEST([$qt6_path])
      else
        fc_qt6_usable=false
      fi
      AC_LANG_POP([C++])

      if test "x$qt6_libs" = "xyes" ; then
        if test "x$qt6_framework_bin" != "x" ; then
          AS_IF([test "x$MOCCMD" = "x"], [MOCCMD="$qt6_framework_bin/moc"])
        fi
        AS_IF([test -x $MOCCMD], [fc_qt6_usable=true], [fc_qt6_usable=false])
      else
        fc_qt6_usable=false
      fi
    fi

    if test "x$fc_qt6_usable" = "xtrue" ; then
      AC_MSG_RESULT([found])
    else
      AC_MSG_RESULT([not found])
    fi
  fi
])

dnl Test if Qt6 headers are found from given path
AC_DEFUN([FC_QT6_DARWIN_COMPILETEST],
[
  CPPFADD=" -DQT_NO_DEBUG -DQT_WIDGETS_LIB -DQT_GUI_LIB -DQT_CORE_LIB -I$1/lib/QtWidgets.framework/Versions/Current/Headers -I$1/lib/QtGui.framework/Versions/Current/Headers -I$1/lib/QtCore.framework/Versions/Current/Headers -I. -I$1/mkspecs/macx-clang -F$1/lib "

  CPPFLAGS_SAVE="$CPPFLAGS"
  CPPFLAGS="${CPPFLAGS}${CPPFADD}"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[int a; QApplication app(a, 0);]])],
    [qt6_headers=yes
     FC_QT6_CPPFLAGS="${FC_QT6_CPPFLAGS}${CPPFADD}"],
    [CXXFLAGS_SAVE="${CXXFLAGS}"
     CXXFLAGS="${CXXFLAGS} -fPIC"
     AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[int a; QApplication app(a, 0);]])],
      [qt6_headers=yes
       FC_QT6_CPPFLAGS="${FC_QT6_CPPFLAGS}${CPPFADD}"
       FC_QT6_CXXFLAGS="${FC_QT6_CXXFLAGS} -fPIC"])
     CXXFLAGS="${CXXFLAGS_SAVE}"])

  CPPFLAGS="$CPPFLAGS_SAVE"
])

dnl Test Qt application linking with current flags
AC_DEFUN([FC_QT6_DARWIN_LINKTEST],
[
  LIBSADD=" -F$1/lib -framework QtWidgets -framework QtGui -framework QtCore -framework DiskArbitration -framework IOKit -framework OpenGL -framework AGL"

  CPPFLAGS_SAVE="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $FC_QT6_CPPFLAGS"
  CXXFLAGS_SAVE="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS $FC_QT6_CXXFLAGS"
  LIBS_SAVE="$LIBS"
  LIBS="${LIBS}${LIBSADD}"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <QApplication>]],
[[int a; QApplication app(a, 0);]])],
[qt6_libs=yes
 FC_QT6_LIBS="${FC_QT6_LIBS}${LIBSADD}"])
 LIBS="$LIBS_SAVE"
 CPPFLAGS="${CPPFLAGS_SAVE}"
 CXXFLAGS="${CXXFLAGS_SAVE}"
])
