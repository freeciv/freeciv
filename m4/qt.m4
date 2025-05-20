# Detect Qt headers and libraries and set flag variables

AC_DEFUN([FC_QT],
[
  case "x$1" in
    xqt6|xQt6|x) FC_QT6([0x060400], [6.4])
      FC_QT_CPPFLAGS="$FC_QT6_CPPFLAGS"
      FC_QT_CXXFLAGS="$FC_QT6_CXXFLAGS"
      FC_QT_LIBS="$FC_QT6_LIBS"
      fc_qt_usable="$fc_qt6_usable" ;;
    xqt6x|xQt6x) FC_QT6([0x060800], [6.8])
      FC_QT_CPPFLAGS="$FC_QT6_CPPFLAGS"
      FC_QT_CXXFLAGS="$FC_QT6_CXXFLAGS"
      FC_QT_LIBS="$FC_QT6_LIBS"
      fc_qt_usable="$fc_qt6_usable" ;;
  esac
])
