#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/input.h>
#include <intuition/intuitionbase.h>
#include <amitcp/socketbasetags.h>
#include <sys/errno.h>

#include <clib/alib_protos.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/socket_protos.h>
#include <clib/usergroup_protos.h>

#include <pragmas/exec_sysbase_pragmas.h>
#include <pragmas/dos_pragmas.h>
#include <pragmas/socket_pragmas.h>
#include <pragmas/usergroup_pragmas.h>

#define MUIMASTER_NAME    "muimaster.library"
#define MUIMASTER_VMIN    11

#define SOCKETNAME "bsdsocket.library"
#define SOCKETVERSION 3 /* minimum bsdsocket version to use */
#define USERGROUPVERSION 1

int h_errno;

struct Library *SocketBase;
struct Library *UserGroupBase;
struct Library *GuiGFXBase;
struct Library *MUIMasterBase;
struct Library *DataTypesBase;

/* Opened by the compiler */
IMPORT struct Library *SysBase;
IMPORT struct Library *DOSBase;

/* Stack for the client */
__near LONG __stack = 120000;

/**************************************************************************
 This is the constructor for the client (called befor main()) which does
 open some libraries and some necessary initialations..
**************************************************************************/
int __stdargs _STI_30000_init(void)
{
  if((SocketBase = OpenLibrary(SOCKETNAME,SOCKETVERSION)))
  {
    if((GuiGFXBase = OpenLibrary("guigfx.library",16)))
    {
      if(GuiGFXBase->lib_Version == 16)
      {
      	if(GuiGFXBase->lib_Revision < 2)
      	{
      	  printf("guigfx.library version 16 with revision smaller than 2 found.\nIf the graphics look corrupt, please install a newer version\n");
      	}
      }
      if((DataTypesBase = OpenLibrary("datatypes.library",39)))
      {
        SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), &errno,
                       SBTM_SETVAL(SBTC_HERRNOLONGPTR), &h_errno,
                       SBTM_SETVAL(SBTC_LOGTAGPTR), "civclient",
                       TAG_END);
	
        if((UserGroupBase = OpenLibrary(USERGROUPNAME, USERGROUPVERSION)))
        {
          ug_SetupContextTags("civclient",
                   UGT_INTRMASK, SIGBREAKB_CTRL_C,
                   UGT_ERRNOPTR(sizeof(errno)), &errno,
                   TAG_END);

          if((MUIMasterBase = OpenLibrary(MUIMASTER_NAME,MUIMASTER_VMIN)))
          {
            /* Reserve 0 for stdin */
            Dup2Socket(-1,0);
            return 0;
          } else printf("Couldn't open " MUIMASTER_NAME "!\n");
        } else printf("Couldn't open " USERGROUPNAME "!\n");
      } else printf("Couldn't open datatypes.library version 39\n");
    } else printf("Couldn't open guigfx.library version 16!\n");
  } else printf("Couldn't open " SOCKETNAME "!\nPlease start a TCP/IP stack.\n");
  return 1;
}

/**************************************************************************
 This is the destuctor which clean ups the resources allcated
 by the constructor
**************************************************************************/
void __stdargs _STD_30000_dispose(void)
{
  if(MUIMasterBase) CloseLibrary(MUIMasterBase);
  if(UserGroupBase) CloseLibrary(UserGroupBase);
  if(SocketBase) CloseLibrary(SocketBase);
  if(GuiGFXBase) CloseLibrary(GuiGFXBase);
}

/**************************************************************************
 select() emulation and more. Only correct for
**************************************************************************/
/*
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exeptfds, struct timeval *timeout)
{
  return WaitSelect(nfds, readfds, writefds, exeptfds, timeout, NULL);
}*/

/**************************************************************************
 usleep() function. (because select() is too slow and not needed to be
 emulated for the client). Complete.
**************************************************************************/
void usleep(unsigned long usec)
{
  TimeDelay(0,0,usec);
}

/**************************************************************************
 strerror() which also understand the non ANSI C errors. Complete.
 Note. SAS uses another prototype definition.
**************************************************************************/
const char *strerror(unsigned int error)
{
  ULONG taglist[3];

  taglist[0] = SBTM_GETVAL(SBTC_ERRNOSTRPTR);
  taglist[1] = error;
  taglist[2] = TAG_END;

  SocketBaseTagList((struct TagItem *)taglist);
  return (char *)taglist[1];
}

/**************************************************************************
 select() emulation (only sockets)
**************************************************************************/
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exeptfds, struct timeval *timeout)
{
  return WaitSelect(nfds, readfds, writefds, exeptfds, timeout, NULL);
}

/**************************************************************************
 read() emulation. Only for real bsd sockets (created with socket()).
 (The server doesn't use it for other descriptors)
**************************************************************************/
int read(int fd, char *buf, int len)
{
  if(fd) return recv(fd,buf,len,0);
  return -1;
}

/**************************************************************************
 write() emulation. Only for real bsd sockets (created with socket()).
 (The server doesn't use it for other descriptors)
**************************************************************************/
int write(int fd, char *buf, int len)
{
  if(fd) return send(fd,buf,len,0);
  return 0;
}

/**************************************************************************
 close() emulation. Only for real bsd sockets (created with socket()).
 (The server doesn't use it for other descriptors)
**************************************************************************/
void close(int fd)
{
  if(fd) CloseSocket(fd);
}

