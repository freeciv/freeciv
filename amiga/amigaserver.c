/* This is a preparse main function for freeciv server.
The file server/civserver.c must be compiled with main defined to civ_main,
or this will not work.

This method is hopefully a lot better portable than the previous method, which
depended on init, exit, cleanup and destructor methods of compiler.

The main() functions either gets
1) C-style commandline arguments or
2) argc == 0 and argv == WBStartup message.

If your compiler does not support this, create a preparse function, which
produces that results. Maybe rename main() to main2() and name your preparse
function main() and call main2() afterwards. This depends on your compiler.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#undef HAVE_CONFIG_H /* little trick to remove error message */
#include "version.h"

#include <exec/memory.h>
#include <workbench/startup.h>

#include <clib/alib_protos.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/socket.h>
#ifdef MIAMI_SDK
#include <bsdsocket/socketbasetags.h>
#else /* AmiTCP */
#include <amitcp/socketbasetags.h>
#endif

#ifdef __SASC
#include <ios1.h>
#endif

#define STDIN_BUF_SIZE 256

#define SOCKETNAME "bsdsocket.library"
#define SOCKETVERSION 3     /* minimum bsdsocket version to use */
#define USERGROUPVERSION 1

/* the external definitions */
extern int civ_main(int, void *); /* The prototype of the REAL freeciv main function */
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;

struct Library *SocketBase = 0;
struct IntuitionBase *IntuitionBase = 0;
struct Library *IconBase = 0;
static char *stdargv[1] = {
"civserver"}
; /* standard arg, if WB parsing failed */
static struct MsgPort *arexx_port = 0;
static struct MsgPort *stdin_port = 0;
static STRPTR stdin_buffer = 0;
static char stdin_buffer_copy[STDIN_BUF_SIZE]; /* copy of the stdin_buffer */
static int stdin_buffer_copy_len;
static struct DosPacket *pkt = 0;
static int pkt_sent = 0;
static struct FileHandle *stdin_handle;
#ifdef __SASC
const char *version = "$VER: civserver " VERSION_STRING " " __AMIGADATE__;
#else
const char *version = "$VER: civserver " VERSION_STRING " (" __DATE__ ")";
#endif

#ifdef __SASC
/* Stack for the server */
__near LONG __stack = 150000;
#elif defined(__VBCC__)
LONG __stack = 150000;
#endif

/*********************************************************/

static void send_stdin_write_packet(void)
{
  if(!pkt_sent)
  {
    pkt->dp_Type = ACTION_READ;
    pkt->dp_Arg1 = stdin_handle->fh_Arg1;
    pkt->dp_Arg2 = (LONG)stdin_buffer;
    pkt->dp_Arg3 = STDIN_BUF_SIZE;
    SendPkt(pkt, stdin_handle->fh_Type, stdin_port);

    pkt_sent = TRUE;
  }
}

static void civ_exitfunc(void)
{
  if(pkt_sent)
  {
    AbortPkt(stdin_handle->fh_Type, pkt);
    WaitPort(stdin_port);
  }

  if(SocketBase) CloseLibrary(SocketBase);
  if(IntuitionBase) CloseLibrary((struct Library *) IntuitionBase);

  if(pkt) FreeDosObject(DOS_STDPKT, pkt);
  if(stdin_port) DeleteMsgPort(stdin_port);
  if(stdin_buffer) FreeVec(stdin_buffer);

  if(arexx_port)
  {
    RemPort(arexx_port);
    DeleteMsgPort(arexx_port);
  }
}

