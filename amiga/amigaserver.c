#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/input.h>
#include <intuition/intuitionbase.h>
#include <rexx/storage.h>

#include <clib/alib_protos.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

/* 3rd patry stuff */

#define _TIME_T
#include <amitcp/socketbasetags.h>
#include <libraries/usergroup.h>
#include <proto/socket.h>
#include <proto/usergroup.h>

/* compiler stuff */

#ifdef __VBCC__
#define constructor_failed exit(20)
#define constructor_ok return
#else
#define constructor_failed return 1
#define constructor_ok return 0
#endif

#ifdef __SASC
#include <ios1.h>
#endif

/* The ARexx Port stuff - currently not really used */
STATIC struct MsgPort *arexx_port;
STATIC ULONG arexx_added;

#define STDIN_BUF_SIZE 256

STATIC struct MsgPort *stdin_port;
STATIC STRPTR stdin_buffer;
STATIC char stdin_buffer_copy[STDIN_BUF_SIZE]; /* copy of the stdin_buffer */
STATIC int stdin_buffer_copy_len;
STATIC struct DosPacket *pkt;
STATIC int pkt_sent;
STATIC struct FileHandle *stdin_handle;

#define SOCKETNAME "bsdsocket.library"
#define SOCKETVERSION 3     /* minimum bsdsocket version to use */
#define USERGROUPVERSION 1

int h_errno;

struct Library *SocketBase;
struct Library *UserGroupBase;

/* Opened by the compiler */
IMPORT struct ExecBase *SysBase;
IMPORT struct DosLibrary *DOSBase;
IMPORT struct IntuitionBase *IntuitionBase;

/* Stack for the server */
__near LONG __stack = 150000;

/**************************************************************************
 ...
**************************************************************************/
static void send_stdin_write_packet(void)
{
  if (!pkt_sent)
  {
    pkt->dp_Type = ACTION_READ;
    pkt->dp_Arg1 = stdin_handle->fh_Arg1;
    pkt->dp_Arg2 = (LONG)stdin_buffer;
    pkt->dp_Arg3 = STDIN_BUF_SIZE;
    SendPkt(pkt, stdin_handle->fh_Type, stdin_port);

    pkt_sent = TRUE;
  }
}

/**************************************************************************
 This is the constructor for the server (called befor main()) which does
 some necessary initialations so Freeciv needn't to be compiled with GNU C
**************************************************************************/
#ifdef __VBCC__
void _INIT_3_amigaserver(void)
#else
int __stdargs _STI_30000_init(void)
#endif
{
  /* create the ARexx Port, which actually is only a dummy port */
  struct MsgPort *old_port;
  if(!(arexx_port = CreateMsgPort()))
    constructor_failed;

  Forbid();
  if(old_port = FindPort("CIVSERVER"))
  {
    Permit();
    DeleteMsgPort(old_port);
    printf("Civserver is already running!\n");
    constructor_failed;
  }
  arexx_port->mp_Node.ln_Name = "CIVSERVER";
  arexx_added = TRUE;
  AddPort(arexx_port);
  Permit();

  if((stdin_port = CreateMsgPort()))
  {
    if((pkt = AllocDosObject(DOS_STDPKT, NULL)))
    {
      if((stdin_buffer = AllocVec(STDIN_BUF_SIZE, MEMF_PUBLIC)))
      {
      	BPTR input = Input();

#ifdef __SASC
	/* When started from Workbench input will be NULL
	   so we use the handle of stdin */
	if (!input && stdin)
	{
	  struct UFB *stdin_ufb = chkufb(fileno(stdin));
	  if (stdin_ufb) input = (BPTR)stdin_ufb->ufbfh;
	}
#endif
	stdin_handle = (struct FileHandle *)BADDR(input);

	if (stdin_handle && stdin_handle->fh_Type)
	{
          /* Now open the bsdsocket.library, which provides the bsd socket functions */
          if((SocketBase = OpenLibrary(SOCKETNAME,SOCKETVERSION)))
          {
            SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), &errno,
                           SBTM_SETVAL(SBTC_HERRNOLONGPTR), &h_errno,
                           SBTM_SETVAL(SBTC_LOGTAGPTR), "civserver",
                           TAG_END);

            if((UserGroupBase = OpenLibrary(USERGROUPNAME, USERGROUPVERSION)))
            {
              ug_SetupContextTags("civserver",
                                  UGT_INTRMASK, SIGBREAKB_CTRL_C,
                                  UGT_ERRNOPTR(sizeof(errno)), &errno,
                                  TAG_END);

              /* Reserve 0 for stdin */
              Dup2Socket(-1,0);

              send_stdin_write_packet();

              constructor_ok;
            } else printf("Couldn't open " USERGROUPNAME"\n");
          } else printf("Couldn't open " SOCKETNAME "!\nPlease start a TCP/IP stack.\n");
        } else printf("Bad Input Handle\n");
      }
    }
  }
  constructor_failed;
}

