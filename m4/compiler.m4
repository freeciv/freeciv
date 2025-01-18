# Macros to check compiler options
#

# Helper function that adds flags (words) to variable listing them.
# Makes sure there is no extra spaces in any situation
#
# $1 - Name of the target variable
# $2 - Flags to add
#
AC_DEFUN([FC_ADD_WORDS_TO_VAR],
[
old_value="`eval echo '$'$1`"
if test "x$old_value" = "x" ; then
  $1="$2"
elif test "x$2" != "x" ; then
  $1="$old_value $2"
fi
])

# Check if compiler supports given commandline parameter in language specific
# variable. If it does, it will be concatenated to variable. If several
# parameters are given, they are tested, and added to target variable,
# one at a time.
#
# $1 - Language
# $2 - Language specific variable
# $3 - Parameters to test
# $4 - Additional parameters
# $5 - Variable where to add
#

AC_DEFUN([FC_COMPILER_FLAGS],
[
AC_LANG_PUSH([$1])

flags_save="`eval echo '$'$2`"
accepted_flags=""
existing_flags="`eval echo '$'$5`"

for flag in $3
do
  dnl We need -Werror to test any flags (it can't be tested itself)
  dnl Without it, illegal flag will not give an error for us to detect
  $2="-Werror $flags_save $existing_flags $accepted_flags $flag $4"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([int a;])],
                    [FC_ADD_WORDS_TO_VAR([accepted_flags], [$flag])])
done
FC_ADD_WORDS_TO_VAR([$5], [$accepted_flags])

$2="$flags_save"

AC_LANG_POP([$1])
])

# Commandline flag tests for C and C++
#
#
# $1 - Parameters to test
# $2 - Additional parameters
# $3 - Variable where to add

AC_DEFUN([FC_C_FLAGS],
[
FC_COMPILER_FLAGS([C], [CFLAGS], [$1], [$2], [$3])
])


AC_DEFUN([FC_CXX_FLAGS],
[
FC_COMPILER_FLAGS([C++], [CXXFLAGS], [$1], [$2], [$3])
])

# Commandline flag tests for linker
#
#
# $1 - Parameters to test
# $2 - Additional parameters
# $3 - Variable where to add
AC_DEFUN([FC_LD_FLAGS],
[
flags_save=$LDFLAGS
accepted_flags=""
existing_flags="`eval echo '$'$3`"

for flag in $1
do
  LDFLAGS="$flags_save $existing_flags $accepted_flags $flag $2"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([], [int a;])],
                 [FC_ADD_WORDS_TO_VAR([accepted_flags], [$flag])])
done
FC_ADD_WORDS_TO_VAR([$3], [$accepted_flags])

LDFLAGS="$flags_save"
])

# Does current C++ compiler work.
# Sets variable cxx_works accordingly.
AC_DEFUN([FC_WORKING_CXX],
[
AX_CXX_COMPILE_STDCXX([17], [], [optional])

AC_MSG_CHECKING([whether C++ compiler works])

AC_LANG_PUSH([C++])

AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])],
[
AC_MSG_RESULT([yes])
cxx_works=yes],
[
AC_MSG_RESULT([not])
cxx_works=no])

AC_LANG_POP([C++])

if test "x$HAVE_CXX17" = "x" ; then
  dnl Qt6 requires C++17.
  AC_MSG_WARN([The C++ compiler doesn't support C++17])
  cxx_works=no
fi
])

# Test suitability of a single printf() format specifier for
# size_t variables. Helper for FC_SIZE_T_FORMAT
#
# $1 - Format string
AC_DEFUN([_FC_SIZE_T_FORMAT_TEST],
[
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <stdio.h>
#if defined(__GNUC__)
void fr(const char *form, ...)
  __attribute__((__format__(__printf__, 1, 2)));
#else
#define fr(_a_,_b_) printf(_a_,_b_)
#endif
]],
[[
size_t var = 0;
fr("$1", var);]])], [SIZE_T_PRINTF="$1"])
])

# Find out proper printf() format specifier for size_t variables
AC_DEFUN([FC_SIZE_T_FORMAT],
[
  AC_MSG_CHECKING([format specifier for size_t])

  for fmt in "%zu" "%ld" "%lld" "%I64d" "%I32d"
  do
    if test "x$SIZE_T_PRINTF" = "x" ; then
      _FC_SIZE_T_FORMAT_TEST([${fmt}])
    fi
  done

  if test "x$SIZE_T_PRINTF" = "x" ; then
    AC_MSG_ERROR([Cannot find correct printf format specifier for size_t])
  fi

  AC_DEFINE_UNQUOTED([SIZE_T_PRINTF], ["$SIZE_T_PRINTF"], [Format specifier for size_t])

  AC_MSG_RESULT([$SIZE_T_PRINTF])
])

# Check for __builtin_unreachable()
AC_DEFUN([FC_BUILTIN_UNREACHABLE],
[
  AC_MSG_CHECKING([__builtin_unreachable()])

  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ ]],
[[__builtin_unreachable();]])],[
  AC_DEFINE([FREECIV_HAVE_UNREACHABLE], [1], [__builtin_unreachable() available])
  AC_MSG_RESULT([yes])], [AC_MSG_RESULT([no])])
])
