# Check for the presence of behavior usually seen with tcc

# Check if value must be returned even after exit()
#
AC_DEFUN([FC_VALUE_AFTER_EXIT],
[
  AC_CACHE_CHECK([needs to return value after exit()], [ac_cv_value_after_exit],
    [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <stdlib.h>
int func(void);
int func(void)
{
  exit(1);
}]], [])], [ac_cv_value_after_exit=no],
      [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <stdlib.h>
int func(void);
int func(void)
{
  exit(1);
  return 0;
}]], [])], [ac_cv_value_after_exit=yes], [ac_cv_value_after_exit=unknown])])])
  if test "x${ac_cv_value_after_exit}" = "xyes" ; then
    AC_DEFINE([FREECIV_RETURN_VALUE_AFTER_EXIT], [1], [[Value must be returned even after exit()]])
  fi
])

# Whether C99-style initializers of a struct can, or even must, be
# within braces.
# Sets macros INIT_BRACE_BEGIN and INIT_BRACE_END accordingly.
#
AC_DEFUN([FC_INITIALIZER_BRACES],
[
AC_CACHE_CHECK([can struct initializers be within braces],
  [ac_cv_c99_initializer_braces],
  [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([],
    [[
struct outer
{
  int v1;
  int v2;
  union
  {
    int v3;
    struct
    {
      int v4;
      int v5;
    } inner;
  };
};

  struct outer init_me = { 1, 2, { .inner = { 3, 4 }}}
]])],
  [ac_cv_c99_initializer_braces=yes], [ac_cv_c99_initializer_braces=no])])
  if test "x${ac_cv_c99_initializer_braces}" = "xyes" ; then
    AC_DEFINE([INIT_BRACE_BEGIN], [{], [Beginning of C99 structure initializer])
    AC_DEFINE([INIT_BRACE_END], [}], [End of C99 structure initializer])
  else
    AC_DEFINE([INIT_BRACE_BEGIN], [], [Beginning of C99 structure initializer])
    AC_DEFINE([INIT_BRACE_END], [], [End of C99 structure initializer])
  fi
])

# Are const var arg parameters supported?
#
AC_DEFUN([FC_CONST_VAR_ARG],
[
AC_CACHE_CHECK([can var arg be a const],
  [ac_cv_const_var_arg],
  [AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <stddef.h>
void var_arg_func(...);]],
  [[const char *param = NULL;
var_arg_func(param);
return 0;
]])],
  [ac_cv_const_var_arg=yes], [ac_cv_const_var_arg=no])])
  if test "x${ac_cv_const_var_arg}" != "xyes" ; then
    AC_DEFINE([FREECIV_NO_CONST_VAR_ARG], [1], [[Var arg cannot be const]])
  fi
])
