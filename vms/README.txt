This document includes instructions on how to build Freeciv for OpenVMS
Alpha.  I used Compaq C V6.2-003 on OpenVMS Alpha V7.2, but
other versions of the C compiler and OpenVMS should work.

1.  Make sure TCP/IP is installed correctly and running.

2.  Download and install the latest Mozilla.
    I used the M16 release from
    http://www.openvms.compaq.com/openvms/products/ips/get_mozilla.html
    Follow the release notes there to install it
    Run it, and make sure it at least works.

3.  Get the latest version of GLIB
    I used 1.9.7 from ftp://ftp.gtk.org/pub/gtk/v1.2/
    UnBzip2 it, untar it
    rename the directory to [.glib]
    Copy in glibconfig.h_vms from the bottom of this file
	to [.glib]glibconfig.h
    This directory is only needed for the header files.
    (I hope to get glibconfig.h_vms added to the
     next release of glib)

4.  Get the lastest version of GTK+
    I used 1.2.8 from ftp://ftp.gtk.org/pub/gtk/v1.2/
    UnBzip2 it, untar it
    rename the directory to [.gtk]
    This is only needed for the header files.

5.  Get the latest version of IMLIB
    ftp://ftp.gnome.org/pub/GNOME/stable/sources/imlib/
	I used 1.9.8
    UnBzip2 it, untar it
    rename the directory to [.imlib]
    copy the imlib_vms_build.com from the bottom of this file
	to [.imlib]vms_build.com
    set def [.imlib]
    Execute the vms_build and make sure imlib builds.
    (There will be several compiler warnings but I choose to
     leave them so maybe someday they will be fixed.)
    run [.utils]IMLIB_CONFIG.EXE and make sure it works.
    If you have problems, make sure the logical etc exists or
    hard-code the correct directory in your sys$login:.imrc file.
    (I hope to get theses changes added to the next version
     of IMLIB)

6.  Get the latest version of Freeciv down.
    UnBzip2 it, untar it to the directory [.freeciv]
	$ bzip2 -d freeciv-1_11_0.bz2
	$ tar -xvf FREECIV-1_11_0.;
    make sure Freeciv.h is in [.FREECIV-1_11_0.CLIENT.GUI-GTK]
	(You can rebuild this with GNV SED and rc2c)
    $ set def [.FREECIV-1_11_0.vms]
    $ @vms_build.com
	You will get a few warnings but theses should be OK.
    Read the vms_build.com file or the unix install.; file if you have problems

7.  To run the game you must have the following logicals defined.
    Place theses in the system startup file:

$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGDK.SO LIBGDK
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGTK.SO LIBGTK
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGLIB.SO LIBGLIB
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]VMS_JACKETS.SO VMS_JACKETS
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGMODULE.SO LIBGMODULE

    You must be in the [.FREECIV-1_11_0] directory to run the programs
    or assign the logical FREECIV_PATH to where the data files are being
    kept:  i.e. [.DATA]  For example:

$ assign/system "/$3$DKA100/TUCKER/FREECIV-1_11_0/DATA" FREECIV_PATH
	This logical must be a Unix style path since it searches for :'s
	to delimit search directories.

    You need to setup a foreign symbol for the server or make sure the
    path is part of your DCL$PATH logical.
    if you want to restore a saved game for example:
	    $ civserver:==$dev:[dir...]civserver
	    $ civserver -f savedgame.sav

