$ set verify
$ cc_opts = "/reentrancy=multithread/name=(as_is,shortened)/prefix=all/float=ieee"
$ link_opts = "/traceback"
$ !
$ ! Put the following logicals in the system startup file
$ !
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGDK.SO LIBGDK
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGTK.SO LIBGTK
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGLIB.SO LIBGLIB
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]VMS_JACKETS.SO VMS_JACKETS
$ assign/system SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGMODULE.SO LIBGMODULE
$ !
$ ! Compile the common code
$ !
$ set def [-.common]
$ lib/create/log libcivcommon.olb
$ cc := cc'cc_opts'/define=("HAVE_UNISTD_H","HAVE_NETINET_IN_H",-
"HAVE_SYS_TIME_H","HAVE_STRERROR","HAVE_USLEEP","DEBUG")
$ CALL COMPILE libcivcommon astring
$ CALL COMPILE libcivcommon capability
$ CALL COMPILE libcivcommon capstr
$ CALL COMPILE libcivcommon city
$ CALL COMPILE libcivcommon diptreaty
$ CALL COMPILE libcivcommon fcintl
$ CALL COMPILE libcivcommon game
$ CALL COMPILE libcivcommon genlist
$ CALL COMPILE libcivcommon government
$ CALL COMPILE libcivcommon hash
$ CALL COMPILE libcivcommon idex
$ CALL COMPILE libcivcommon inputfile
$ CALL COMPILE libcivcommon log
$ CALL COMPILE libcivcommon map
$ CALL COMPILE libcivcommon mem
$ CALL COMPILE libcivcommon nation
$ CALL COMPILE libcivcommon netintf
$ CALL COMPILE libcivcommon packets
$ CALL COMPILE libcivcommon player
$ CALL COMPILE libcivcommon rand
$ CALL COMPILE libcivcommon registry
$ CALL COMPILE libcivcommon sbuffer
$ CALL COMPILE libcivcommon shared
$ CALL COMPILE libcivcommon spaceship
$ CALL COMPILE libcivcommon support
$ CALL COMPILE libcivcommon tech
$ CALL COMPILE libcivcommon timing
$ CALL COMPILE libcivcommon unit
$ CALL COMPILE libcivcommon version
$ CALL COMPILE libcivcommon worklist
$ !
$ ! Now compile the AI part
$ !
$ set def [-.ai]
$ lib/create/log libcivai.olb
$ cc := cc'cc_opts'/define=("HAVE_UNISTD_H","HAVE_NETINET_IN_H","HAVE_SYS_TIME_H","DEBUG")-
/include=([-.COMMON],[-.SERVER])
$ CALL COMPILE libcivai advattitude
$ CALL COMPILE libcivai advdomestic
$ CALL COMPILE libcivai advforeign
$ CALL COMPILE libcivai advisland
$ CALL COMPILE libcivai advleader
$ CALL COMPILE libcivai advmilitary
$ CALL COMPILE libcivai advscience
$ CALL COMPILE libcivai advspace
$ CALL COMPILE libcivai advtrade
$ CALL COMPILE libcivai aicity
$ CALL COMPILE libcivai aihand
$ CALL COMPILE libcivai aitech
$ CALL COMPILE libcivai aitools
$ CALL COMPILE libcivai aiunit
$ !
$ ! Now compile and link the server
$ !
$ set def [-.server]
$!"SOCKET_ZERO_ISNT_STDIN"
$ cc := cc'cc_opts'/define=("HAVE_UNISTD_H","HAVE_NETINET_IN_H","HAVE_SYS_TIME_H","HAVE_ARPA_INET_H",-
"HAVE_SYS_SOCKET_H","HAVE_NETDB_H","DEBUG")/include=([-.COMMON],[-.ai])/nest=primary
$ CC autoattack
$ CC barbarian
$ CC cityhand
$ CC citytools
$ CC cityturn
$ CC civserver
$ CC console
$ CC diplhand
$ CC gamehand
$ CC gamelog
$ CC gotohand
$ CC handchat
$ CC mapgen
$ CC maphand
$ CC meta
$ CC plrhand
$ CC ruleset
$ cc rulesout
$ CC sernet
$ CC settlers
$ CC spacerace
$ CC stdinhand
$ CC unitfunc
$ CC unithand
$ CC unittools
$ link'trace_opts'/exec=civserver autoattack,barbarian,cityhand,citytools,-
cityturn,civserver,console,diplhand,gamehand,gamelog,-
gotohand,handchat,mapgen,maphand,meta,plrhand,ruleset,rulesout,-
sernet,settlers,spacerace,stdinhand,unitfunc,unithand,unittools,-
[-.common]libcivcommon.olb/lib,[-.ai]libcivai.olb/lib
$ delete/nolog autoattack.obj;*,barbarian;*,cityhand;*,citytools;*,-
cityturn;*,civserver;*,console;*,diplhand;*,gamehand;*,gamelog;*,-
gotohand;*,handchat;*,mapgen;*,maphand;*,meta;*,plrhand;*,ruleset;*,rulesout;*,-
sernet;*,settlers;*,spacerace;*,stdinhand;*,unitfunc;*,unithand;*,unittools;*
$ !
$ ! Compile the client library
$ !
$ set def [-.CLIENT.GUI-GTK]
$ define gtk [-.-.-.gtk.gtk]
$ define gdk [-.-.-.gtk.gdk]
$ lib/create/log libguiclient.olb
$ cc := cc'cc_opts'/define=("HAVE_UNISTD_H","HAVE_NETINET_IN_H","HAVE_SYS_TIME_H","HAVE_ARPA_INET_H",-
"HAVE_SYS_SOCKET_H","HAVE_NETDB_H","SOCKET_ZERO_ISNT_STDIN","DEBUG")-
/include=([],[-],[-.include],[-.-.common],[-.-.-.glib],[-.-.-.imlib.gdk_imlib])/nest=primary
$! bash rc2c [-.-.data]freeciv.rc > Freeciv.h
$ CALL COMPILE libguiclient chatline
$ CALL COMPILE libguiclient citydlg
$ CALL COMPILE libguiclient cityrep
$ CALL COMPILE libguiclient colors
$ CALL COMPILE libguiclient connectdlg
$ CALL COMPILE libguiclient dialogs
$ CALL COMPILE libguiclient diplodlg
$ CALL COMPILE libguiclient finddlg
$ CALL COMPILE libguiclient gamedlgs
$ CALL COMPILE libguiclient gotodlg
$ CALL COMPILE libguiclient graphics
$ CALL COMPILE libguiclient gtkpixcomm
$ CALL COMPILE libguiclient gui_MAIN
$ CALL COMPILE libguiclient gui_STUFF
$ CALL COMPILE libguiclient helpdlg
$ CALL COMPILE libguiclient inputdlg
$ CALL COMPILE libguiclient inteldlg
$ CALL COMPILE libguiclient mapctrl
$ CALL COMPILE libguiclient mapview
$ CALL COMPILE libguiclient menu
$ CALL COMPILE libguiclient messagedlg
$ CALL COMPILE libguiclient messagewin
$ CALL COMPILE libguiclient plrdlg
$ CALL COMPILE libguiclient repodlgs
$ CALL COMPILE libguiclient resources
$ CALL COMPILE libguiclient spaceshipdlg
$ CALL COMPILE libguiclient wldlg
$ !
$ ! Compile and link the GTK client
$ !
$ set def [-.-.CLIENT]
$ create config.h
$ deck
#define OPTION_FILE_NAME "sys$login:.civclientrc"
#define FC_CONFIG_H
$ eod
$ cc := cc'cc_opts'/define=("HAVE_UNISTD_H","HAVE_NETINET_IN_H","HAVE_SYS_TIME_H","HAVE_ARPA_INET_H",-
"HAVE_SYS_SOCKET_H","HAVE_NETDB_H","DEBUG","HAVE_CONFIG_H")-
/include=([],[.include],[-.COMMON])/nest=primary
$ CC cityrepdata
$ CC civclient
$ CC climisc
$ CC clinet
$ CC control
$ CC helpdata
$ CC options
$ CC packhand
$ CC tilespec
$ link'link_opts'/execute=civclient.exe cityrepdata,civclient,climisc,clinet,control,helpdata,packhand,options,tilespec,-
[.GUI-GTK]libguiclient.olb/lib,[-.common]libcivcommon.olb/lib,[-.-.imlib.GDK_IMLIB]libimlib.olb/lib,sys$input:/opt
$ deck
SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGDK.SO/share
SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGTK.SO/share
SYS$SYSDEVICE:[VMS$COMMON.MOZILLA]LIBGLIB.SO/share
SYS$SHARE:DECW$XLIBSHR.EXE/SHARE
SYS$SHARE:DECW$XEXTLIBSHR.EXE/share
$ eod
$ delete/nolog cityrepdata.obj;*,civclient;*,climisc;*,clinet;*,control;*,helpdata;*,packhand;*,options;*,tilespec;*
$ set def [-.vms]
$ !
$ ! You can be in the FREECIV directory to execute the client and server or
$ ! you can assign the logical FREECIV_PATH to where the data files are kept.
$ ! For example:
$ !	$ assign/system "/$3$DKA100/TUCKER/FREECIV/DATA" FREECIV_PATH
$ ! Also assign foreign symbols for the CIVSERVER and CIVCLIENT in your login.com
$ ! or place them in your DCL$PATH search list.
$ !
$ set def [-]
$ set noverify
$ write sys$output "Now try running [.SERVER]CIVSERVER"
$ write sys$output "In another window, with your default set to the Freeciv directory"
$ write sys$output "you should be able to run the client [.CLIENT]CIVCLIENT"
$ write sys$output "See the README.txt in the [.VMS] directory for more information."
$ EXIT
$ !
$ ! Subroutine to compile and insert into library
$ !
$ COMPILE : SUBROUTINE
$ cc 'p2'.C
$ library/replace 'p1'.olb 'p2'.obj
$ delete/nolog 'p2'.obj;*
$ ENDSUBROUTINE
$ EXIT