/**************************************************************************
 This is the destuctor which clean ups the resources allcated
 by the constructor
**************************************************************************/
#ifdef __VBCC__
void _EXIT_3_amigaserver(void)
#else
void __stdargs _STD_30000_dispose(void)
#endif
{
  if (pkt_sent)
  {
    AbortPkt(stdin_handle->fh_Type, pkt);
    WaitPort(stdin_port);
  }

  if (UserGroupBase) CloseLibrary(UserGroupBase);
  if (SocketBase) CloseLibrary(SocketBase);

  if (pkt) FreeDosObject(DOS_STDPKT, pkt);
  if (stdin_port) DeleteMsgPort(stdin_port);
  if (stdin_buffer) FreeVec(stdin_buffer);

  if (arexx_port)
  {
    if (arexx_added) RemPort(arexx_port);
    DeleteMsgPort(arexx_port);
  }
}

/**************************************************************************
 select() emulation and more. Complete.
**************************************************************************/
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exeptfds, struct timeval *timeout)
{
  ULONG stdinmask = 1UL << stdin_port->mp_SigBit;
  ULONG arexxmask = 1UL << arexx_port->mp_SigBit;

  ULONG mask = SIGBREAKF_CTRL_C | arexxmask;
  int sel;
  BOOL usestdin=FALSE;

  if (readfds)
  {
    if(FD_ISSET(0,readfds))
    {
      FD_CLR(0, readfds);
      usestdin=TRUE;
      mask |= stdinmask;

      send_stdin_write_packet();
    }
  }

  sel = WaitSelect(nfds, readfds, writefds, exeptfds, timeout, &mask);
  if(sel >=0)
  {
    if(mask & SIGBREAKF_CTRL_C)
    {
      printf("\n***Break\n");
      exit(0);
    }

    if(mask & arexxmask)
    {
      struct RexxMsg *msg;
      while((msg = (struct RexxMsg*)GetMsg(arexx_port)))
        ReplyMsg((struct Message*)msg);

      printf("Got ARexx Message");
    }

    if(usestdin && (mask & stdinmask))
    {
      while(GetMsg(stdin_port));
      pkt_sent = FALSE;

      memcpy(stdin_buffer_copy, stdin_buffer, (int)pkt->dp_Res1);
      stdin_buffer_copy_len = (int)pkt->dp_Res1;
      FD_SET(0, readfds);
      sel = 1;
    }
  } else
  {
    printf("select failed %d\n",errno);
    exit(1);
  }

  return sel;
}

/**************************************************************************
 usleep() function. (because select() is too slow). Complete.
**************************************************************************/
void usleep(unsigned long usec)
{
  TimeDelay(0,0,usec);
}

/**************************************************************************
 strerror() which also understand the non ANSI C errors. Complete.
 Note. SAS uses another prototype definition.
**************************************************************************/
/* const char *strerror(unsigned int error) */
char *strerror(int error)
{
  ULONG taglist[3];

  taglist[0] = SBTM_GETVAL(SBTC_ERRNOSTRPTR);
  taglist[1] = error;
  taglist[2] = TAG_END;

  SocketBaseTagList((struct TagItem *)taglist);
  return (char *)taglist[1];
}

/**************************************************************************
 read() emulation. Supports reading from stdin. Otherwise only for
 real bsd sockets (created with socket())
**************************************************************************/
int read(int fd, char *buf, int len)
{
  if(!len) return 0;
  if(!fd)
  {
    int i=0;
    char *src = stdin_buffer_copy;
    char *dest = buf;

    while((i < STDIN_BUF_SIZE - 1) && (i < len - 1) && (i < stdin_buffer_copy_len))
    {
      char c = *src++;
      *dest++ = c; i++;
      if (c==10) break;
    }

    *dest = 0;

    return i;
  } else
  {
    return recv(fd,buf,len,0);
  }
}

/**************************************************************************
 write() emulation. Only for real bsd sockets (created with socket()).
 (The server doesn't use it for other descriptors)
**************************************************************************/
int write(int fd, char *buf, int len)
{
  if(fd) return send(fd,buf,len,0);
}

/**************************************************************************
 close() emulation. Only for real bsd sockets (created with socket()).
 (The server doesn't use it for other descriptors)
**************************************************************************/
void close(int fd)
{
  if(fd) CloseSocket(fd);
}

/**************************************************************************
 inet_ntoa() stub
**************************************************************************/
char *inet_ntoa(struct in_addr addr) 
{
  return Inet_NtoA(addr.s_addr);
}

/**************************************************************************
 ioctl() stub
**************************************************************************/
int ioctl(int fd, unsigned int request, char *argp)
{
  return IoctlSocket(fd, request, argp);
}

/**************************************************************************
 For VBCC - this is also in extra.lib
**************************************************************************/
clock_t clock(void)
{
  struct DateStamp ds;
  DateStamp(&ds);

  return (clock_t)((ds.ds_Days*1440 + ds.ds_Minute)*60 +
                    ds.ds_Tick/TICKS_PER_SECOND);
}

/**************************************************************************
 Another time function for SAS (the orginal function always looked
 for the TZ variable)
**************************************************************************/
#ifdef __SASC
time_t time(time_t *timeptr)
{
  time_t timeval = (time_t)clock() + 8*365*24*60*60;
  if (timeptr) *timeptr = timeval;
  return timeval;
}
#endif

#ifdef __VBCC__

#endif
