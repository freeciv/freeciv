# Detect Qt headers and libraries and set flag variables

AC_DEFUN([FC_QT],
[
  case "x$1" in
    xqt6|xQt6|x) FC_QT6([0x060000], [6.0])
      FC_QT_CPPFLAGS="$FC_QT6_CPPFLAGS"
      FC_QT_CXXFLAGS="$FC_QT6_CXXFLAGS"
      FC_QT_LIBS="$FC_QT6_LIBS"
      fc_qt_usable="$fc_qt6_usable" ;;
    xqt6x|xQt6x) FC_QT6([0x060800], [6.8])
      FC_QT_CPPFLAGS="$FC_QT6_CPPFLAGS"
      FC_QT_CXXFLAGS="$FC_QT6_CXXFLAGS"
      FC_QT_LIBS="$FC_QT6_LIBS"
      fc_qt_usable="$fc_qt6_usable" ;;
    xqt5|xQt5) FC_QT5
      FC_QT_CPPFLAGS="$FC_QT5_CPPFLAGS"
      FC_QT_CXXFLAGS="$FC_QT5_CXXFLAGS"
      FC_QT_LIBS="$FC_QT5_LIBS"
      fc_qt_usable="$fc_qt5_usable" ;;
  esac
])
