# Detect Qt6 headers and libraries and set flag variables

AC_DEFUN([FC_QT6],
[
  if test "x$fc_qt6_usable" = "x" ; then
    FC_QT6_CPPFLAGS="-DQT_DISABLE_DEPRECATED_BEFORE=0x050f00"
    case $host_os in
    darwin*) FC_QT6_DARWIN;;
    *) FC_QT6_GENERIC;;
    esac
  fi
])

AC_DEFUN([FC_QT6_GENERIC],
[
  AC_LANG_PUSH([C++])

  AC_MSG_CHECKING([Qt6 headers])

  AC_ARG_WITH([qt6-includes],
    AS_HELP_STRING([--with-qt6-includes], [path to Qt6 includes]),
              [FC_QT6_COMPILETEST([$withval])],
[POTENTIAL_PATHS="/usr/include /usr/include/qt6 /usr/include/qt"

  # search multiarch paths too (if the multiarch tuple can be found)
  FC_MULTIARCH_TUPLE()
  AS_IF(test "x$MULTIARCH_TUPLE" != "x",
    POTENTIAL_PATHS="$POTENTIAL_PATHS /usr/include/$MULTIARCH_TUPLE/qt6")

  dnl First test without any additional include paths to see if it works already
  FC_QT6_COMPILETEST
  for TEST_PATH in $POTENTIAL_PATHS
  do
    if test "x$qt6_headers" != "xyes" ; then
      FC_QT6_COMPILETEST($TEST_PATH)
    fi
  done])

  if test "x$qt6_headers" = "xyes" ; then
    AC_MSG_RESULT([found])

    AC_MSG_CHECKING([Qt6 libraries])
    AC_ARG_WITH([qt6-libs],
      AS_HELP_STRING([--with-qt6-libs], [path to Qt6 libraries]),
                [FC_QT6_LINKTEST([$withval])],
[POTENTIAL_PATHS="/usr/lib/qt6 /usr/lib/qt"

    # search multiarch paths too (if the multiarch tuple can be found)
    FC_MULTIARCH_TUPLE()
    AS_IF(test "x$MULTIARCH_TUPLE" != "x",
      POTENTIAL_PATHS="$POTENTIAL_PATHS /usr/lib/$MULTIARCH_TUPLE/qt6")

    dnl First test without any additional library paths to see if it works already
    FC_QT6_LINKTEST
    for TEST_PATH in $POTENTIAL_PATHS
    do
      if test "x$qt6_libs" != "xyes" ; then
        FC_QT6_LINKTEST($TEST_PATH)
      fi
    done])
  fi

  if test "x$qt6_libs" = "xyes" ; then
    AC_MSG_RESULT([found])
    AC_MSG_CHECKING([for Qt6 >= 6.0])
    FC_QT6_VERSION_CHECK
  fi

  AC_LANG_POP([C++])
  if test "x$fc_qt6_min_ver" = "xyes" ; then
    AC_MSG_RESULT([ok])
    FC_QT6_VALIDATE_MOC([fc_qt6_usable=true], [fc_qt6_usable=false])
  else
    AC_MSG_RESULT([not found])
    fc_qt6_usable=false
  fi
])

dnl Test if Qt headers are found from given path
AC_DEFUN([FC_QT6_COMPILETEST],
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

dnl Check if the included version of Qt is at least Qt-6.0
dnl Output: fc_qt6_min_ver=yes|no
AC_DEFUN([FC_QT6_VERSION_CHECK],
[
  CPPFLAGS_SAVE="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $FC_QT6_CPPFLAGS"
  CXXFLAGS_SAVE="$CXXFLAGS"
  CXXFLAGS="$CXXFLAGS $FC_QT6_CXXFLAGS"
  LIBS_SAVE="$LIBS"
  LIBS="${LIBS}${LIBSADD}"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
    [[#include <QtCore>]],[[
      #if QT_VERSION < 0x060000
        fail
      #endif
    ]])],
    [fc_qt6_min_ver=yes],
    [fc_qt6_min_ver=no])
  LIBS="$LIBS_SAVE"
  CPPFLAGS="${CPPFLAGS_SAVE}"
  CXXFLAGS="${CXXFLAGS_SAVE}"
])


dnl Test Qt application linking with current flags
AC_DEFUN([FC_QT6_LINKTEST],
[
  if test "x$1" != "x" ; then
    LIBSADD=" -L$1 -lQt6Gui -lQt6Core -lQt6Widgets"
  else
    LIBSADD=" -lQt6Gui -lQt6Core -lQt6Widgets"
  fi

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

dnl If $1 is Qt 6's moc command then $2
AC_DEFUN([FC_QT6_IF_QT6_MOC],
  AS_IF([$1 -v >/dev/null 2>/dev/null &&
         (test "`$1 -v 2<&1 | grep -o 'Qt [[[0-9]]]\+'`" = "Qt 6" ||
          test "`$1 -v 2<&1 | grep -o 'moc [[[0-9]]]\+'`" = "moc 6" ||
          test "`$1 -v 2<&1 | grep -o 'moc-qt[[[0-9]]]\+'`" = "moc-qt6")],
    [$2]))

dnl Set MOCCMD to $1 if it is the Qt 6 "moc".
AC_DEFUN([FC_QT6_TRY_MOC],
  [FC_QT6_IF_QT6_MOC([$1], [MOCCMD="$1"])])


dnl If a usable moc command is found do $1 else do $2
AC_DEFUN([FC_QT6_VALIDATE_MOC], [
  AC_MSG_CHECKING([the Qt 6 moc command])

  dnl Try to find a Qt 6 'moc' if MOCCMD isn't set.
  dnl Test that the supplied MOCCMD is a Qt 6 'moc' if it is set.
  AS_IF([test "x$MOCCMD" = "x"],
    [for mocpath in "moc" "qtchooser -run-tool=moc -qt=6" "moc-qt6" "/usr/lib/qt6/moc" "/usr/lib/qt6/libexec/moc"
    do
      if test "x$MOCCMD" = "x" ; then
        FC_QT6_TRY_MOC([$mocpath])
      fi
    done],
    [FC_QT6_TRY_MOC([$MOCCMD],
      AC_MSG_ERROR(["MOCCMD set to a bad value ($MOCCMD)"]))])

  dnl If no Qt 6 'moc' was found do $2, else do $1
  AS_IF([test "x$MOCCMD" = "x"],
    [AC_MSG_RESULT([not found]); $2],
    [AC_MSG_RESULT([$MOCCMD]); $1])])
