# wm_cflags_check.m4 - Macros to check options for the compiler into CFLAGS
#
# Copyright (c) 2013 Christophe CURIS
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


# WM_CFLAGS_CHECK_FIRST
# ---------------------
#
# For each option provided, check which one is supported and stops when
# found, adding it to CFLAGS. It extends AX_CFLAGS_GCC_OPTION which does
# not checks for fallbacks; as a bonus it uses a shared function to
# produce smaller configure script.
#
# Usage: WM_CFLAGS_CHECK_FIRST([message], [option alt_option...])
#   $1 message: required, message displayed in 'Checking for...'
#   $2 option_list: list of options, tested in given order
#
# The first option that works is added to CFLAGS
AC_DEFUN([WM_CFLAGS_CHECK_FIRST],
[AC_REQUIRE([_WM_SHELLFN_CHKCFLAGS])
m4_define([_wm_optlist], m4_split([$2]))dnl
AS_VAR_PUSHDEF([VAR], [wm_cv_c_check_compopt[]m4_car(_wm_optlist)])dnl
m4_define([_wm_trimmed_optlist], m4_join([ ], _wm_optlist))dnl
AC_CACHE_CHECK([CFLAGS for m4_ifnblank($1,$1,m4_car(_wm_optlist))], VAR,
  [VAR="no, unknown"
   for wm_option in _wm_trimmed_optlist ; do
     AS_IF([wm_fn_c_try_compile_cflag "$wm_option"],
           [VAR="$wm_option" ; break])
   done])
AS_CASE([$VAR],
  [no,*], [],
  [AS_IF([echo " $CFLAGS " | grep " $VAR " 2>&1 > /dev/null],
    [AC_MSG_WARN([option $VAR already present in \$CFLAGS, not appended])],
    [CFLAGS="$CFLAGS $VAR"]) ])
AS_VAR_POPDEF([VAR])dnl
m4_undefine([_wm_optlist])dnl
m4_undefine([_wm_trimmed_optlist])dnl
])


# _WM_SHELLFN_CHKCFLAGS
# ---------------------
# (internal shell function)
#
# Create a shell function that tries compiling a simple program with the
# specified compiler option. Assumes the current compilation language is
# already set to C
AC_DEFUN_ONCE([_WM_SHELLFN_CHKCFLAGS],
[@%:@ wm_fn_c_try_compile_cflag CC_OPTION
@%:@ -----------------------------------
@%:@ Try compiling a function using CC_OPTION in the compiler's options
wm_fn_c_try_compile_cflag ()
{
  wm_save_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS -Werror $[]1"
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([], [])],
    [wm_retval=0],
    [wm_retval=1])
  CFLAGS="$wm_save_CFLAGS"
  AS_SET_STATUS([$wm_retval])
}
])


# WM_CFLAGS_GCC_OPTION_UNUSEDMACROS
# ---------------------------------
#
# Check if the compiler support '-Wunused-macros'. This would be done
# traditionally using AX_CFLAGS_GCC_OPTION(...), but in the present case it
# always fail because the test program defines many macro (all the stuff
# detected so far) so the warning always trigger, the compilation then fail,
# and the option is then marked as never supported.
#
# This macro works around this by using a basic program without any #define
AC_DEFUN([WM_CFLAGS_GCC_OPTION_UNUSEDMACROS],
[AC_CACHE_CHECK([CFLAGS for gcc -Wunused-macros], [wm_cv_c_checks_compopt_Wunused_macros],
  [AC_LANG_COMPILER_REQUIRE()dnl
   AC_LANG_PUSH([C])
   wm_save_CFLAGS="$CFLAGS"
   CFLAGS="$wm_save_CFLAGS -pedantic -Werror -Wunused-macros"
   dnl Similar to AC_LANG_CONFTEST(C), except we do not include the 'confdefs.h'
   dnl directly in the source to avoid an "unused macro" error, but we include
   dnl it in the same way we will include the "config.h", so we get in the same
   dnl condition as WindowMaker's source
   cat <<_ACEOF >conftest.$ac_ext
@%:@include "confdefs.h"

int
main(int argc, char **argv)
{
	/* to avoid failing on -Wunused-parameter */
	(void) argc;
	(void) argv;

	return 0;
}
_ACEOF
   AS_IF([ac_fn_[]_AC_LANG_ABBREV[]_try_compile "$LINENO"],
     [wm_cv_c_checks_compopt_Wunused_macros="-Wunused-macros"],
     [wm_cv_c_checks_compopt_Wunused_macros="no"])
   rm -f core conftest.err conftest.$ac_objext conftest.$ac_ext
   CFLAGS="$wm_save_CFLAGS"
   AC_LANG_POP([C]) ])
# We cannot add this flag now to CFLAGS because it could break all further
# detections done by configure, the flag will be added at the end by the
# macro WM_CFLAGS_GCC_OPTION_POSTPONED
])


# WM_CFLAGS_GCC_OPTION_POSTPONED
# ------------------------------
#
# Some options cannot be added to the compilation command as soon as they have
# been detected because they can break the detection done in autoconf, because
# the template are cannot always be perfect.
# This macro takes care of making them effective, so it should be called just
# before AC_OUTPUT.
AC_DEFUN_ONCE([WM_CFLAGS_GCC_OPTION_POSTPONED],
[# WM_CFLAGS_GCC_OPTION_POSTPONED: add compiler flags at the end of the autoconf flow
AS_IF([test "x$debug" = "xyes"],
  [m4_foreach([arg_name], [[Wunused_macros]],
    [AS_CASE([$wm_cv_c_checks_compopt_[]arg_name],
      [no*], [],
      [AS_IF([echo " $CFLAGS " | grep " $wm_cv_c_checks_compopt_[]arg_name " 2>&1 > /dev/null],
        [AC_MSG_WARN([option $wm_cv_c_checks_compopt_[]arg_name already present in \$CFLAGS, not appended])],
        [CFLAGS="$CFLAGS $wm_cv_c_checks_compopt_[]arg_name"])dnl
      ])
    ])dnl
  ])dnl
])
