/*
**  config.h for Amiga
*/
#ifndef FC_CONFIG_H
#define FC_CONFIG_H

/***************************************************************************/
/* The following lines prevent prototype errors and do some general stuff  */
/* This file is modified by hand and no longer auto generated :-)          */
/***************************************************************************/
#ifdef MIAMI_SDK
void usleep(unsigned long usec);
#else /* AmiTCP */
#include <netdb.h>
char *inet_ntoa(struct in_addr addr);
#endif
int ioctl(int fd, unsigned int request, char *argp);

/* client/auto_none.c uses a name "shutdown" which clashes with the bsd api shutdown() */
#ifndef NOSOCKETSPROTO
#include <proto/socket.h>
#endif

/*#include <proto/usergroup.h>*/
#ifdef __VBCC__
int read(int fd, char *buf, int len);
int write(int fd, char *buf, int len);
void close(int fd);
int stricmp(const char *a, const char *b);
#else
#include <fcntl.h>
#endif

#ifdef __SASC /* this is necessary, else SAS-C does not use the supplied */
#undef read   /* functions in server and client, but it's own stuff in */
#undef write  /* sc.lib, which has no network support. */
#undef close
#endif

/* the prototype for our own vsnprintf function */
#include <stdarg.h>
int vsnprintf(char *, int, const char *, va_list);

#ifdef __VBCC__
#include <stdlib.h>
#define bzero(a,l) memset(a, 0, l);
#endif

#ifndef AMIGA
#define AMIGA
#endif

#define ssize_t int
#define S_ISREG(x) ((x)&S_IFREG)
/***************************************************************************/

/* Define if using alloca.c.  */
/* #undef C_ALLOCA */

/* Define to empty if the keyword does not work.  */
/* #undef const */

/* Define to one of _getb67, GETB67, getb67 for Cray-2 and Cray-YMP systems.
   This function is required for alloca.c support on those systems.  */
/* #undef CRAY_STACKSEG_END */

/* Define if you have alloca, as a function or macro.  */
/* #undef HAVE_ALLOCA */

/* Define if you have <alloca.h> and it should be used (not on Ultrix).  */
/* #undef HAVE_ALLOCA_H */

/* Define if you don't have vprintf but do have _doprnt.  */
/* #undef HAVE_DOPRNT */

/* Define if you have a working `mmap' system call.  */
/* #undef HAVE_MMAP */

/* Define if you have the vprintf function.  */
#define HAVE_VPRINTF

/* Define as __inline if that's what the C compiler calls it.  */
/* #undef inline */

/* Define to `long' if <sys/types.h> doesn't define.  */
/* #undef off_t */

/* Define if you need to in order for stat and other things to work.  */
/* #undef _POSIX_SOURCE */

/* Define as the return type of signal handlers (int or void).  */
#define RETSIGTYPE void

/* Define to `unsigned' if <sys/types.h> doesn't define.  */
/* #undef size_t */

/* If using the C implementation of alloca, define if you know the
   direction of stack growth for your system; otherwise it will be
   automatically deduced at run-time.
 STACK_DIRECTION > 0 => grows toward higher addresses
 STACK_DIRECTION < 0 => grows toward lower addresses
 STACK_DIRECTION = 0 => direction of growth unknown
 */
/* #undef STACK_DIRECTION */

/* Define if you have the ANSI C header files.  */
/* #undef STDC_HEADERS */

/* Define if you can safely include both <sys/time.h> and <time.h>.  */
/* #undef TIME_WITH_SYS_TIME */

/* Define if your <sys/time.h> declares struct tm.  */
/* #undef TM_IN_SYS_TIME */

/* Define if the X Window System is missing or not being used.  */
/* #undef X_DISPLAY_MISSING */

/* #undef MAJOR_VERSION */
/* #undef MINOR_VERSION */
/* #undef PATCH_VERSION */
/* #undef VERSION_LABEL */
/* #undef IS_DEVEL_VERSION */
/* #undef IS_BETA_VERSION */
/* #undef VERSION_STRING */
/* #undef HAVE_LIBICE */
/* #undef HAVE_LIBSM */
/* #undef HAVE_LIBX11 */
/* #undef HAVE_LIBXAW */
/* #undef HAVE_LIBXAW3D */
/* #undef HAVE_LIBXEXT */
/* #undef HAVE_LIBXMU */
/* #undef HAVE_LIBXPM */
/* #undef HAVE_LIBXT */
#define ENABLE_NLS
/* #undef HAVE_CATGETS */
#define HAVE_GETTEXT
/* #undef HAVE_LC_MESSAGES */
/* #undef HAVE_STPCPY */
#define LOCALEDIR "PROGDIR:locale"
#define DEFAULT_DATA_PATH "PROGDIR:data"
/* #undef HAVE_SIGPIPE */
/* #undef XPM_H_NO_X11 */
/* #undef FUNCPROTO */
/* #undef NARROWPROTO */
/* #undef HAVE_LIBREADLINE */
#define ALWAYS_ROOT
/* #undef STRICT_WINDOWS */
/* #undef GENERATING_MAC */
/* #undef HAVE_OPENTRANSPORT */
#define PATH_SEPARATOR ","
/* #undef SOCKET_ZERO_ISNT_STDIN */
#define NONBLOCKING_SOCKETS
/* #undef  HAVE_FCNTL */
#define HAVE_IOCTL
#define OPTION_FILE_NAME "PROGDIR:civclient.options"

