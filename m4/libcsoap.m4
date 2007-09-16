# Based on:
# Configure paths for libcsoap
# Heiko Ronsdorf 2006-02-03
# Adapted from:
#
dnl AM_PATH_CSOAP([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl Test for CSOAP, and define CSOAP_CFLAGS and CSOAP_LIBS
dnl
AC_DEFUN([AM_PATH_CSOAP],[

  AC_ARG_WITH(csoap-prefix,
    AC_HELP_STRING(
      [--with-csoap-prefix=PFX],
      [Prefix where libcsoap is installed (optional)]
    ),
    csoap_config_prefix="$withval", csoap_config_prefix=""
  )

  AC_ARG_WITH(csoap-exec-prefix,
    AC_HELP_STRING(
      [--with-csoap-exec-prefix=PFX],
      [Exec prefix where libcsoap is installed (optional)]
    ),
    csoap_config_exec_prefix="$withval", csoap_config_exec_prefix=""
  )

  AC_ARG_ENABLE(csoaptest,
    AC_HELP_STRING(
      [--disable-csoaptest],
      [Do not try to compile and run a test cSOAP program]
    ),,
    enable_csoaptest=yes
  )

  if test x$csoap_config_exec_prefix != x ; then
     csoap_config_args="$csoap_config_args --exec-prefix=$csoap_config_exec_prefix"
     if test x${CSOAP_CONFIG+set} != xset ; then
        CSOAP_CONFIG=$csoap_config_exec_prefix/bin/csoap-config
     fi
  fi
  if test x$csoap_config_prefix != x ; then
     csoap_config_args="$csoap_config_args --prefix=$csoap_config_prefix"
     if test x${CSOAP_CONFIG+set} != xset ; then
        CSOAP_CONFIG=$csoap_config_prefix/bin/csoap-config
     fi
  fi


  AC_PATH_PROG(CSOAP_CONFIG, csoap-config, no)
  min_csoap_version=ifelse([$1], ,1.0.0,[$1])
  AC_MSG_CHECKING(for libcsoap - version >= $min_csoap_version)
  no_csoap=""
  if test "$CSOAP_CONFIG" = "no" ; then
    no_csoap=yes
  else
    CSOAP_CFLAGS=`$CSOAP_CONFIG $csoap_config_args --cflags`
    CSOAP_LIBS=`$CSOAP_CONFIG $csoap_config_args --libs`
    csoap_config_major_version=`$CSOAP_CONFIG $csoap_config_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
    csoap_config_minor_version=`$CSOAP_CONFIG $csoap_config_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
    csoap_config_micro_version=`$CSOAP_CONFIG $csoap_config_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
    if test "x$enable_csoaptest" = "xyes" ; then
      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      CFLAGS="$CFLAGS $CSOAP_CFLAGS"
      LIBS="$CSOAP_LIBS $LIBS"
dnl
dnl Now check if the installed libcsoap is sufficiently new.
dnl (Also sanity checks the results of csoap-config to some extent)
dnl
      rm -f conf.csoaptest
      AC_TRY_RUN([
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libxml/tree.h>
#include <libcsoap/soap-client.h>

int main(int argc, char **argv) {

	int major, minor, micro;
	char *tmp_version;

	system("touch conf.csoaptest");

	/* Capture csoap-config output via autoconf/configure variables */
	/* HP/UX 9 (%@#!) writes to sscanf strings */
	tmp_version = (char *)strdup("$min_csoap_version");
	if (sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {

		printf("%s, bad version string from csoap-config\n", "$min_csoap_version");
		exit(1);
	}
	free(tmp_version);

	/* Test that the library is greater than our minimum version */
	if (($csoap_config_major_version < major) ||
		(($csoap_config_major_version == major) && ($csoap_config_minor_version < minor)) ||
		(($csoap_config_major_version == major) && ($csoap_config_minor_version == minor) && ($csoap_config_micro_version < micro))) {

		printf("\n*** An old version of libcsoap (%d.%d.%d) was found.\n", $csoap_config_major_version, $csoap_config_minor_version, $csoap_config_micro_version);
		printf("*** You need a version of libcsoap newer than %d.%d.%d. The latest version of\n", major, minor, micro);
		printf("*** libcsoap is always available from http://csoap.sf.net.\n\n");
		printf("*** If you have already installed a sufficiently new version, this error\n");
		printf("*** probably means that the wrong copy of the csoap-config shell script is\n");
		printf("*** being found. The easiest way to fix this is to remove the old version\n");
		printf("*** of libcsoap, but you can also set the CSOAP_CONFIG environment to point to the\n");
		printf("*** correct copy of csoap-config. (In this case, you will have to\n");
		printf("*** modify your LD_LIBRARY_PATH enviroment variable, or edit /etc/ld.so.conf\n");
		printf("*** so that the correct libraries are found at run-time))\n");
		return 1;
	}
	else {

		return 0;
	}
}
],, no_csoap=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
  fi

  if test "x$no_csoap" = x ; then
     AC_MSG_RESULT(yes (version $csoap_config_major_version.$csoap_config_minor_version.$csoap_config_micro_version))
     ifelse([$2], , :, [$2])     
  else
     AC_MSG_RESULT(no)
     if test "$CSOAP_CONFIG" = "no" ; then
       echo "*** The csoap-config script installed by LIBCSOAP could not be found"
       echo "*** If libcsoap was installed in PREFIX, make sure PREFIX/bin is in"
       echo "*** your path, or set the CSOAP_CONFIG environment variable to the"
       echo "*** full path to csoap-config."
     else
       if test -f conf.csoaptest ; then
        :
       else
          echo "*** Could not run libcsoap test program, checking why..."
          CFLAGS="$CFLAGS $CSOAP_CFLAGS"
          LIBS="$LIBS $CSOAP_LIBS"
          AC_TRY_LINK([
#include <libcsoap/soap-client.h>
#include <stdio.h>
],      [ soap_client_destroy(); return 0;],
        [ echo "*** The test program compiled, but did not run. This usually means"
          echo "*** that the run-time linker is not finding LIBCSOAP or finding the wrong"
          echo "*** version of LIBCSOAP. If it is not finding LIBCSOAP, you'll need to set your"
          echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
          echo "*** to the installed location  Also, make sure you have run ldconfig if that"
          echo "*** is required on your system"
          echo "***"
          echo "*** If you have an old version installed, it is best to remove it, although"
          echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH" ],
        [ echo "*** The test program failed to compile or link. See the file config.log for the"
          echo "*** exact error that occured. This usually means LIBCSOAP was incorrectly installed"
          echo "*** or that you have moved LIBCSOAP since it was installed. In the latter case, you"
          echo "*** may want to edit the csoap-config script: $CSOAP_CONFIG" ])
          CFLAGS="$ac_save_CFLAGS"
          LIBS="$ac_save_LIBS"
       fi
     fi

     CSOAP_CFLAGS=""
     CSOAP_LIBS=""
     ifelse([$3], , :, [$3])
  fi
  AC_SUBST(CSOAP_CFLAGS)
  AC_SUBST(CSOAP_LIBS)
  rm -f conf.csoaptest
])