8.  Todo list:

    Create a standard VMS binary distribution.
    Get changes needed into GLIB and IMLIB source distributions
    Get AUTOCONFIG to work on VMS so that VMS can use the standard
	UNIX make-files.
    Get RedHat Linux .RPM files to work on VMS.
	(Now I'm really dreaming...)

Enjoy the game, and if you have any problems, can improve on things, or
have suggestions for the OpenVMS port of Freeciv, post them to the
freeciv-dev@freeciv.org mailing list and CC me at the e-mail address:

Roger Tucker - roger.tucker@wcom.com
Enjoy...

------------------------------glibconfig.h_vms----------------------------
/* glibconfig.h_vms handcrafted from the glibconfig.h.win32 */

#ifndef GLIBCONFIG_H
#define GLIBCONFIG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <limits.h>
#include <float.h>

#define G_MINFLOAT	FLT_MIN
#define G_MAXFLOAT	FLT_MAX
#define G_MINDOUBLE	DBL_MIN
#define G_MAXDOUBLE	DBL_MAX
#define G_MINSHORT	SHRT_MIN
#define G_MAXSHORT	SHRT_MAX
#define G_MININT	INT_MIN
#define G_MAXINT	INT_MAX
#define G_MINLONG	LONG_MIN
#define G_MAXLONG	LONG_MAX

typedef signed char gint8;
typedef unsigned char guint8;
typedef signed short gint16;
typedef unsigned short guint16;
typedef signed int gint32;
typedef unsigned int guint32;

#define G_HAVE_GINT64 1

typedef __int64 gint64;
typedef unsigned __int64 guint64;

#define G_GINT64_CONSTANT(val)	(val##i64)

#define GPOINTER_TO_INT(p)	((gint)(p))
#define GPOINTER_TO_UINT(p)	((guint)(p))

#define GINT_TO_POINTER(i)	((gpointer)(i))
#define GUINT_TO_POINTER(u)	((gpointer)(u))

#define g_ATEXIT(proc)	(atexit (proc))

#define g_memmove(d,s,n) G_STMT_START { memmove ((d), (s), (n)); } G_STMT_END

#define G_HAVE_ALLOCA 1
#define alloca _alloca

#define GLIB_MAJOR_VERSION 1
#define GLIB_MINOR_VERSION 2
#define GLIB_MICRO_VERSION 7

#ifdef	__cplusplus
#define	G_HAVE_INLINE	1
#else	/* !__cplusplus */
#define G_HAVE___INLINE 1
#endif

#define G_THREADS_ENABLED
/*
* The following program can be used to determine the magic values below:
* #include <stdio.h>
* #include <pthread.h>
* main(int argc, char **argv)
* {
*   int i;
*   pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
*   printf ("sizeof (pthread_mutex_t) = %d\n", sizeof (pthread_mutex_t));
*   printf ("PTHREAD_MUTEX_INITIALIZER = ");
*   for (i = 0; i < sizeof (pthread_mutex_t); i++)
*     printf ("%u, ", ((unsigned char *) &m)[i]);
*   printf ("\n");
*   exit(0);
* }
*/

#define G_THREADS_IMPL_POSIX
typedef struct _GStaticMutex GStaticMutex;
struct _GStaticMutex
{
  struct _GMutex *runtime_mutex;
  union {
    /* The size of the pad array should be sizeof (pthread_mutext_t) */
    /* This value corresponds to the 1999-04-07 version of pthreads-win32 */
    char   pad[40];
    double dummy_double;
    void  *dummy_pointer;
    long   dummy_long;
  } aligned_pad_u;
};
/* This should be NULL followed by the bytes in PTHREAD_MUTEX_INITIALIZER */
#define	G_STATIC_MUTEX_INIT	{ NULL, { {  0, 0, 32, 0, 225, 175, 188, 13, \
					     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
					     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
					     0, 0, 0, 0, 0, 0, 0, 0 } } }
#define	g_static_mutex_get_mutex(mutex) \
  (g_thread_use_default_impl ? ((GMutex*) &((mutex)->aligned_pad_u)) : \
   g_static_mutex_get_mutex_impl (&((mutex)->runtime_mutex)))

#define G_BYTE_ORDER G_LITTLE_ENDIAN

#define GINT16_TO_LE(val)	((gint16) (val))
#define GUINT16_TO_LE(val)	((guint16) (val))
#define GINT16_TO_BE(val)	((gint16) GUINT16_SWAP_LE_BE (val))
#define GUINT16_TO_BE(val)	(GUINT16_SWAP_LE_BE (val))

#define GINT32_TO_LE(val)	((gint32) (val))
#define GUINT32_TO_LE(val)	((guint32) (val))
#define GINT32_TO_BE(val)	((gint32) GUINT32_SWAP_LE_BE (val))
#define GUINT32_TO_BE(val)	(GUINT32_SWAP_LE_BE (val))

#define GINT64_TO_LE(val)	((gint64) (val))
#define GUINT64_TO_LE(val)	((guint64) (val))
#define GINT64_TO_BE(val)	((gint64) GUINT64_SWAP_LE_BE (val))
#define GUINT64_TO_BE(val)	(GUINT64_SWAP_LE_BE (val))