/* Define if you have the __argz_count function.  */
/* #undef HAVE___ARGZ_COUNT */

/* Define if you have the __argz_next function.  */
/* #undef HAVE___ARGZ_NEXT */

/* Define if you have the __argz_stringify function.  */
/* #undef HAVE___ARGZ_STRINGIFY */

/* Define if you have the dcgettext function.  */
/* #undef HAVE_DCGETTEXT */

/* Define if you have the fdopen function.  */
/* #undef HAVE_FDOPEN */

/* Define if you have the getcwd function.  */
/* #undef HAVE_GETCWD */

/* Define if you have the gethostname function.  */
/* #undef HAVE_GETHOSTNAME */

/* Define if you have the getpagesize function.  */
/* #undef HAVE_GETPAGESIZE */

/* Define if you have the getpwuid function.  */
/* #undef HAVE_GETPWUID */

/* Define if you have the gettimeofday function.  */
/* #undef HAVE_GETTIMEOFDAY */

/* Define if you have the munmap function.  */
/* #undef HAVE_MUNMAP */

/* Define if you have the putenv function.  */
/* #undef HAVE_PUTENV */

/* Define if you have the select function.  */
#define HAVE_SELECT

/* Define if you have the setenv function.  */
/* #undef HAVE_SETENV */

/* Define if you have the setlocale function.  */
/* #undef HAVE_SETLOCALE */

/* Define if you have the snooze function.  */
/* #undef HAVE_SNOOZE */

/* Define if you have the stpcpy function.  */
/* #undef HAVE_STPCPY */

/* Define if you have the strcasecmp function.  */
/* #undef HAVE_STRCASECMP */

/* Define if you have the strchr function.  */
/* #undef HAVE_STRCHR */

/* Define if you have the strdup function.  */
/* #undef HAVE_STRDUP */

/* Define if you have the strerror function.  */
#define HAVE_STRERROR

/* Define if you have the strlcat function.  */
/* #undef HAVE_STRLCAT */

/* Define if you have the strlcpy function.  */
/* #undef HAVE_STRLCPY */

/* Define if you have the strstr function.  */
/* #undef HAVE_STRSTR */

/* Define if you have the usleep function.  */
#define HAVE_USLEEP

/* Define if you have the vsnprintf function.  */
#define HAVE_VSNPRINTF

/* Define if you have the <argz.h> header file.  */
/* #undef HAVE_ARGZ_H */

/* Define if you have the <arpa/inet.h> header file.  */
#define HAVE_ARPA_INET_H

/* Define if you have the <fcntl.h> header file.  */
/* #define HAVE_FCNTL_H */

/* Define if you have the <limits.h> header file.  */
/* #undef HAVE_LIMITS_H */

/* Define if you have the <locale.h> header file.  */
/* #undef HAVE_LOCALE_H */

/* Define if you have the <malloc.h> header file.  */
/* #undef HAVE_MALLOC_H */

/* Define if you have the <netdb.h> header file.  */
#define HAVE_NETDB_H

/* Define if you have the <netinet/in.h> header file.  */
#define HAVE_NETINET_IN_H

/* Define if you have the <nl_types.h> header file.  */
/* #undef HAVE_NL_TYPES_H */

/* Define if you have the <pwd.h> header file.  */
#ifndef MIAMI_SDK
#define HAVE_PWD_H
#endif

/* Define if you have the <string.h> header file.  */
#define HAVE_STRING_H

/* Define if you have the <sys/ioctl.h> header file.  */
#define HAVE_SYS_IOCTL_H

/* Define if you have the <sys/param.h> header file.  */
/* #undef HAVE_SYS_PARAM_H */

/* Define if you have the <sys/select.h> header file.  */
/* #undef HAVE_SYS_SELECT_H */

/* Define if you have the <sys/signal.h> header file.  */
/* #undef HAVE_SYS_SIGNAL_H */

/* Define if you have the <sys/socket.h> header file.  */
#define HAVE_SYS_SOCKET_H

/* Define if you have the <sys/termio.h> header file.  */
/* #undef HAVE_SYS_TERMIO_H */

/* Define if you have the <sys/time.h> header file.  */
#define HAVE_SYS_TIME_H

/* Define if you have the <sys/types.h> header file.  */
#define HAVE_SYS_TYPES_H

/* Define if you have the <sys/uio.h> header file.  */
/* #define HAVE_SYS_UIO_H */

/* Define if you have the <termios.h> header file.  */
/* #undef HAVE_TERMIOS_H */

/* Define if you have the <unistd.h> header file.  */
#if !defined(__VBCC__) || !defined(MIAMI_SDK)
#define HAVE_UNISTD_H
#endif

/* Define if you have the i library (-li).  */
/* #undef HAVE_LIBI */

/* Define if you have the nls library (-lnls).  */
/* #undef HAVE_LIBNLS */

/* Define if you have the z library (-lz).  */
/* #undef HAVE_LIBZ */

/* Name of package */
#define PACKAGE "freeciv"

/* Version number of package */
/* #undef VERSION */


#endif /* FC_CONFIG_H */
