#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <ios1.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <devices/input.h>
#include <intuition/intuitionbase.h>
#include <amitcp/socketbasetags.h>
#include <libraries/usergroup.h>
#include <rexx/storage.h>
#include <sys/errno.h>

#include <clib/alib_protos.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <clib/intuition_protos.h>
#include <clib/socket_protos.h>
#include <clib/usergroup_protos.h>

#include <pragmas/exec_sysbase_pragmas.h>
#include <pragmas/dos_pragmas.h>
#include <pragmas/intuition_pragmas.h>
#include <pragmas/socket_pragmas.h>
#include <pragmas/usergroup_pragmas.h>

/* The ARexx Port stuff - currently not really used */
STATIC struct MsgPort *arexx_port;
STATIC ULONG arexx_added;

/* Stuff for the input.device */
STATIC struct MsgPort *input_port;
STATIC struct IOStdReq *input_req;
STATIC ULONG input_open;

/* Stuff for the console */
STATIC struct MsgPort *con_port;
STATIC struct Window *con_window;
STATIC struct Interrupt *con_interrupt;

#define SOCKETNAME "bsdsocket.library"
#define SOCKETVERSION 3		/* minimum bsdsocket version to use */
#define USERGROUPVERSION 1

int h_errno;

struct Library *SocketBase;
struct Library *UserGroupBase;

/* Opened by the compiler */
IMPORT struct Library *SysBase;
IMPORT struct Library *DOSBase;
IMPORT struct IntuitionBase *IntuitionBase;

/* Stack for the server */
__near LONG __stack = 150000;

struct ConMsg
{
  struct Message msg;
  ULONG type;
};

#define CT_RETURN 1

/**************************************************************************
 The Inputhandler simply sends a message to the con_port if somebody
 pressed enter within the console window where the civserver  is started
**************************************************************************/
__asm __saveds struct InputEvent *InterruptHandler( register __a0 struct InputEvent *oldEventChain, register __a1 ULONG data)
{
  struct InputEvent *ie;
  
  for (ie = oldEventChain; ie; ie = ie->ie_NextEvent)
  {
    if(ie->ie_Class == IECLASS_RAWKEY)
    {
      if(IntuitionBase->ActiveWindow == con_window)
      {
        if((ie->ie_Code == 68 || ie->ie_Code ==67) && !(ie->ie_Qualifier & IEQUALIFIER_REPEAT))
        {
          struct ConMsg *msg = (struct ConMsg*)AllocVec(sizeof(struct ConMsg),MEMF_PUBLIC);
          if(msg)
          {
            msg->type = CT_RETURN;
            PutMsg(con_port,(struct Message*)msg);
          }
        }
      }
    }
  }
  return oldEventChain;
}

/**************************************************************************
 This is the constructor for the server (called befor main()) which does
 some necessary initialations so Freeciv needn't to be compiled with GNU C
**************************************************************************/
int __stdargs _STI_30000_init(void)
{
  /* create the ARexx Port, which actually is only a dummy port */
  struct MsgPort *old_port;
  if(!(arexx_port = CreateMsgPort())) return 1;

  Forbid();
  if(old_port = FindPort("CIVSERVER"))
  {
    Permit();
    DeleteMsgPort(old_port);
    printf("Civserver is already running!\n");
    return 1;
  }
  arexx_port->mp_Node.ln_Name = "CIVSERVER";
  arexx_added = TRUE;
  AddPort(arexx_port);
  Permit();

  if((input_port = CreateMsgPort()))
  {
    if((input_req = (struct IOStdReq*)CreateIORequest(input_port,sizeof(struct IOStdReq))))
    {
      if(!OpenDevice("input.device",0,(struct IORequest*)input_req,0))
      {
        input_open = TRUE;

        if((con_port = CreateMsgPort()))
        {
          struct MsgPort *console_task = GetConsoleTask();
          if(console_task)
          {
            struct InfoData *id = (struct InfoData*)AllocVec(sizeof(*id),0);
            if(id)
            {
              DoPkt(console_task,ACTION_DISK_INFO,((ULONG)id)>>2,NULL,NULL,NULL,NULL);
              if((con_window = (struct Window*)id->id_VolumeNode))
              {
                if((con_interrupt = (struct Interrupt*)AllocVec(sizeof(struct Interrupt),MEMF_PUBLIC)))
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

                      /* Tell the input.device our input handler.
                         From now we get every time the user pressed Return or Enter
                         a message in the con_port */
                      con_interrupt->is_Code = (void(*)())InterruptHandler;
                      con_interrupt->is_Data = NULL;
                      con_interrupt->is_Node.ln_Pri = 10;
                      con_interrupt->is_Node.ln_Name = "Civserver Handler";

                      input_req->io_Data = (APTR)con_interrupt;
                      input_req->io_Command = IND_ADDHANDLER;

                      DoIO((struct IORequest*)input_req);

                      return 0;
                    }  else printf("Couldn't open " USERGROUPNAME"\n");
                  }  else printf("Couldn't open " SOCKETNAME "!\nPlease start a TCP/IP stack.\n");
                }
              }
              FreeVec(id);
            }
          }
        }
      }
    }
  }
  return 1;
}

/**************************************************************************
 This is the destuctor which clean ups the resources allcated
 by the constructor
**************************************************************************/
void __stdargs _STD_30000_dispose(void)
{
  if(UserGroupBase)
  {
    CloseLibrary(UserGroupBase);

    input_req->io_Data = (APTR)con_interrupt;
    input_req->io_Command = IND_REMHANDLER;

    DoIO((struct IORequest*)input_req);
  }
  if(SocketBase) CloseLibrary(SocketBase);

  if(con_interrupt) FreeVec(con_interrupt);
  if(con_port) DeleteMsgPort(con_port);

  if(input_open) CloseDevice((struct IORequest*)input_req);
  if(input_req) DeleteIORequest((struct IORequest*)input_req);
  if(input_port) DeleteMsgPort(input_port);

  if(arexx_port)
  {
    if(arexx_added) RemPort(arexx_port);
    DeleteMsgPort(arexx_port);
  }
}

/**************************************************************************
 select() emulation and more. Complete.
**************************************************************************/
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exeptfds, struct timeval *timeout)
{
  ULONG conmask = 1UL << con_port->mp_SigBit;
  ULONG arexxmask = 1UL << arexx_port->mp_SigBit;

  ULONG mask = SIGBREAKF_CTRL_C | conmask | arexxmask;
  int sel;
  BOOL usestdin=FALSE;

  if(FD_ISSET(0,readfds))
  {
    FD_CLR(0, readfds);
    usestdin=TRUE;
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
      {
        ReplyMsg((struct Message*)msg);
      }
      printf("Got ARexx Message");
    }

    if(usestdin && (mask & conmask))
    {
      struct ConMsg *msg;
      while((msg = (struct ConMsg*)GetMsg(con_port))) FreeVec(msg);
      FD_SET(0, readfds);
      sel = 1;
    }
  } else
  {
    printf("select failed %ld\n",errno);
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
  if(!fd)
  {
    char *str = fgets(buf,len,stdin);
    if(str) return (int)strlen(str);
    return 0;
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