#define GLONG_TO_LE(val)	((glong) GINT32_TO_LE (val))
#define GULONG_TO_LE(val)	((gulong) GUINT32_TO_LE (val))
#define GLONG_TO_BE(val)	((glong) GINT32_TO_BE (val))
#define GULONG_TO_BE(val)	((gulong) GUINT32_TO_BE (val))

#define GINT_TO_LE(val)		((gint) GINT32_TO_LE (val))
#define GUINT_TO_LE(val)	((guint) GUINT32_TO_LE (val))
#define GINT_TO_BE(val)		((gint) GINT32_TO_BE (val))
#define GUINT_TO_BE(val)	((guint) GUINT32_TO_BE (val))

#define GLIB_SYSDEF_POLLIN	= 1
#define GLIB_SYSDEF_POLLOUT	= 4
#define GLIB_SYSDEF_POLLPRI	= 2
#define GLIB_SYSDEF_POLLERR	= 8
#define GLIB_SYSDEF_POLLHUP	= 16
#define GLIB_SYSDEF_POLLNVAL	= 32

#define G_HAVE_WCHAR_H 1
#define G_HAVE_WCTYPE_H 1

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* GLIBCONFIG_H */
------------------------------imlib_vms_build.com----------------------------
$!
$! Command file to build IMLIB for OpenVMS
$!   Requires GLIB header files to be in [-.glib]
$!	(I used GLib version 1.2.7 with a hand configured glibconfig.h)
$!   Requires GTK header files to be in [-.GTK].
$!	(I used GTK+ version 1.2.7.)
$!   Also requires MOZILLA to be installed for
$!	LIBGDK, LIBGTK, LIBGLIB shareable images.
$! There are a few compiler warnings but they should be OK.
$! This was tested with Compaq C V6.2-003 on OpenVMS Alpha V7.2
$!
$ set verify
$ !
$ ! Create the config.h file for VMS builds
$ !
$ create config.h
/* VMS currently doesn't have fork (being added in
   future version) use vfork for now */
#include <unistd.h>
#include <unixio.h>
#define fork vfork
#define lib$get_current_invo_context LIB$GET_CURRENT_INVO_CONTEXT
					/* Should be in lib$routines.h */
#include <lib$routines.h>		/* Needed for LIB$GET_CURRENT_INVO_CONTEXT used by vfork */

/*
** Externals in the X-library are in upper-case
** but we need mixed-case for everything else.
*/

#define XAllocColor XALLOCCOLOR
#define XChangeProperty XCHANGEPROPERTY
#define XCreateColormap XCREATECOLORMAP
#define XCreateGC XCREATEGC
#define XCreateImage XCREATEIMAGE
#define XCreatePixmap XCREATEPIXMAP
#define XCreateWindow XCREATEWINDOW
#define XFlush XFLUSH
#define XFree XFREE
#define XFreeColors XFREECOLORS
#define XFreeGC XFREEGC
#define XGetGeometry XGETGEOMETRY
#define XGetImage XGETIMAGE
#define XGetVisualInfo XGETVISUALINFO
#define XGetWindowAttributes XGETWINDOWATTRIBUTES
#define XGetWindowProperty XGETWINDOWPROPERTY
#define XGrabServer XGRABSERVER
#define XInternAtom XINTERNATOM
#define XParseColor XPARSECOLOR
#define XPutImage XPUTIMAGE
#define XQueryColors XQUERYCOLORS
#define XSetErrorHandler XSETERRORHANDLER
#define XSync XSYNC
#define XTranslateCoordinates XTRANSLATECOORDINATES
#define XUngrabServer XUNGRABSERVER
#define XVisualIDFromVisual XVISUALIDFROMVISUAL

#define XCopyArea XCOPYAREA
#define XFreePixmap XFREEPIXMAP

#define XQueryExtension XQUERYEXTENSION
#define XShmAttach XSHMATTACH
#define XShmCreateImage XSHMCREATEIMAGE
#define XShmCreatePixmap XSHMCREATEPIXMAP
#define XShmDetach XSHMDETACH
#define XShmGetEventBase XSHMGETEVENTBASE
#define XShmGetImage XSHMGETIMAGE
#define XShmPixmapFormat XSHMPIXMAPFORMAT
#define XShmPutImage XSHMPUTIMAGE
#define XShmQueryExtension XSHMQUERYEXTENSION
#define XShmQueryVersion XSHMQUERYVERSION
#define shmat SHMAT
#define shmctl SHMCTL
#define shmdt SHMDT
#define shmget SHMGET

