/********************************************************************** 
Freeciv - Copyright (C) 2004 - The Freeciv Project
   This program is free software; you can redistribute it and / or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful, 
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/ 
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
  
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#ifdef HAVE_SYS_SOCKET_H
  #include <sys/socket.h>
#endif
  
#ifdef HAVE_SYS_TYPES_H
  #include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
  #include <sys/stat.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef HAVE_WINSOCK
#include <winsock.h>
#endif

#include "fcintl.h"
#include "mem.h"
#include "netintf.h"
#include "rand.h"
#include "registry.h"
#include "support.h"
#include "civclient.h"
#include "climisc.h"
#include "clinet.h"
#include "packhand_gen.h"

#include "chatline_common.h"
#include "connectdlg_g.h"
#include "connectdlg_common.h"

#define WAIT_BETWEEN_TRIES 100000 /* usecs */ 
#define NUMBER_OF_TRIES 500
  
#ifdef WIN32_NATIVE
HANDLE server_process = INVALID_HANDLE_VALUE;
#else
pid_t server_pid = - 1;
#endif

char player_name[MAX_LEN_NAME];
char *current_filename = NULL;

bool client_has_hack = FALSE;

const char *skill_level_names[NUM_SKILL_LEVELS] = { 
  N_("novice"),
  N_("easy"), 
  N_("normal"), 
  N_("hard")
 ,N_("experimental")
};

/************************************************************************** 
The general chain of events:

Two distinct paths are taken depending on the choice of mode: 

if the user selects the multi- player mode, then a packet_req_join_game 
packet is sent to the server. It is either successful or not. The end.

If the user selects a single- player mode (either a new game or a save game) 
then: 
   1. the packet_req_join_game is sent.
   2. on receipt, if we can join, then a challenge packet is sent to the
      server, so we can get hack level control.
   3. if we can't get hack, then we get dumped to multi- player mode. If
      we can, then:
      a. for a new game, we send a series of packet_generic_message packets
         with commands to start the game.
      b. for a saved game, we send the load command with a 
         packet_generic_message, then we send a PACKET_PLAYER_LIST_REQUEST.
         the response to this request will tell us if the game was loaded or
         not. if not, then we send another load command. if so, then we send
         a series of packet_generic_message packets with commands to start 
         the game.
**************************************************************************/ 

/************************************************************************** 
Tests if the client has started the server.
**************************************************************************/ 
bool is_server_running()
{ 
#ifdef WIN32_NATIVE
  return (server_process != INVALID_HANDLE_VALUE);
#else    
  return (server_pid > 0);
#endif
} 

/************************************************************************** 
Kills the server if the client has started it (FIXME: atexit handler?)
**************************************************************************/ 
void client_kill_server()
{
  if (is_server_running()) {
#ifdef WIN32_NATIVE
    TerminateProcess(server_process, 0);
    CloseHandle(server_process);
    server_process = INVALID_HANDLE_VALUE;		
#else
    kill(server_pid, SIGTERM);
    waitpid(server_pid, NULL, WUNTRACED);
    server_pid = - 1;
#endif    
  }
}   