int main(int argc, char **argv)
{
  struct DiskObject *dob;
  struct WBArg *wba;
  BPTR dir, input;
  int ret = 20, i, j, len, u, h_errno;
  char *ttype, *dest, *second;
  struct UFB *stdin_ufb;

  /* using malloc needs to free calls */
  if(!argc) /* called from WB */
  {
    if(IconBase = OpenLibrary("icon.library", 0L))
    {
      wba = ((struct WBStartup *) argv)->sm_ArgList;
      dir = CurrentDir(wba->wa_Lock);

      if((dob = GetDiskObject(wba->wa_Name)))
      {
        i = 1;

        if(dob->do_ToolTypes)
        {
          for(j = 0; dob->do_ToolTypes[j]; ++j)
          {
            if(*dob->do_ToolTypes[j] != '(' && *dob->do_ToolTypes[j] != ' ' && *dob->do_ToolTypes[j] != ';')
            {
              ++argc;
              if(strchr(dob->do_ToolTypes[j], '='))
              {
                ++argc;
              }
            }
          }
        }

        ++argc; /* the program name */
        if((argv = (char**)malloc(sizeof(char*)*argc)))
        {
          argv[0] = stdargv[0]; /* the file name */

          for(j = 0; argc && dob->do_ToolTypes[j]; ++j)
          {
            ttype = dob->do_ToolTypes[j];
            len = strlen(ttype);
            second = strchr(ttype,'=');

            if (*ttype == '(' || *ttype == ' ' || *ttype == ';')
            {
              ;
            }
            else if((dest = argv[i++] = (char*)malloc(len+4)))
            {
              if(second)
              {
                len = second-ttype;
              }
              dest[0] = dest[1] = '-';
              strcpy(dest+2,ttype);
              dest[2+len] = 0;

              for(u = 0; dest[u]; ++u)
              {
                dest[u]=tolower(dest[u]);
              }
              if(second)
              {
                argv[i++] = dest+3+len;
              }
            }
            else
            {
              argc = 0; /* error */
            }
          }
        }
        else
        {
          argc=0; /* error */
        }
        FreeDiskObject(dob);
      }
      /* get disk object */
      CurrentDir(dir);
      CloseLibrary(IconBase);
    }
    /* OpenLibary */
  }
  /* WB start */

  atexit(civ_exitfunc); /* we need to free the stuff on exit()! */
  if((IntuitionBase = (struct IntuitionBase *) OpenLibrary("intuition.library", 37)))
  {
    /* create the ARexx Port, which actually is only a dummy port */
    if((arexx_port = CreateMsgPort()))
    {
      Forbid();
      if(FindPort("CIVSERVER"))
      {
        Permit();
        DeleteMsgPort(arexx_port);
        arexx_port = 0;
        printf("Civserver is already running!\n");
      }
      else
      {
        arexx_port->mp_Node.ln_Name = "CIVSERVER";
        AddPort(arexx_port);
        Permit();

        if((stdin_port = CreateMsgPort()))
        {
          if((pkt = AllocDosObject(DOS_STDPKT, NULL)))
          {
            if((stdin_buffer = AllocVec(STDIN_BUF_SIZE, MEMF_PUBLIC)))
            {
              input = Input();

              #ifdef __SASC
              /* When started from Workbench input will be NULL so we use the handle of stdin */
              if(!input && stdin)
              {
                if (stdin_ufb = chkufb(fileno(stdin)))
                {
                  input = (BPTR)stdin_ufb->ufbfh;
                }
              }
              #endif
              stdin_handle = (struct FileHandle *) BADDR(input);

              if(stdin_handle && stdin_handle->fh_Type)
              {
                /* Now open the bsdsocket.library, which provides the bsd socket functions */
                if((SocketBase = OpenLibrary(SOCKETNAME,SOCKETVERSION)))
                {
                  SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), &errno, SBTM_SETVAL(SBTC_HERRNOLONGPTR), &h_errno, SBTM_SETVAL(SBTC_LOGTAGPTR), "civserver", TAG_END);

                  /* Reserve 0 for stdin */
                  Dup2Socket(-1,0);

                  send_stdin_write_packet();

                  /* all went well, call main function */
                  if(!argc)
                  {
                    ret = civ_main(1, stdargv);
                  }
                  else
                  {
                    ret = civ_main(argc, argv);
                  }
                }
                else
                {
                  printf("Couldn't open " SOCKETNAME "!\nPlease start a TCP/IP stack.\n");
                }
              }
              else
              {
                printf("Bad Input Handle\n");
              }
            }
          }
        }
      }
    }
  }
  exit(ret);
}

/**************************************************************************
 select() emulation and more. Complete.
**************************************************************************/
#ifdef MIAMI_SDK
long select(long nfds,fd_set *readfds, fd_set *writefds, fd_set *exeptfds, void *timeout)
#else
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exeptfds, struct timeval *timeout)
#endif
{
  ULONG stdinmask = 1UL << stdin_port->mp_SigBit;
  ULONG arexxmask = 1UL << arexx_port->mp_SigBit;

  ULONG mask = SIGBREAKF_CTRL_C | arexxmask;
  int sel;
  BOOL usestdin=FALSE;

  struct RexxMsg *msg;

  if (readfds)
  {
    if (FD_ISSET(0,readfds))
    {
      FD_CLR(0, readfds);
      usestdin=TRUE;
      mask |= stdinmask;

      send_stdin_write_packet();
    }
  }

  sel = WaitSelect(nfds, readfds, writefds, exeptfds, timeout, &mask);
  if (sel >=0)
  {
    if (mask & SIGBREAKF_CTRL_C)
    {
      printf("\n***Break\n");
      exit(EXIT_SUCCESS);
    }

    if (mask & arexxmask)
    {
      while((msg = (struct RexxMsg*)GetMsg(arexx_port)))
      ReplyMsg((struct Message*)msg);

      printf("Got ARexx Message");
    }

    if (usestdin && (mask & stdinmask))
    {
      while(GetMsg(stdin_port));
      pkt_sent = FALSE;

      memcpy(stdin_buffer_copy, stdin_buffer, (int)pkt->dp_Res1);
      stdin_buffer_copy_len = (int)pkt->dp_Res1;
      FD_SET(0, readfds);
      sel = 1;
    }
  }
  else
  {
    printf("select failed %d\n",errno);
    exit(EXIT_FAILURE);
  }
  return(sel);
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
  return((char *)taglist[1]);
}

/**************************************************************************
 read() emulation. Supports reading from stdin. Otherwise only for
 real bsd sockets (created with socket())
**************************************************************************/
int read(int fd, char *buf, int len)
{
  int i;
  char *src;
  char *dest;
  char c;

  if (!len)
  {
    return(0);
  }
  if (!fd)
  {
    i=0;
    src = stdin_buffer_copy;
    dest = buf;

    while((i < STDIN_BUF_SIZE - 1) && (i < len - 1) && (i < stdin_buffer_copy_len))
    {
      c = *src++;
      *dest++ = c;
      i++;
      if (c==10)
      {
        break;
      }
    }

    *dest = 0;

    return(i);
  }
  else
  {
    return(recv(fd,buf,len,0));
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