#define PACKAGE "imlib"
#define VERSION "1.9.8"

#define HAVE_IPC_H
#define HAVE_SHM_H
#define HAVE_XSHM_H
#define HAVE_SHM
#undef IPC_RMID_DEFERRED_RELEASE
#undef HAVE_LIBJPEG
#undef HAVE_LIBTIFF
#undef HAVE_LIBGIF
#undef HAVE_LIBPNG

#define HAVE_STDARGS
#undef USE_GMODULE

/* should be shm.h */

#include <sys/types.h>
int shmget(key_t key, size_t size, int shmflg);

/* should be in xshm.h */

#include <X11/Xlib.h>
int XShmQueryExtension (Display *display);
$ eod
$ !
$ ! Now perform the compiles
$ !
$ set def [.gdk_imlib]
$ define sys decw$include
$ define gtk [-.-.gtk.gtk]
$ define gdk [-.-.gtk.gdk]
$ cc :== cc/name=(as_is,shortened)/prefix=all/nest=primary-
	/include=([],[-],[-.-.glib],decw$include:)/floag=ieee
$ cc cache.c
$ cc colors.c
$ cc globals.c    	
$ cc load.c       	
$ cc misc.c       	
$ cc rend.c
$ cc utils.c 
$ cc save.c	
$ cc modules.c
$ lib/create libimlib cache,colors,globals,load,misc,rend,utils,save,modules
$ delete/nolog cache.obj;*
$ delete/nolog colors.obj;*
$ delete/nolog globals.obj;*
$ delete/nolog load.obj;*
$ delete/nolog misc.obj;*
$ delete/nolog utils.obj;*
$ delete/nolog save.obj;*
$ delete/nolog modules.obj;*
$ set def [-.utils]
$ cc :== cc/name=(as_is,shortened)/prefix=all/nest=primary-
	/include=([],[-],[-.gdk_imlib],[-.-.glib])/float=ieee
$ cc IMLIB_CONFIG.C
$ cc ICONS.C
$ cc TESTIMG.C
$ LINK IMLIB_CONFIG,ICONS,TESTIMG,sys$input:/OPT,[-.GDK_IMLIB]libimlib.olb/lib
$ deck
SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGDK.SO/share
SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGTK.SO/share
SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGLIB.SO/share
SYS$SHARE:DECW$XLIBSHR.EXE/SHARE
sys$share:DECW$XEXTLIBSHR.EXE/SHARE
$ eod
$ delete/nolog imlib_config.obj;*
$ delete/nolog icons.obj;*
$ delete/nolog testimg.obj;*
$ set def [-]
$ !
$ ! Theses logicals are also needed to run IMLIB_CONFIG.
$ ! They should be added to the system startup files.
$ !
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGDK.SO LIBGDK
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGTK.SO LIBGTK
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGLIB.SO LIBGLIB
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]VMS_JACKETS.SO VMS_JACKETS
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGMODULE.SO LIBGMODULE
$ !
$ ! Assign the logical where the .pal files are kept
$ ! Put the location of etc in the system startup file
$ !
$ etc_dir = "''F$PARSE("[.config]IM_PALETTE-TINY.PAL",,,"DEVICE")'-
''F$PARSE("[.config]IM_PALETTE-TINY.PAL",,,"DIRECTORY")"
$ assign/system 'etc_dir' ETC
$ write sys$output "Add the following logical to the system startup"
$ write sys$output "assign/system ''etc_dir' etc"
$ !
$ ! Also you will need to copy [.config]imrc.in to sys$login:
$ ! and edit it to change the line:
$ !   PaletteFile @sysconfdir@/im_palette.pal to 
$ !   PaletteFile etc/im_palette.pal
$ ! Here are the commands to do that:
$ delete/nolog sys$login:.imrc;*
$ edit/edt/create/nocommand sys$login:.imrc
$ deck
INCLUDE etc:imrc.in
SUBSTITUTE/@sysconfdir@/etc/ wh
exit
$ eod
$ ! Now run the [.utils]IMLIB_CONFIG.EXE to make sure things work