/**************************************************************** 
forks a server if it can. returns FALSE is we find we couldn't start
the server.
This is so system-intensive that it's *nix only.  VMS and Windows 
code will come later 
*****************************************************************/ 
bool client_start_server(void)
{
#if !defined(__VMS) && !defined(WIN32)
  int i = 0;
  int nargs = 4; /* base number of args */
  char **argv = NULL;
  char buf[512];

  /* only one server (forked from this client) shall be running at a time */ 
  client_kill_server();

  append_output_window(_("Starting server..."));

  /* find a free port */ 
  server_port = min_free_port();

  /* determine how many args we need */
  if (logfile) {
    nargs += 4;
  }
  if (scriptfile) {
    nargs += 2;
  }

  argv = fc_malloc(nargs * sizeof(char*));
  argv[i] = fc_malloc(10);
  my_snprintf(argv[i++], 10, "civserver");
  argv[i] = fc_malloc(3);
  my_snprintf(argv[i++], 3, "-p");
  argv[i] = fc_malloc(7);
  my_snprintf(argv[i++], 7, "%d", server_port);

  if (logfile) {
    argv[i] = fc_malloc(8);
    my_snprintf(argv[i++], 8, "--debug");
    argv[i] = fc_malloc(2);
    my_snprintf(argv[i++], 2, "3");
    argv[i] = fc_malloc(6);
    my_snprintf(argv[i++], 6, "--log");
    argv[i] = fc_malloc(strlen(logfile) + 1);
    my_snprintf(argv[i++], strlen(logfile) + 1, logfile);
  }
  if (scriptfile) {
    argv[i] = fc_malloc(7);
    my_snprintf(argv[i++], 7, "--read");
    argv[i] = fc_malloc(strlen(scriptfile) + 1);
    my_snprintf(argv[i++], strlen(scriptfile) + 1, scriptfile);
  }
  argv[i] = NULL;

  server_pid = fork();
  
  if (server_pid == 0) {
    int fd;

    /* inside the fork */  

    /* avoid terminal spam, but still make server output available */ 
    fclose(stdout);
    fclose(stderr);

    /* include the port to avoid duplication */
    if (logfile) {
      fd = open(logfile, O_WRONLY | O_CREAT);

      if (fd != 1) {
        dup2(fd, 1);
      }
      if (fd != 2) {
        dup2(fd, 2);
      }
      fchmod(1, 0644);
    }

    /* If it's still attatched to our terminal, things get messed up, 
      but civserver needs *something* */ 
    fclose(stdin);
    fd = open("/dev/null", O_RDONLY);
    if (fd != 0) {
      dup2(fd, 0);
    }

    /* these won't return on success */ 
    execvp("./ser", argv);
    execvp("./server/civserver", argv);
    execvp("civserver", argv);
    
    /* This line is only reached if civserver cannot be started, 
     * so we kill the forked process.
     * Calling exit here is dangerous due to X11 problems (async replies) */ 
    _exit(1);
  } 

  /* don't need these anymore */ 
  for (i = 0; i < nargs - 1; i++) {
    free(argv[i]);
  }
  free(argv);

  /* a reasonable number of tries */ 
  while(connect_to_server((char *)user_username(), "localhost", server_port, 
                          buf, sizeof(buf)) == -1) {
    myusleep(WAIT_BETWEEN_TRIES);

    if (i > NUMBER_OF_TRIES)  break;
    i++;
  }

  /* weird, but could happen, if server doesn't support new startup stuff
   * capabilities won't help us here... */ 
  if (!aconnection.used) {
    /* possible that server is still running. kill it */ 
    kill(server_pid, SIGTERM);
    server_pid = -1;

    append_output_window(_("Couldn't connect to the server. We probably "
                           "couldn't start it from here. You'll have to "
                           "start one manually. Sorry..."));
    return FALSE;
  }

  return TRUE;
#else /*VMS or Win32*/
  return FALSE;
#endif
}

/************************************************************************** 
Finds the lowest port which can be used for the 
server (starting at the 5555C)
**************************************************************************/ 
int min_free_port(void)
{
  int port, n, s;
  struct sockaddr_in tmp;
  
  s = socket(AF_INET, SOCK_STREAM, 0);
  n = INADDR_ANY;
  port = 5554; /* make looping convenient */ 
  do {
    port++;
    memset(&tmp, 0, sizeof(struct sockaddr_in));
    tmp.sin_family = AF_INET;
    tmp.sin_port = htons(port);
    memcpy(&tmp. sin_addr, &n, sizeof(long));
  } while(bind(s, (struct sockaddr*) &tmp, sizeof(struct sockaddr_in)));

  my_closesocket(s);
  
  return port;
}

/**************************************************************** 
if the client is capable of 'wanting hack', then the server will 
send the client a filename in the packet_join_game_reply packet.

this function creates the file with a suitably random number in it 
and then sends the filename and number to the server. If the server 
can open and read the number, then the client is given hack access.
*****************************************************************/ 
void send_client_wants_hack(char * filename)
{
  struct section_file file;

  /* find some entropy */ 
  int entropy = myrand(MAX_UINT32) - time(NULL);

  /* we don't want zero */ 
  if (entropy == 0) {
    entropy++;
  }

  section_file_init(&file);
  secfile_insert_int(&file, entropy, "challenge.entropy");
  section_file_save(&file, filename, 0);      
  section_file_free(&file);

  /* tell the server what we put into the file */ 
  dsend_packet_single_want_hack_req(&aconnection, entropy);
}

/**************************************************************** 
handle response (by the server) if the client has got hack or not.
*****************************************************************/ 
void handle_single_want_hack_reply(bool you_have_hack)
{
  if (you_have_hack) {
    append_output_window(_("We have control of the server "
                         "(command access level hack)"));
    client_has_hack = TRUE;
  } else if (is_server_running()) {
    /* only output this if we started the server and we NEED hack */
    append_output_window(_("We can't take control of server, "
                         "attempting to kill it."));
    client_kill_server();
  }
}

/**************************************************************** 
send server commands to start a saved game.
*****************************************************************/ 
void send_start_saved_game(void)
{   
  char buf[MAX_LEN_MSG];

  send_chat("/set timeout 0");
  send_chat("/set autotoggle 1");
  my_snprintf(buf, sizeof(buf), "/take %s %s", user_name, player_name);
  send_chat(buf);
  send_chat("/start");
}

/**************************************************************** 
send server command to save game.
*****************************************************************/ 
void send_save_game(char *filename)
{   
  char message[MAX_LEN_MSG];

  if (filename) {
    my_snprintf(message, MAX_LEN_MSG, "/save %s", filename);
  } else {
    my_snprintf(message, MAX_LEN_MSG, "/save");
  }

  send_chat(message);
}
