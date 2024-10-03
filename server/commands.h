/***********************************************************************
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__COMMANDS_H
#define FC__COMMANDS_H

/* common */
#include "connection.h"         /* enum cmdlevel */

enum cmd_echo {
  CMD_ECHO_NONE = 0,
  CMD_ECHO_ADMINS,      /* i.e. all with 'admin' access and above. */
  CMD_ECHO_ALL,
};

/**************************************************************************
  Commands - can be recognised by unique prefix
**************************************************************************/
/* Order here is important: for ambiguous abbreviations the first
   match is used.  Arrange order to:
   - allow old commands 's', 'h', 'l', 'q', 'c' to work.
   - reduce harm for ambiguous cases, where "harm" includes inconvenience,
     eg accidentally removing a player in a running game.
*/
enum command_id {
  /* old one-letter commands: */
  CMD_START_GAME = 0,
  CMD_HELP,
  CMD_LIST,
  CMD_QUIT,
  CMD_CUT,

  /* completely non-harmful: */
  CMD_EXPLAIN,
  CMD_SHOW,
  CMD_WALL,
  CMD_CONNECTMSG,
  CMD_VOTE,
  
  /* mostly non-harmful: */
  CMD_DEBUG,
  CMD_SET,
  CMD_TEAM,
  CMD_RULESETDIR,
  CMD_METAMESSAGE,
  CMD_METAPATCHES,
  CMD_METACONN,
  CMD_METASERVER,
  CMD_AITOGGLE,
  CMD_TAKE,
  CMD_OBSERVE,
  CMD_DETACH,
  CMD_CREATE,
  CMD_AWAY,
  CMD_RESTRICTED,
  CMD_NOVICE,
  CMD_EASY,
  CMD_NORMAL,
  CMD_HARD,
  CMD_CHEATING,
#ifdef FREECIV_DEBUG
  CMD_EXPERIMENTAL,
#endif
  CMD_CMDLEVEL,
  CMD_FIRSTLEVEL,
  CMD_TIMEOUT,
  CMD_CANCELVOTE,
  CMD_IGNORE,
  CMD_UNIGNORE,
  CMD_PLAYERCOLOR,
  CMD_PLAYERNATION,

  /* potentially harmful: */
  CMD_END_GAME,
  CMD_SURRENDER, /* not really harmful, info level */
  CMD_REMOVE,
  CMD_SAVE,
  CMD_SCENSAVE,
  CMD_LOAD,
  CMD_READ_SCRIPT,
  CMD_WRITE_SCRIPT,
  CMD_RESET,
  CMD_DEFAULT,
  CMD_LUA,
  CMD_KICK,
  CMD_DELEGATE,
  CMD_AICMD,
  CMD_FCDB,
  CMD_MAPIMG,

  CMD_LOCK,
  CMD_UNLOCK,

  /* undocumented */
  CMD_RFCSTYLE,
  CMD_SRVID,

  /* pseudo-commands: */
  CMD_NUM,              /* the number of commands - for iterations */
  CMD_UNRECOGNIZED,     /* used as a possible iteration result */
  CMD_AMBIGUOUS         /* used as a possible iteration result */
};

const struct command *command_by_number(int i);
const char *command_name_by_number(int i);

const char *command_name(const struct command *pcommand);
const char *command_synopsis(const struct command *pcommand);
const char *command_short_help(const struct command *pcommand);
char *command_extra_help(const struct command *pcommand);

enum cmdlevel command_level(const struct command *pcommand);
enum cmd_echo command_echo(const struct command *pcommand);
int command_vote_flags(const struct command *pcommand);
int command_vote_percent(const struct command *pcommand);

#endif /* FC__COMMANDS_H */
