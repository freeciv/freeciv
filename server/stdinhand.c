/**********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
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

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_LIBREADLINE
#include <readline/readline.h>
#ifdef HAVE_NEWLIBREADLINE
#define completion_matches(x,y) rl_completion_matches(x,y)
#define filename_completion_function rl_filename_completion_function
#endif
#endif

#include "fciconv.h"

#include "astring.h"
#include "capability.h"
#include "events.h"
#include "fcintl.h"
#include "game.h"
#include "log.h"
#include "map.h"
#include "mem.h"
#include "packets.h"
#include "player.h"
#include "registry.h"
#include "shared.h"		/* fc__attribute, bool type, etc. */
#include "support.h"
#include "timing.h"
#include "version.h"

#include "citytools.h"
#include "commands.h"
#include "connecthand.h"
#include "console.h"
#include "diplhand.h"
#include "gamehand.h"
#include "gamelog.h"
#include "mapgen.h"
#include "maphand.h"
#include "meta.h"
#include "plrhand.h"
#include "report.h"
#include "ruleset.h"
#include "sanitycheck.h"
#include "savegame.h"
#include "sernet.h"
#include "settings.h"
#include "srv_main.h"

#include "advmilitary.h"	/* assess_danger_player() */
#include "ailog.h"

#include "stdinhand.h"

/* Import */
#include "stdinhand_info.h"

static enum cmdlevel_id default_access_level = ALLOW_INFO;
static enum cmdlevel_id   first_access_level = ALLOW_CTRL;

static bool cut_client_connection(struct connection *caller, char *name,
                                  bool check);
static bool show_help(struct connection *caller, char *arg);
static bool show_list(struct connection *caller, char *arg);
static void show_connections(struct connection *caller);
static bool set_ai_level(struct connection *caller, char *name, int level, 
                         bool check);
static bool set_away(struct connection *caller, char *name, bool check);

static bool is_allowed_to_take(struct player *pplayer, bool will_obs, 
                               char *msg);

static bool observe_command(struct connection *caller, char *name, bool check);
static bool take_command(struct connection *caller, char *name, bool check);
static bool detach_command(struct connection *caller, char *name, bool check);
static bool start_command(struct connection *caller, char *name, bool check);
static bool end_command(struct connection *caller, char *str, bool check);

enum vote_type {
  VOTE_NONE, VOTE_UNUSED, VOTE_YES, VOTE_NO
};
struct voting {
  char command[MAX_LEN_CONSOLE_LINE]; /* [0] == \0 if none in action */
  enum vote_type votes_cast[MAX_NUM_PLAYERS]; /* see enum above */
  int vote_no; /* place in the queue */
  bool full_turn; /* has a full turn begun for this vote yet? */
  int yes, no;
};
static struct voting votes[MAX_NUM_PLAYERS];
static int last_vote;

static const char horiz_line[] =
"------------------------------------------------------------------------------";

/********************************************************************
Returns whether the specified server setting (option) should be
sent to the client.
*********************************************************************/
static bool sset_is_to_client(int idx)
{
  return (settings[idx].to_client == SSET_TO_CLIENT);
}

typedef enum {
    PNameOk,
    PNameEmpty,
    PNameTooLong,
    PNameIllegal
} PlayerNameStatus;

/**************************************************************************
...
**************************************************************************/
static PlayerNameStatus test_player_name(char* name)
{
  size_t len = strlen(name);

  if (len == 0) {
      return PNameEmpty;
  } else if (len > MAX_LEN_NAME-1) {
      return PNameTooLong;
  } else if (mystrcasecmp(name, ANON_PLAYER_NAME) == 0) {
      return PNameIllegal;
  } else if (mystrcasecmp(name, OBSERVER_NAME) == 0) {
      return PNameIllegal;
  }

  return PNameOk;
}

static const char *cmdname_accessor(int i) {
  return commands[i].name;
}
/**************************************************************************
  Convert a named command into an id.
  If accept_ambiguity is true, return the first command in the
  enum list which matches, else return CMD_AMBIGOUS on ambiguity.
  (This is a trick to allow ambiguity to be handled in a flexible way
  without importing notify_player() messages inside this routine - rp)
**************************************************************************/
static enum command_id command_named(const char *token, bool accept_ambiguity)
{
  enum m_pre_result result;
  int ind;

  result = match_prefix(cmdname_accessor, CMD_NUM, 0,
			mystrncasecmp, token, &ind);

  if (result < M_PRE_AMBIGUOUS) {
    return ind;
  } else if (result == M_PRE_AMBIGUOUS) {
    return accept_ambiguity ? ind : CMD_AMBIGUOUS;
  } else {
    return CMD_UNRECOGNIZED;
  }
}

/**************************************************************************
  Initialize stuff related to this code module.
**************************************************************************/
void stdinhand_init(void)
{
  int i;

  for (i = 0; i < MAX_NUM_PLAYERS; i++) {
    votes[i].command[0] = '\0';
    memset(votes[i].votes_cast, 0, sizeof(votes[i].votes_cast));
    votes[i].vote_no = -1;
    votes[i].full_turn = FALSE;
  }
  last_vote = -1;
}

/**************************************************************************
  Check if we satisfy the criteria for resolving a vote, and resolve it
  if these critera are indeed met. Updates yes and no variables in voting 
  struct as well.

  Criteria:
    Accepted immediately if: > 50% of votes for
    Rejected immediately if: >= 50% of votes against
    Accepted on conclusion iff: More than half eligible voters voted for,
                                or none against.
**************************************************************************/
static void check_vote(struct voting *vote)
{
  int i, num_cast = 0, num_voters = 0;

  vote->yes = 0;
  vote->no = 0;

  for (i = 0; i < MAX_NUM_PLAYERS; i++) {
    if (game.players[i].is_alive && game.players[i].is_connected) {
      num_voters++;
    } else {
      /* Disqualify already given vote (eg if disconnected after voting) */
      vote->votes_cast[i] = VOTE_NONE;
    }
  }
  for (i = 0; i < MAX_NUM_PLAYERS; i++) {
    num_cast = (vote->votes_cast[i] > VOTE_NONE) ? num_cast + 1 : num_cast;
    vote->yes = (vote->votes_cast[i] == VOTE_YES) ? vote->yes + 1 : vote->yes;
    vote->no = (vote->votes_cast[i] == VOTE_NO) ? vote->no + 1 : vote->no;
  }

  /* Check if we should resolve the vote */
  if (vote->command[0] != '\0'
      && num_voters > 0
      && (vote->full_turn == TRUE
          || (vote->yes > num_voters / 2)
          || (vote->no >= (num_voters + 1) / 2))) {
    /* Yep, resolve this one */
    vote->vote_no = -1;
    vote->full_turn = FALSE;
    if (last_vote == vote->vote_no) {
      last_vote = -1;
    }
    if (vote->yes > num_voters / 2 || vote->no == 0) {
      /* Do it! */
      notify_conn(NULL, _("Vote \"%s\" is passed %d to %d with %d "
			  "abstentions."), vote->command, vote->yes, vote->no,
		  num_voters - vote->yes - vote->no);
      handle_stdin_input((struct connection *)NULL, vote->command, FALSE);
    } else {
      notify_conn(NULL, _("Vote \"%s\" failed with %d against, %d for "
			  "and %d abstentions."),
		  vote->command, vote->no, vote->yes, 
		  num_voters - vote->yes - vote->no);
    }
    vote->command[0] = '\0';
  }
}

/**************************************************************************
  Update stuff every turn that is related to this code module. Run this
  on turn end.
**************************************************************************/
void stdinhand_turn(void)
{
  int i;

  /* Check if any votes have passed */
  for (i = 0; i < MAX_NUM_PLAYERS; i++) {
    check_vote(&votes[i]);
  }

  /* Update full turn info */
  for (i = 0; i < MAX_NUM_PLAYERS; i++) {
    votes[i].full_turn = TRUE;
  }
}

/**************************************************************************
  Deinitialize stuff related to this code module.
**************************************************************************/
void stdinhand_free(void)
{
  /* Nothing yet */
}

/**************************************************************************
  Return the access level of a command.
**************************************************************************/
static enum cmdlevel_id access_level(enum command_id cmd)
{
  if (server_state == PRE_GAME_STATE) {
    return commands[cmd].pregame_level;
  } else {
    return commands[cmd].game_level;
  }
}

/**************************************************************************
  Whether the caller can use the specified command. caller == NULL means 
  console.
**************************************************************************/
static bool may_use(struct connection *caller, enum command_id cmd)
{
  if (!caller) {
    return TRUE;  /* on the console, everything is allowed */
  }
  return caller->access_level >= access_level(cmd);
}

/**************************************************************************
  Whether the caller cannot use any commands at all.
  caller == NULL means console.
**************************************************************************/
static bool may_use_nothing(struct connection *caller)
{
  if (!caller) {
    return FALSE;  /* on the console, everything is allowed */
  }
  return (caller->access_level == ALLOW_NONE);
}

/**************************************************************************
  Whether the caller can set the specified option (assuming that
  the state of the game would allow changing the option at all).
  caller == NULL means console.
**************************************************************************/
static bool may_set_option(struct connection *caller, int option_idx)
{
  if (!caller) {
    return TRUE;  /* on the console, everything is allowed */
  } else {
    int level = caller->access_level;
    return ((level == ALLOW_HACK)
	    || (level >= access_level(CMD_SET) 
                && sset_is_to_client(option_idx)));
  }
}

/**************************************************************************
  Whether the caller can set the specified option, taking into account
  access, and the game state.  caller == NULL means console.
**************************************************************************/
static bool may_set_option_now(struct connection *caller, int option_idx)
{
  return (may_set_option(caller, option_idx)
	  && sset_is_changeable(option_idx));
}

/**************************************************************************
  Whether the caller can SEE the specified option.
  caller == NULL means console, which can see all.
  client players can see "to client" options, or if player
  has command access level to change option.
**************************************************************************/
static bool may_view_option(struct connection *caller, int option_idx)
{
  if (!caller) {
    return TRUE;  /* on the console, everything is allowed */
  } else {
    return sset_is_to_client(option_idx)
      || may_set_option(caller, option_idx);
  }
}

/**************************************************************************
  feedback related to server commands
  caller == NULL means console.
  No longer duplicate all output to console.

  This lowlevel function takes a single line; prefix is prepended to line.
**************************************************************************/
static void cmd_reply_line(enum command_id cmd, struct connection *caller,
			   enum rfc_status rfc_status, const char *prefix,
			   const char *line)
{
  const char *cmdname = cmd < CMD_NUM ? commands[cmd].name :
                  cmd == CMD_AMBIGUOUS ? _("(ambiguous)") :
                  cmd == CMD_UNRECOGNIZED ? _("(unknown)") :
			"(?!?)";  /* this case is a bug! */

  if (caller) {
    notify_conn(&caller->self, "/%s: %s%s", cmdname, prefix, line);
    /* cc: to the console - testing has proved it's too verbose - rp
    con_write(rfc_status, "%s/%s: %s%s", caller->name, cmdname, prefix, line);
    */
  } else {
    con_write(rfc_status, "%s%s", prefix, line);
  }

  if (rfc_status == C_OK) {
    conn_list_iterate(game.est_connections, pconn) {
      /* Do not tell caller, since he was told above! */
      if (pconn != caller) {
        notify_conn(&pconn->self, _("Game: %s"), line);
      }
    } conn_list_iterate_end;
  }
}

/**************************************************************************
  va_list version which allow embedded newlines, and each line is sent
  separately. 'prefix' is prepended to every line _after_ the first line.
**************************************************************************/
static void vcmd_reply_prefix(enum command_id cmd, struct connection *caller,
			      enum rfc_status rfc_status, const char *prefix,
			      const char *format, va_list ap)
{
  char buf[4096];
  char *c0, *c1;

  my_vsnprintf(buf, sizeof(buf), format, ap);

  c0 = buf;
  while ((c1=strstr(c0, "\n"))) {
    *c1 = '\0';
    cmd_reply_line(cmd, caller, rfc_status, (c0==buf?"":prefix), c0);
    c0 = c1+1;
  }
  cmd_reply_line(cmd, caller, rfc_status, (c0==buf?"":prefix), c0);
}

/**************************************************************************
  var-args version of above
  duplicate declaration required for attribute to work...
**************************************************************************/
static void cmd_reply_prefix(enum command_id cmd, struct connection *caller,
			     enum rfc_status rfc_status, const char *prefix,
			     const char *format, ...)
     fc__attribute((__format__ (__printf__, 5, 6)));
static void cmd_reply_prefix(enum command_id cmd, struct connection *caller,
			     enum rfc_status rfc_status, const char *prefix,
			     const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  vcmd_reply_prefix(cmd, caller, rfc_status, prefix, format, ap);
  va_end(ap);
}

/**************************************************************************
  var-args version as above, no prefix
**************************************************************************/
static void cmd_reply(enum command_id cmd, struct connection *caller,
		      enum rfc_status rfc_status, const char *format, ...)
     fc__attribute((__format__ (__printf__, 4, 5)));
static void cmd_reply(enum command_id cmd, struct connection *caller,
		      enum rfc_status rfc_status, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  vcmd_reply_prefix(cmd, caller, rfc_status, "", format, ap);
  va_end(ap);
}

/**************************************************************************
...
**************************************************************************/
static void cmd_reply_no_such_player(enum command_id cmd,
				     struct connection *caller,
				     char *name,
				     enum m_pre_result match_result)
{
  switch(match_result) {
  case M_PRE_EMPTY:
    cmd_reply(cmd, caller, C_SYNTAX,
	      _("Name is empty, so cannot be a player."));
    break;
  case M_PRE_LONG:
    cmd_reply(cmd, caller, C_SYNTAX,
	      _("Name is too long, so cannot be a player."));
    break;
  case M_PRE_AMBIGUOUS:
    cmd_reply(cmd, caller, C_FAIL,
	      _("Player name prefix '%s' is ambiguous."), name);
    break;
  case M_PRE_FAIL:
    cmd_reply(cmd, caller, C_FAIL,
	      _("No player by the name of '%s'."), name);
    break;
  default:
    cmd_reply(cmd, caller, C_FAIL,
	      _("Unexpected match_result %d (%s) for '%s'."),
	      match_result, _(m_pre_description(match_result)), name);
    freelog(LOG_ERROR,
	    "Unexpected match_result %d (%s) for '%s'.",
	    match_result, m_pre_description(match_result), name);
  }
}

/**************************************************************************
...
**************************************************************************/
static void cmd_reply_no_such_conn(enum command_id cmd,
				   struct connection *caller,
				   const char *name,
				   enum m_pre_result match_result)
{
  switch(match_result) {
  case M_PRE_EMPTY:
    cmd_reply(cmd, caller, C_SYNTAX,
	      _("Name is empty, so cannot be a connection."));
    break;
  case M_PRE_LONG:
    cmd_reply(cmd, caller, C_SYNTAX,
	      _("Name is too long, so cannot be a connection."));
    break;
  case M_PRE_AMBIGUOUS:
    cmd_reply(cmd, caller, C_FAIL,
	      _("Connection name prefix '%s' is ambiguous."), name);
    break;
  case M_PRE_FAIL:
    cmd_reply(cmd, caller, C_FAIL,
	      _("No connection by the name of '%s'."), name);
    break;
  default:
    cmd_reply(cmd, caller, C_FAIL,
	      _("Unexpected match_result %d (%s) for '%s'."),
	      match_result, _(m_pre_description(match_result)), name);
    freelog(LOG_ERROR,
	    "Unexpected match_result %d (%s) for '%s'.",
	    match_result, m_pre_description(match_result), name);
  }
}

/**************************************************************************
...
**************************************************************************/
static void open_metaserver_connection(struct connection *caller)
{
  server_open_meta();
  if (send_server_info_to_metaserver(META_INFO)) {
    notify_conn(NULL, _("Open metaserver connection to [%s]."),
		meta_addr_port());
  }
}

/**************************************************************************
...
**************************************************************************/
static void close_metaserver_connection(struct connection *caller)
{
  if (send_server_info_to_metaserver(META_GOODBYE)) {
    server_close_meta();
    notify_conn(NULL, _("Close metaserver connection to [%s]."),
		meta_addr_port());
  }
}

/**************************************************************************
...
**************************************************************************/
static bool metaconnection_command(struct connection *caller, char *arg, 
                                   bool check)
{
  if ((*arg == '\0') ||
      (0 == strcmp (arg, "?"))) {
    if (is_metaserver_open()) {
      cmd_reply(CMD_METACONN, caller, C_COMMENT,
		_("Metaserver connection is open."));
    } else {
      cmd_reply(CMD_METACONN, caller, C_COMMENT,
		_("Metaserver connection is closed."));
    }
  } else if ((0 == mystrcasecmp(arg, "u")) ||
	     (0 == mystrcasecmp(arg, "up"))) {
    if (!is_metaserver_open()) {
      if (!check) {
        open_metaserver_connection(caller);
      }
    } else {
      cmd_reply(CMD_METACONN, caller, C_METAERROR,
		_("Metaserver connection is already open."));
      return FALSE;
    }
  } else if ((0 == mystrcasecmp(arg, "d")) ||
	     (0 == mystrcasecmp(arg, "down"))) {
    if (is_metaserver_open()) {
      if (!check) {
        close_metaserver_connection(caller);
      }
    } else {
      cmd_reply(CMD_METACONN, caller, C_METAERROR,
		_("Metaserver connection is already closed."));
      return FALSE;
    }
  } else {
    cmd_reply(CMD_METACONN, caller, C_METAERROR,
	      _("Argument must be 'u', 'up', 'd', 'down', or '?'."));
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool metapatches_command(struct connection *caller, 
                                char *arg, bool check)
{
  if (check) {
    return TRUE;
  }

  set_meta_patches_string(arg);

  if (is_metaserver_open()) {
    send_server_info_to_metaserver(META_INFO);
    notify_conn(NULL, _("Metaserver patches string set to '%s'."), arg);
  } else {
    notify_conn(NULL, _("Metaserver patches string set to '%s', "
                          "not reporting to metaserver."), arg);
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool metatopic_command(struct connection *caller, char *arg, bool check)
{
  if (check) {
    return TRUE;
  }

  set_meta_topic_string(arg);
  if (is_metaserver_open()) {
    send_server_info_to_metaserver(META_INFO);
    notify_conn(NULL, _("Metaserver topic string set to '%s'."), arg);
  } else {
    notify_conn(NULL, _("Metaserver topic string set to '%s', "
                          "not reporting to metaserver."), arg);
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool metamessage_command(struct connection *caller, 
                                char *arg, bool check)
{
  if (check) {
    return TRUE;
  }

  set_user_meta_message_string(arg);
  if (is_metaserver_open()) {
    send_server_info_to_metaserver(META_INFO);
    notify_conn(NULL, _("Metaserver message string set to '%s'."), arg);
  } else {
    notify_conn(NULL, _("Metaserver message string set to '%s', "
			"not reporting to metaserver."), arg);
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool metaserver_command(struct connection *caller, char *arg, 
                               bool check)
{
  if (check) {
    return TRUE;
  }
  close_metaserver_connection(caller);

  sz_strlcpy(srvarg.metaserver_addr, arg);

  notify_conn(NULL, _("Metaserver is now [%s]."),
	      meta_addr_port());
  return TRUE;
}

/**************************************************************************
 Returns the serverid 
**************************************************************************/
static bool show_serverid(struct connection *caller, char *arg)
{
  cmd_reply(CMD_SRVID, caller, C_COMMENT, _("Server id: %s"), srvarg.serverid);

  return TRUE;
}

/***************************************************************
 This could be in common/player if the client ever gets
 told the ai player skill levels.
***************************************************************/
const char *name_of_skill_level(int level)
{
  const char *nm[11] = { "UNUSED", "away", "novice", "easy",
			 "UNKNOWN", "normal", "UNKNOWN", "hard",
			 "UNKNOWN", "UNKNOWN", "experimental" };
  
  assert(level>0 && level<=10);
  return nm[level];
}

/***************************************************************
...
***************************************************************/
static int handicap_of_skill_level(int level)
{
  int h[11] = { -1,
 /* away   */	H_AWAY  | H_RATES | H_TARGETS | H_HUTS | H_FOG | H_MAP
                        | H_REVOLUTION,
 /* novice */   H_RATES | H_TARGETS | H_HUTS | H_NOPLANES 
                        | H_DIPLOMAT | H_LIMITEDHUTS | H_DEFENSIVE
			| H_DIPLOMACY | H_REVOLUTION,
 /* easy */	H_RATES | H_TARGETS | H_HUTS | H_NOPLANES 
                        | H_DIPLOMAT | H_LIMITEDHUTS | H_DEFENSIVE,
		H_NONE,
 /* medium */	H_RATES | H_TARGETS | H_HUTS | H_DIPLOMAT,
		H_NONE,
 /* hard */	H_NONE,
		H_NONE,
		H_NONE,
 /* testing */	H_EXPERIMENTAL,
		};
  
  assert(level>0 && level<=10);
  return h[level];
}

/**************************************************************************
Return the AI fuzziness (0 to 1000) corresponding to a given skill
level (1 to 10).  See ai_fuzzy() in common/player.c
**************************************************************************/
static int fuzzy_of_skill_level(int level)
{
  int f[11] = { -1, 0, 400/*novice*/, 300/*easy*/, 0, 0, 0, 0, 0, 0, 0 };
  
  assert(level>0 && level<=10);
  return f[level];
}

/**************************************************************************
Return the AI's science development cost; a science development cost of 100
means that the AI develops science at the same speed as a human; a science
development cost of 200 means that the AI develops science at half the speed
of a human, and a sceence development cost of 50 means that the AI develops
science twice as fast as the human
**************************************************************************/
static int science_cost_of_skill_level(int level) 
{
  int x[11] = { -1, 100, 250/*novice*/, 100/*easy*/, 100, 100, 100, 100, 
                100, 100, 100 };
  
  assert(level>0 && level<=10);
  return x[level];
}

/**************************************************************************
Return the AI expansion tendency, a percentage factor to value new cities,
compared to defaults.  0 means _never_ build new cities, > 100 means to
(over?)value them even more than the default (already expansionistic) AI.
**************************************************************************/
static int expansionism_of_skill_level(int level)
{
  int x[11] = { -1, 100, 10/*novice*/, 10/*easy*/, 100, 100, 100, 100, 
                100, 100, 100 };
  
  assert(level>0 && level<=10);
  return x[level];
}

/**************************************************************************
For command "save foo";
Save the game, with filename=arg, provided server state is ok.
**************************************************************************/
static bool save_command(struct connection *caller, char *arg, bool check)
{
  if (server_state==SELECT_RACES_STATE) {
    cmd_reply(CMD_SAVE, caller, C_SYNTAX,
	      _("The game cannot be saved before it is started."));
    return FALSE;
  } else if (!check) {
    save_game(arg);
  }
  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
void toggle_ai_player_direct(struct connection *caller, struct player *pplayer)
{
  assert(pplayer != NULL);
  if (is_barbarian(pplayer)) {
    cmd_reply(CMD_AITOGGLE, caller, C_FAIL,
	      _("Cannot toggle a barbarian player."));
    return;
  }

  pplayer->ai.control = !pplayer->ai.control;
  if (pplayer->ai.control) {
    cmd_reply(CMD_AITOGGLE, caller, C_OK,
	      _("%s is now under AI control."), pplayer->name);
    if (pplayer->ai.skill_level==0) {
      pplayer->ai.skill_level = game.skill_level;
    }
    /* Set the skill level explicitly, because eg: the player skill
       level could have been set as AI, then toggled, then saved,
       then reloaded. */ 
    set_ai_level(caller, pplayer->name, pplayer->ai.skill_level, FALSE);
    /* the AI can't do active diplomacy */
    cancel_all_meetings(pplayer);
    /* The following is sometimes necessary to avoid using
       uninitialized data... */
    if (server_state == RUN_GAME_STATE) {
      assess_danger_player(pplayer);
    }
    /* In case this was last player who has not pressed turn done. */
    check_for_full_turn_done();
  } else {
    cmd_reply(CMD_AITOGGLE, caller, C_OK,
	      _("%s is now under human control."), pplayer->name);

    /* because the hard AI `cheats' with government rates but humans shouldn't */
    if (!game.is_new_game) {
      check_player_government_rates(pplayer);
    }
    /* Remove hidden dialogs from clients. This way the player can initiate
     * new meeting */
    cancel_all_meetings(pplayer);
  }

  if (server_state == RUN_GAME_STATE) {
    send_player_info(pplayer, NULL);
    gamelog(GAMELOG_PLAYER, pplayer);
  }
}

/**************************************************************************
...
**************************************************************************/
static bool toggle_ai_player(struct connection *caller, char *arg, bool check)
{
  enum m_pre_result match_result;
  struct player *pplayer;

  pplayer = find_player_by_name_prefix(arg, &match_result);

  if (!pplayer) {
    cmd_reply_no_such_player(CMD_AITOGGLE, caller, arg, match_result);
    return FALSE;
  } else if (!check) {
    toggle_ai_player_direct(caller, pplayer);
  }
  return TRUE;
}

/****************************************************************************
  Return the number of non-observer players.  game.nplayers includes
  observers so in some places this function should be called instead.
****************************************************************************/
static int get_num_nonobserver_players(void)
{
  int nplayers = 0;

  players_iterate(pplayer) {
    if (!pplayer->is_observer) {
      nplayers++;
    }
  } players_iterate_end;

  return nplayers;
}

/**************************************************************************
...
**************************************************************************/
static bool create_ai_player(struct connection *caller, char *arg, bool check)
{
  struct player *pplayer;
  PlayerNameStatus PNameStatus;
   
  if (server_state!=PRE_GAME_STATE)
  {
    cmd_reply(CMD_CREATE, caller, C_SYNTAX,
	      _("Can't add AI players once the game has begun."));
    return FALSE;
  }

  /* game.max_players is a limit on the number of non-observer players.
   * MAX_NUM_PLAYERS is a limit on all players. */
  if (get_num_nonobserver_players() >= game.max_players
      || game.nplayers >= MAX_NUM_PLAYERS) {
    cmd_reply(CMD_CREATE, caller, C_FAIL,
	      _("Can't add more players, server is full."));
    return FALSE;
  }

  if ((PNameStatus = test_player_name(arg)) == PNameEmpty)
  {
    cmd_reply(CMD_CREATE, caller, C_SYNTAX, _("Can't use an empty name."));
    return FALSE;
  }

  if (PNameStatus == PNameTooLong)
  {
    cmd_reply(CMD_CREATE, caller, C_SYNTAX,
	      _("That name exceeds the maximum of %d chars."), MAX_LEN_NAME-1);
    return FALSE;
  }

  if (PNameStatus == PNameIllegal)
  {
    cmd_reply(CMD_CREATE, caller, C_SYNTAX, _("That name is not allowed."));
    return FALSE;
  }       

  if ((pplayer=find_player_by_name(arg))) {
    cmd_reply(CMD_CREATE, caller, C_BOUNCE,
	      _("A player already exists by that name."));
    return FALSE;
  }

  if ((pplayer = find_player_by_user(arg))) {
    cmd_reply(CMD_CREATE, caller, C_BOUNCE,
              _("A user already exists by that name."));
    return FALSE;
  }
  if (check) {
    return TRUE;
  }

  pplayer = &game.players[game.nplayers];
  server_player_init(pplayer, FALSE);
  sz_strlcpy(pplayer->name, arg);
  sz_strlcpy(pplayer->username, ANON_USER_NAME);
  pplayer->was_created = TRUE; /* must use /remove explicitly to remove */

  game.nplayers++;

  notify_conn(NULL, _("Game: %s has been added as an AI-controlled player."),
	      arg);

  pplayer = find_player_by_name(arg);
  if (!pplayer)
  {
    cmd_reply(CMD_CREATE, caller, C_FAIL,
	      _("Error creating new AI player: %s."), arg);
    return FALSE;
  }

  pplayer->ai.control = TRUE;
  set_ai_level_directer(pplayer, game.skill_level);
  (void) send_server_info_to_metaserver(META_INFO);
  return TRUE;
}


/**************************************************************************
...
**************************************************************************/
static bool remove_player(struct connection *caller, char *arg, bool check)
{
  enum m_pre_result match_result;
  struct player *pplayer;
  char name[MAX_LEN_NAME];

  pplayer=find_player_by_name_prefix(arg, &match_result);
  
  if(!pplayer) {
    cmd_reply_no_such_player(CMD_REMOVE, caller, arg, match_result);
    return FALSE;
  }

  if (!(game.is_new_game && (server_state==PRE_GAME_STATE ||
			     server_state==SELECT_RACES_STATE))) {
    cmd_reply(CMD_REMOVE, caller, C_FAIL,
	      _("Players cannot be removed once the game has started."));
    return FALSE;
  }
  if (check) {
    return TRUE;
  }

  sz_strlcpy(name, pplayer->name);
  team_remove_player(pplayer);
  server_remove_player(pplayer);
  if (!caller || caller->used) {     /* may have removed self */
    cmd_reply(CMD_REMOVE, caller, C_OK,
	      _("Removed player %s from the game."), name);
  }
  return TRUE;
}

/**************************************************************************
  Returns FALSE iff there was an error.
**************************************************************************/
bool read_init_script(struct connection *caller, char *script_filename)
{
  FILE *script_file;
  char real_filename[1024];

  freelog(LOG_NORMAL, _("Loading script file: %s"), script_filename);
  
  interpret_tilde(real_filename, sizeof(real_filename), script_filename);
 
  if (is_reg_file_for_access(real_filename, FALSE)
      && (script_file = fopen(real_filename, "r"))) {
    char buffer[MAX_LEN_CONSOLE_LINE];
    /* the size is set as to not overflow buffer in handle_stdin_input */
    while(fgets(buffer,MAX_LEN_CONSOLE_LINE-1,script_file))
      handle_stdin_input((struct connection *)NULL, buffer, FALSE);
    fclose(script_file);
    return TRUE;
  } else {
    cmd_reply(CMD_READ_SCRIPT, caller, C_FAIL,
	_("Cannot read command line scriptfile '%s'."), real_filename);
    freelog(LOG_ERROR,
	_("Could not read script file '%s'."), real_filename);
    return FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
static bool read_command(struct connection *caller, char *arg, bool check)
{
  if (check) {
    return TRUE; /* FIXME: no actual checks done */
  }
  /* warning: there is no recursion check! */
  return read_init_script(caller, arg);
}

/**************************************************************************
...
(Should this take a 'caller' argument for output? --dwp)
**************************************************************************/
static void write_init_script(char *script_filename)
{
  char real_filename[1024];
  FILE *script_file;
  
  interpret_tilde(real_filename, sizeof(real_filename), script_filename);

  if (is_reg_file_for_access(real_filename, TRUE)
      && (script_file = fopen(real_filename, "w"))) {

    int i;

    fprintf(script_file,
	"#FREECIV SERVER COMMAND FILE, version %s\n", VERSION_STRING);
    fputs("# These are server options saved from a running civserver.\n",
	script_file);

    /* first, some state info from commands (we can't save everything) */

    fprintf(script_file, "cmdlevel %s new\n",
	cmdlevel_name(default_access_level));

    fprintf(script_file, "cmdlevel %s first\n",
	cmdlevel_name(first_access_level));

    fprintf(script_file, "%s\n",
        (game.skill_level == 1) ?       "away" :
	(game.skill_level == 2) ?	"novice" :
	(game.skill_level == 3) ?	"easy" :
	(game.skill_level == 5) ?	"medium" :
	(game.skill_level < 10) ?	"hard" :
					"experimental");

    if (*srvarg.metaserver_addr != '\0' &&
	((0 != strcmp(srvarg.metaserver_addr, DEFAULT_META_SERVER_ADDR)))) {
      fprintf(script_file, "metaserver %s\n", meta_addr_port());
    }

    if (0 != strcmp(get_meta_patches_string(), default_meta_patches_string())) {
      fprintf(script_file, "metapatches %s\n", get_meta_patches_string());
    }
    if (0 != strcmp(get_meta_topic_string(), default_meta_topic_string())) {
      fprintf(script_file, "metatopic %s\n", get_meta_topic_string());
    }
    if (0 != strcmp(get_meta_message_string(), default_meta_message_string())) {
      fprintf(script_file, "metamessage %s\n", get_meta_message_string());
    }

    /* then, the 'set' option settings */

    for (i=0;settings[i].name;i++) {
      struct settings_s *op = &settings[i];

      switch (op->type) {
      case SSET_INT:
	fprintf(script_file, "set %s %i\n", op->name, *op->int_value);
	break;
      case SSET_BOOL:
	fprintf(script_file, "set %s %i\n", op->name,
		(*op->bool_value) ? 1 : 0);
	break;
      case SSET_STRING:
	fprintf(script_file, "set %s %s\n", op->name, op->string_value);
	break;
      }
    }

    /* rulesetdir */
    fprintf(script_file, "rulesetdir %s\n", game.rulesetdir);

    fclose(script_file);

  } else {
    freelog(LOG_ERROR,
	_("Could not write script file '%s'."), real_filename);
  }
}

/**************************************************************************
...
('caller' argument is unused)
**************************************************************************/
static bool write_command(struct connection *caller, char *arg, bool check)
{
  if (!check) {
    write_init_script(arg);
  }
  return TRUE;
}

/**************************************************************************
 set ptarget's cmdlevel to level if caller is allowed to do so
**************************************************************************/
static bool set_cmdlevel(struct connection *caller,
			struct connection *ptarget,
			enum cmdlevel_id level)
{
  assert(ptarget != NULL);    /* only ever call me for specific connection */

  if (caller && ptarget->access_level > caller->access_level) {
    /*
     * This command is intended to be used at ctrl access level
     * and thus this if clause is needed.
     * (Imagine a ctrl level access player that wants to change
     * access level of a hack level access player)
     * At the moment it can be used only by hack access level 
     * and thus this clause is never used.
     */
    cmd_reply(CMD_CMDLEVEL, caller, C_FAIL,
	      _("Cannot decrease command access level '%s' for connection '%s';"
		" you only have '%s'."),
	      cmdlevel_name(ptarget->access_level),
	      ptarget->username,
	      cmdlevel_name(caller->access_level));
    return FALSE;
  } else {
    ptarget->access_level = level;
    return TRUE;
  }
}

/********************************************************************
  Returns true if there is at least one established connection.
*********************************************************************/
static bool a_connection_exists(void)
{
  return conn_list_size(&game.est_connections) > 0;
}

/********************************************************************
...
*********************************************************************/
static bool first_access_level_is_taken(void)
{
  conn_list_iterate(game.est_connections, pconn) {
    if (pconn->access_level >= first_access_level) {
      return TRUE;
    }
  }
  conn_list_iterate_end;
  return FALSE;
}

/********************************************************************
...
*********************************************************************/
enum cmdlevel_id access_level_for_next_connection(void)
{
  if ((first_access_level > default_access_level)
			&& !a_connection_exists()) {
    return first_access_level;
  } else {
    return default_access_level;
  }
}

/********************************************************************
...
*********************************************************************/
void notify_if_first_access_level_is_available(void)
{
  if (first_access_level > default_access_level
      && !first_access_level_is_taken()) {
    notify_conn(NULL, _("Game: Anyone can assume command access level "
			"'%s' now by issuing the 'firstlevel' command."),
		cmdlevel_name(first_access_level));
  }
}

/**************************************************************************
 Change command access level for individual player, or all, or new.
**************************************************************************/
static bool cmdlevel_command(struct connection *caller, char *str, bool check)
{
  char arg_level[MAX_LEN_CONSOLE_LINE]; /* info, ctrl etc */
  char arg_name[MAX_LEN_CONSOLE_LINE];	 /* a player name, or "new" */
  char *cptr_s, *cptr_d;	 /* used for string ops */

  enum m_pre_result match_result;
  enum cmdlevel_id level;
  struct connection *ptarget;

  /* find the start of the level: */
  for (cptr_s = str; *cptr_s != '\0' && !my_isalnum(*cptr_s); cptr_s++) {
    /* nothing */
  }

  /* copy the level into arg_level[] */
  for(cptr_d=arg_level; *cptr_s != '\0' && my_isalnum(*cptr_s); cptr_s++, cptr_d++) {
    *cptr_d=*cptr_s;
  }
  *cptr_d='\0';
  
  if (arg_level[0] == '\0') {
    /* no level name supplied; list the levels */

    cmd_reply(CMD_CMDLEVEL, caller, C_COMMENT, _("Command access levels in effect:"));

    conn_list_iterate(game.est_connections, pconn) {
      cmd_reply(CMD_CMDLEVEL, caller, C_COMMENT, "cmdlevel %s %s",
		cmdlevel_name(pconn->access_level), pconn->username);
    }
    conn_list_iterate_end;
    cmd_reply(CMD_CMDLEVEL, caller, C_COMMENT,
	      _("Command access level for new connections: %s"),
	      cmdlevel_name(default_access_level));
    cmd_reply(CMD_CMDLEVEL, caller, C_COMMENT,
	      _("Command access level for first player to take it: %s"),
	      cmdlevel_name(first_access_level));
    return TRUE;
  }

  /* a level name was supplied; set the level */

  if ((level = cmdlevel_named(arg_level)) == ALLOW_UNRECOGNIZED) {
    cmd_reply(CMD_CMDLEVEL, caller, C_SYNTAX,
	      _("Error: command access level must be one of"
		" 'none', 'info', 'ctrl', or 'hack'."));
    return FALSE;
  } else if (caller && level > caller->access_level) {
    cmd_reply(CMD_CMDLEVEL, caller, C_FAIL,
	      _("Cannot increase command access level to '%s';"
		" you only have '%s' yourself."),
	      arg_level, cmdlevel_name(caller->access_level));
    return FALSE;
  }
  if (check) {
    return TRUE; /* looks good */
  }

  /* find the start of the name: */
  for (; *cptr_s != '\0' && !my_isalnum(*cptr_s); cptr_s++) {
    /* nothing */
  }

  /* copy the name into arg_name[] */
  for(cptr_d=arg_name;
      *cptr_s != '\0' && (*cptr_s == '-' || *cptr_s == ' ' || my_isalnum(*cptr_s));
      cptr_s++ , cptr_d++) {
    *cptr_d=*cptr_s;
  }
  *cptr_d='\0';
 
  if (arg_name[0] == '\0') {
    /* no playername supplied: set for all connections, and set the default */
    conn_list_iterate(game.est_connections, pconn) {
      if (set_cmdlevel(caller, pconn, level)) {
	cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		  _("Command access level set to '%s' for connection %s."),
		  cmdlevel_name(level), pconn->username);
      } else {
	cmd_reply(CMD_CMDLEVEL, caller, C_FAIL,
		  _("Command access level could not be set to '%s' for "
		    "connection %s."),
		  cmdlevel_name(level), pconn->username);
        return FALSE;
      }
    }
    conn_list_iterate_end;
    
    default_access_level = level;
    cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("Command access level set to '%s' for new players."),
		cmdlevel_name(level));
    first_access_level = level;
    cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("Command access level set to '%s' for first player to grab it."),
		cmdlevel_name(level));
  }
  else if (strcmp(arg_name,"new") == 0) {
    default_access_level = level;
    cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("Command access level set to '%s' for new players."),
		cmdlevel_name(level));
    if (level > first_access_level) {
      first_access_level = level;
      cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("Command access level set to '%s' for first player to grab it."),
		cmdlevel_name(level));
    }
  }
  else if (strcmp(arg_name,"first") == 0) {
    first_access_level = level;
    cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("Command access level set to '%s' for first player to grab it."),
		cmdlevel_name(level));
    if (level < default_access_level) {
      default_access_level = level;
      cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("Command access level set to '%s' for new players."),
		cmdlevel_name(level));
    }
  }
  else if ((ptarget = find_conn_by_user_prefix(arg_name, &match_result))) {
    if (set_cmdlevel(caller, ptarget, level)) {
      cmd_reply(CMD_CMDLEVEL, caller, C_OK,
		_("Command access level set to '%s' for connection %s."),
		cmdlevel_name(level), ptarget->username);
    } else {
      cmd_reply(CMD_CMDLEVEL, caller, C_FAIL,
		_("Command access level could not be set to '%s'"
		  " for connection %s."),
		cmdlevel_name(level), ptarget->username);
      return FALSE;
    }
  } else {
    cmd_reply_no_such_conn(CMD_CMDLEVEL, caller, arg_name, match_result);
    return FALSE;
  }
  return TRUE;
}

/**************************************************************************
 This special command to set the command access level is not included into
 cmdlevel_command because of its lower access level: it can be used
 to promote one's own connection to 'first come' cmdlevel if that isn't
 already taken.
 **************************************************************************/
static bool firstlevel_command(struct connection *caller, bool check)
{
  if (!caller) {
    cmd_reply(CMD_FIRSTLEVEL, caller, C_FAIL,
	_("The 'firstlevel' command makes no sense from the server command line."));
    return FALSE;
  } else if (caller->access_level >= first_access_level) {
    cmd_reply(CMD_FIRSTLEVEL, caller, C_FAIL,
	_("You already have command access level '%s' or better."),
		cmdlevel_name(first_access_level));
    return FALSE;
  } else if (first_access_level_is_taken()) {
    cmd_reply(CMD_FIRSTLEVEL, caller, C_FAIL,
	_("Someone else already has command access level '%s' or better."),
		cmdlevel_name(first_access_level));
    return FALSE;
  } else if (!check) {
    caller->access_level = first_access_level;
    cmd_reply(CMD_FIRSTLEVEL, caller, C_OK,
	_("Command access level '%s' has been grabbed by connection %s."),
		cmdlevel_name(first_access_level),
		caller->username);
  }
  return TRUE;
}


/**************************************************************************
  Returns possible parameters for the commands that take server options
  as parameters (CMD_EXPLAIN and CMD_SET).
**************************************************************************/
static const char *optname_accessor(int i)
{
  return settings[i].name;
}

#if defined HAVE_LIBREADLINE || defined HAVE_NEWLIBREADLINE
/**************************************************************************
  Returns possible parameters for the /show command.
**************************************************************************/
static const char *olvlname_accessor(int i)
{
  /* for 0->4, uses option levels, otherwise returns a setting name */
  if (i < OLEVELS_NUM) {
    return sset_level_names[i];
  } else {
    return settings[i-OLEVELS_NUM].name;
  }
}
#endif

/**************************************************************************
  Set timeout options.
**************************************************************************/
static bool timeout_command(struct connection *caller, char *str, bool check)
{
  char buf[MAX_LEN_CONSOLE_LINE];
  char *arg[4];
  int i = 0, ntokens;
  int *timeouts[4];

  timeouts[0] = &game.timeoutint;
  timeouts[1] = &game.timeoutintinc;
  timeouts[2] = &game.timeoutinc;
  timeouts[3] = &game.timeoutincmult;

  sz_strlcpy(buf, str);
  ntokens = get_tokens(buf, arg, 4, TOKEN_DELIMITERS);

  for (i = 0; i < ntokens; i++) {
    if (sscanf(arg[i], "%d", timeouts[i]) != 1) {
      cmd_reply(CMD_TIMEOUT, caller, C_FAIL, _("Invalid argument %d."),
		i + 1);
    }
    free(arg[i]);
  }

  if (ntokens == 0) {
    cmd_reply(CMD_TIMEOUT, caller, C_SYNTAX, _("Usage: timeoutincrease "
					       "<turn> <turnadd> "
					       "<value> <valuemult>."));
    return FALSE;
  } else if (check) {
    return TRUE;
  }

  cmd_reply(CMD_TIMEOUT, caller, C_OK, _("Dynamic timeout set to "
					 "%d %d %d %d"),
	    game.timeoutint, game.timeoutintinc,
	    game.timeoutinc, game.timeoutincmult);

  /* if we set anything here, reset the counter */
  game.timeoutcounter = 1;
  return TRUE;
}

/**************************************************************************
Find option level number by name.
**************************************************************************/
static enum sset_level lookup_option_level(const char *name)
{
  enum sset_level i;

  for (i = SSET_ALL; i <= SSET_RARE; i++) {
    if (0 == mystrcasecmp(name, sset_level_names[i])) {
      return i;
    }
  }

  return SSET_NONE;
}

/**************************************************************************
Find option index by name. Return index (>=0) on success, -1 if no
suitable options were found, -2 if several matches were found.
**************************************************************************/
static int lookup_option(const char *name)
{
  enum m_pre_result result;
  int ind;

  /* Check for option levels, first off */
  if (lookup_option_level(name) != SSET_NONE) {
    return -3;
  }

  result = match_prefix(optname_accessor, SETTINGS_NUM, 0, mystrncasecmp,
			name, &ind);

  return ((result < M_PRE_AMBIGUOUS) ? ind :
	  (result == M_PRE_AMBIGUOUS) ? -2 : -1);
}

/**************************************************************************
 Show the caller detailed help for the single OPTION given by id.
 help_cmd is the command the player used.
 Only show option values for options which the caller can SEE.
**************************************************************************/
static void show_help_option(struct connection *caller,
			     enum command_id help_cmd,
			     int id)
{
  struct settings_s *op = &settings[id];

  if (op->short_help) {
    cmd_reply(help_cmd, caller, C_COMMENT,
	      "%s %s  -  %s", _("Option:"), op->name, _(op->short_help));
  } else {
    cmd_reply(help_cmd, caller, C_COMMENT,
	      "%s %s", _("Option:"), op->name);
  }

  if(op->extra_help && strcmp(op->extra_help,"")!=0) {
    static struct astring abuf = ASTRING_INIT;
    const char *help = _(op->extra_help);

    astr_minsize(&abuf, strlen(help)+1);
    strcpy(abuf.str, help);
    wordwrap_string(abuf.str, 76);
    cmd_reply(help_cmd, caller, C_COMMENT, _("Description:"));
    cmd_reply_prefix(help_cmd, caller, C_COMMENT,
		     "  ", "  %s", abuf.str);
  }
  cmd_reply(help_cmd, caller, C_COMMENT,
	    _("Status: %s"), (sset_is_changeable(id)
				  ? _("changeable") : _("fixed")));
  
  if (may_view_option(caller, id)) {
    switch (op->type) {
    case SSET_BOOL:
      cmd_reply(help_cmd, caller, C_COMMENT,
		_("Value: %d, Minimum: 0, Default: %d, Maximum: 1"),
		(*(op->bool_value)) ? 1 : 0, op->bool_default_value ? 1 : 0);
      break;
    case SSET_INT:
      cmd_reply(help_cmd, caller, C_COMMENT,
		_("Value: %d, Minimum: %d, Default: %d, Maximum: %d"),
		*(op->int_value), op->int_min_value, op->int_default_value,
		op->int_max_value);
      break;
    case SSET_STRING:
      cmd_reply(help_cmd, caller, C_COMMENT,
		_("Value: \"%s\", Default: \"%s\""), op->string_value,
		op->string_default_value);
      break;
    }
  }
}

/**************************************************************************
 Show the caller list of OPTIONS.
 help_cmd is the command the player used.
 Only show options which the caller can SEE.
**************************************************************************/
static void show_help_option_list(struct connection *caller,
				  enum command_id help_cmd)
{
  int i, j;
  
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
  cmd_reply(help_cmd, caller, C_COMMENT,
	    _("Explanations are available for the following server options:"));
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
  if(!caller && con_get_style()) {
    for (i=0; settings[i].name; i++) {
      cmd_reply(help_cmd, caller, C_COMMENT, "%s", settings[i].name);
    }
  } else {
    char buf[MAX_LEN_CONSOLE_LINE];
    buf[0] = '\0';
    for (i=0, j=0; settings[i].name; i++) {
      if (may_view_option(caller, i)) {
	cat_snprintf(buf, sizeof(buf), "%-19s", settings[i].name);
	if ((++j % 4) == 0) {
	  cmd_reply(help_cmd, caller, C_COMMENT, buf);
	  buf[0] = '\0';
	}
      }
    }
    if (buf[0] != '\0')
      cmd_reply(help_cmd, caller, C_COMMENT, buf);
  }
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
}

/**************************************************************************
 ...
**************************************************************************/
static bool explain_option(struct connection *caller, char *str, bool check)
{
  char command[MAX_LEN_CONSOLE_LINE], *cptr_s, *cptr_d;
  int cmd;

  for (cptr_s = str; *cptr_s != '\0' && !my_isalnum(*cptr_s); cptr_s++) {
    /* nothing */
  }
  for (cptr_d = command; *cptr_s != '\0' && my_isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  if (*command != '\0') {
    cmd=lookup_option(command);
    if (cmd >= 0 && cmd < SETTINGS_NUM) {
      show_help_option(caller, CMD_EXPLAIN, cmd);
    } else if (cmd == -1 || cmd == -3) {
      cmd_reply(CMD_EXPLAIN, caller, C_FAIL,
		_("No explanation for that yet."));
      return FALSE;
    } else if (cmd == -2) {
      cmd_reply(CMD_EXPLAIN, caller, C_FAIL, _("Ambiguous option name."));
      return FALSE;
    } else {
      freelog(LOG_ERROR, "Unexpected case %d in %s line %d",
	      cmd, __FILE__, __LINE__);
      return FALSE;
    }
  } else {
    show_help_option_list(caller, CMD_EXPLAIN);
  }
  return TRUE;
}

/******************************************************************
  Send a message to all players
******************************************************************/
static bool wall(char *str, bool check)
{
  if (!check) {
    notify_conn_ex(&game.game_connections, NULL, E_MESSAGE_WALL,
 		   _("Server Operator: %s"), str);
  }
  return TRUE;
}
  
/******************************************************************
Send a report with server options to specified connections.
"which" should be one of:
1: initial options only
2: ongoing options only 
(which=0 means all options; this is now obsolete and no longer used.)
******************************************************************/
void report_server_options(struct conn_list *dest, int which)
{
  int i, c;
  char buffer[4096];
  char title[128];
  const char *caption;

  buffer[0] = '\0';
  my_snprintf(title, sizeof(title), _("%-20svalue  (min , max)"), _("Option"));
  caption = (which == 1) ?
    _("Server Options (initial)") :
    _("Server Options (ongoing)");

  for (c = 0; c < SSET_NUM_CATEGORIES; c++) {
    cat_snprintf(buffer, sizeof(buffer), "%s:\n", sset_category_names[c]);
    for (i = 0; settings[i].name; i++) {
      struct settings_s *op = &settings[i];
      if (!sset_is_to_client(i)) {
        continue;
      }
      if (which == 1 && op->sclass > SSET_GAME_INIT) {
        continue;
      }
      if (which == 2 && op->sclass <= SSET_GAME_INIT) {
        continue;
      }
      if (op->category != c) {
        continue;
      }
      switch (op->type) {
      case SSET_BOOL:
	cat_snprintf(buffer, sizeof(buffer), "%-20s%c%-6d (0,1)\n",
		     op->name,
		     (*op->bool_value == op->bool_default_value) ? '*' : ' ',
		     *op->bool_value);
	break;
	
      case SSET_INT:
	cat_snprintf(buffer, sizeof(buffer), "%-20s%c%-6d (%d,%d)\n", op->name,
		     (*op->int_value == op->int_default_value) ? '*' : ' ',
		     *op->int_value, op->int_min_value, op->int_max_value);
	break;
      case SSET_STRING:
	cat_snprintf(buffer, sizeof(buffer), "%-20s%c\"%s\"\n", op->name,
		     (strcmp(op->string_value,
			     op->string_default_value) == 0) ? '*' : ' ',
		     op->string_value);
	break;
      }
    }
    cat_snprintf(buffer, sizeof(buffer), "\n");
  }
  freelog(LOG_DEBUG, "report_server_options buffer len %d", i);
  page_conn(dest, caption, title, buffer);
}

/******************************************************************
  Deliver options to the client for setting

  which == 1 : REPORT_SERVER_OPTIONS1
  which == 2 : REPORT_SERVER_OPTIONS2
******************************************************************/
void report_settable_server_options(struct connection *dest, int which)
{
  struct packet_options_settable_control control;
  struct packet_options_settable packet;
  int i, s = 0;

  if (dest->access_level == ALLOW_NONE
      || (which == 1 && server_state > PRE_GAME_STATE)) {
    report_server_options(&dest->self, which);
    return;
  }

  memset(&control, 0, sizeof(struct packet_options_settable_control));

  /* count the number of settings */
  for (i = 0; settings[i].name; i++) {
    if (!sset_is_changeable(i)) {
      continue;
    }
    if (settings[i].to_client == SSET_SERVER_ONLY
	&& dest->access_level != ALLOW_HACK) {
      continue;
    }
    s++;
  }

  control.nids = s;

  /* fill in the category strings */
  control.ncategories = SSET_NUM_CATEGORIES;
  for (i = 0; i < SSET_NUM_CATEGORIES; i++) {
    strcpy(control.category_names[i], sset_category_names[i]);
  }

  /* send off the control packet */
  send_packet_options_settable_control(dest, &control);

  for (s = 0, i = 0; settings[i].name; i++) {
    if (!sset_is_changeable(i)) {
      continue;
    }
    if (settings[i].to_client == SSET_SERVER_ONLY
	&& dest->access_level != ALLOW_HACK) {
      continue;
    }
    memset(&packet, 0, sizeof(packet));

    packet.id = s++;
    sz_strlcpy(packet.name, settings[i].name);
    sz_strlcpy(packet.short_help, settings[i].short_help);
    sz_strlcpy(packet.extra_help, settings[i].extra_help);

    packet.category = settings[i].category;
    packet.type = settings[i].type;

    if (settings[i].type == SSET_STRING) {
      strcpy(packet.strval, settings[i].string_value);
      strcpy(packet.default_strval, settings[i].string_default_value);
    } else if (settings[i].type == SSET_BOOL) {
      packet.val = *(settings[i].bool_value);
      packet.default_val = settings[i].bool_default_value;
    } else {
      packet.min = settings[i].int_min_value;
      packet.max = settings[i].int_max_value;
      packet.val = *(settings[i].int_value);
      packet.default_val = settings[i].int_default_value;
    }

    send_packet_options_settable(dest, &packet);
  }
}

/******************************************************************
  Set an AI level and related quantities, with no feedback.
******************************************************************/
void set_ai_level_directer(struct player *pplayer, int level)
{
  pplayer->ai.handicap = handicap_of_skill_level(level);
  pplayer->ai.fuzzy = fuzzy_of_skill_level(level);
  pplayer->ai.expand = expansionism_of_skill_level(level);
  pplayer->ai.science_cost = science_cost_of_skill_level(level);
  pplayer->ai.skill_level = level;
}

/******************************************************************
  Translate an AI level back to its CMD_* value.
  If we just used /set ailevel <num> we wouldn't have to do this - rp
******************************************************************/
static enum command_id cmd_of_level(int level)
{
  switch(level) {
    case 1 : return CMD_AWAY;
    case 2 : return CMD_NOVICE;
    case 3 : return CMD_EASY;
    case 5 : return CMD_NORMAL;
    case 7 : return CMD_HARD;
    case 10 : return CMD_EXPERIMENTAL;
  }
  assert(FALSE);
  return CMD_NORMAL; /* to satisfy compiler */
}

/******************************************************************
  Set an AI level from the server prompt.
******************************************************************/
void set_ai_level_direct(struct player *pplayer, int level)
{
  set_ai_level_directer(pplayer,level);
  cmd_reply(cmd_of_level(level), NULL, C_OK,
	_("Player '%s' now has AI skill level '%s'."),
	pplayer->name, name_of_skill_level(level));
  
}

/******************************************************************
  Handle a user command to set an AI level.
******************************************************************/
static bool set_ai_level(struct connection *caller, char *name, 
                         int level, bool check)
{
  enum m_pre_result match_result;
  struct player *pplayer;

  assert(level > 0 && level < 11);

  pplayer=find_player_by_name_prefix(name, &match_result);

  if (pplayer) {
    if (pplayer->ai.control) {
      if (check) {
        return TRUE;
      }
      set_ai_level_directer(pplayer, level);
      cmd_reply(cmd_of_level(level), caller, C_OK,
		_("Player '%s' now has AI skill level '%s'."),
		pplayer->name, name_of_skill_level(level));
    } else {
      cmd_reply(cmd_of_level(level), caller, C_FAIL,
		_("%s is not controlled by the AI."), pplayer->name);
      return FALSE;
    }
  } else if(match_result == M_PRE_EMPTY) {
    if (check) {
      return TRUE;
    }
    players_iterate(pplayer) {
      if (pplayer->ai.control) {
	set_ai_level_directer(pplayer, level);
        cmd_reply(cmd_of_level(level), caller, C_OK,
		_("Player '%s' now has AI skill level '%s'."),
		pplayer->name, name_of_skill_level(level));
      }
    } players_iterate_end;
    game.skill_level = level;
    cmd_reply(cmd_of_level(level), caller, C_OK,
		_("Default AI skill level set to '%s'."),
		name_of_skill_level(level));
  } else {
    cmd_reply_no_such_player(cmd_of_level(level), caller, name, match_result);
    return FALSE;
  }
  return TRUE;
}

/******************************************************************
  Set user to away mode.
******************************************************************/
static bool set_away(struct connection *caller, char *name, bool check)
{
  if (caller == NULL) {
    cmd_reply(CMD_AWAY, caller, C_FAIL, _("This command is client only."));
    return FALSE;
  } else if (name && strlen(name) > 0) {
    notify_conn(&caller->self, _("Usage: away"));
    return FALSE;
  } else if (!caller->player || caller->observer) {
    /* This happens for detached or observer connections. */
    notify_conn(&caller->self, _("Only players may use the away command."));
    return FALSE;
  } else if (!caller->player->ai.control && !check) {
    notify_conn(&game.est_connections, _("%s set to away mode."), 
                caller->player->name);
    set_ai_level_directer(caller->player, 1);
    caller->player->ai.control = TRUE;
    cancel_all_meetings(caller->player);
  } else if (!check) {
    notify_conn(&game.est_connections, _("%s returned to game."), 
                caller->player->name);
    caller->player->ai.control = FALSE;
    /* We have to do it, because the client doesn't display 
     * dialogs for meetings in AI mode. */
    cancel_all_meetings(caller->player);
  }
  return TRUE;
}

/******************************************************************
Print a summary of the settings and their values.
Note that most values are at most 4 digits, except seeds,
which we let overflow their columns, plus a sign character.
Only show options which the caller can SEE.
******************************************************************/
static bool show_command(struct connection *caller, char *str, bool check)
{
  char buf[MAX_LEN_CONSOLE_LINE];
  char command[MAX_LEN_CONSOLE_LINE], *cptr_s, *cptr_d;
  int cmd,i,len1;
  enum sset_level level = SSET_VITAL;
  size_t clen = 0;

  for (cptr_s = str; *cptr_s != '\0' && !my_isalnum(*cptr_s); cptr_s++) {
    /* nothing */
  }
  for (cptr_d = command; *cptr_s != '\0' && my_isalnum(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  if (*command != '\0') {
    /* In "/show forests", figure out that it's the forests option we're
     * looking at. */
    cmd=lookup_option(command);
    if (cmd >= 0) {
      /* Ignore levels when a particular option is specified. */
      level = SSET_NONE;

      if (!may_view_option(caller, cmd)) {
        cmd_reply(CMD_SHOW, caller, C_FAIL,
		  _("Sorry, you do not have access to view option '%s'."),
		  command);
        return FALSE;
      }
    }
    if (cmd == -1) {
      cmd_reply(CMD_SHOW, caller, C_FAIL, _("Unknown option '%s'."), command);
      return FALSE;
    }
    if (cmd == -2) {
      /* allow ambiguous: show all matching */
      clen = strlen(command);
    }
    if (cmd == -3) {
      /* Option level */
      level = lookup_option_level(command);
    }
  } else {
   cmd = -1;  /* to indicate that no comannd was specified */
  }

#define cmd_reply_show(string)  cmd_reply(CMD_SHOW, caller, C_COMMENT, string)

#define OPTION_NAME_SPACE 13
  /* under SSET_MAX_LEN, so it fits into 80 cols more easily - rp */

  cmd_reply_show(horiz_line);
  switch(level) {
    case SSET_NONE:
      break;
    case SSET_ALL:
      cmd_reply_show(_("All options"));
      break;
    case SSET_VITAL:
      cmd_reply_show(_("Vital options"));
      break;
    case SSET_SITUATIONAL:
      cmd_reply_show(_("Situational options"));
      break;
    case SSET_RARE:
      cmd_reply_show(_("Rarely used options"));
      break;
  }
  cmd_reply_show(_("+ means you may change the option"));
  cmd_reply_show(_("= means the option is on its default value"));
  cmd_reply_show(horiz_line);
  len1 = my_snprintf(buf, sizeof(buf),
	_("%-*s value   (min,max)      "), OPTION_NAME_SPACE, _("Option"));
  if (len1 == -1)
    len1 = sizeof(buf) -1;
  sz_strlcat(buf, _("description"));
  cmd_reply_show(buf);
  cmd_reply_show(horiz_line);

  buf[0] = '\0';

  for (i = 0; settings[i].name; i++) {
    if (may_view_option(caller, i)
	&& (cmd == -1 || cmd == -3 || cmd == i
	    || (cmd == -2
		&& mystrncasecmp(settings[i].name, command, clen) == 0))) {
      /* in the cmd==i case, this loop is inefficient. never mind - rp */
      struct settings_s *op = &settings[i];
      int len = 0;

      if (level == SSET_ALL || op->level == level || cmd >= 0) {
        switch (op->type) {
        case SSET_BOOL:
	  len = my_snprintf(buf, sizeof(buf),
			    "%-*s %c%c%-5d (0,1)", OPTION_NAME_SPACE, op->name,
			    may_set_option_now(caller, i) ? '+' : ' ',
			    ((*op->bool_value == op->bool_default_value) ?
			     '=' : ' '), (*op->bool_value) ? 1 : 0);
	  break;

        case SSET_INT:
	  len = my_snprintf(buf, sizeof(buf),
			    "%-*s %c%c%-5d (%d,%d)", OPTION_NAME_SPACE,
			    op->name, may_set_option_now(caller,
						         i) ? '+' : ' ',
			    ((*op->int_value == op->int_default_value) ?
			     '=' : ' '),
			    *op->int_value, op->int_min_value,
			    op->int_max_value);
	  break;

        case SSET_STRING:
	  len = my_snprintf(buf, sizeof(buf),
			    "%-*s %c%c\"%s\"", OPTION_NAME_SPACE, op->name,
			    may_set_option_now(caller, i) ? '+' : ' ',
			    ((strcmp(op->string_value,
				     op->string_default_value) == 0) ?
			     '=' : ' '), op->string_value);
	  break;
        }
        if (len == -1) {
          len = sizeof(buf) - 1;
        }
        /* Line up the descriptions: */
        if (len < len1) {
          cat_snprintf(buf, sizeof(buf), "%*s", (len1-len), " ");
        } else {
          sz_strlcat(buf, " ");
        }
        sz_strlcat(buf, _(op->short_help));
        cmd_reply_show(buf);
      }
    }
  }
  cmd_reply_show(horiz_line);
  if (level == SSET_VITAL) {
    cmd_reply_show(_("Try 'show situational' or 'show rare' to show "
		     "more options"));
    cmd_reply_show(horiz_line);
  }
  return TRUE;
#undef cmd_reply_show
#undef OPTION_NAME_SPACE
}

/******************************************************************
  Which characters are allowed within option names: (for 'set')
******************************************************************/
static bool is_ok_opt_name_char(char c)
{
  return my_isalnum(c);
}

/******************************************************************
  Which characters are allowed within option values: (for 'set')
******************************************************************/
static bool is_ok_opt_value_char(char c)
{
  return (c == '-') || (c == '*') || (c == '+') || (c == '=') || my_isalnum(c);
}

/******************************************************************
  Which characters are allowed between option names and values: (for 'set')
******************************************************************/
static bool is_ok_opt_name_value_sep_char(char c)
{
  return (c == '=') || my_isspace(c);
}

/******************************************************************
...
******************************************************************/
static bool team_command(struct connection *caller, char *str, bool check)
{
  struct player *pplayer;
  enum m_pre_result match_result;
  char buf[MAX_LEN_CONSOLE_LINE];
  char *arg[2];
  int ntokens = 0, i;
  bool res = FALSE;

  if (server_state != PRE_GAME_STATE || !game.is_new_game) {
    cmd_reply(CMD_TEAM, caller, C_SYNTAX,
              _("Cannot change teams once game has begun."));
    return FALSE;
  }

  if (str != NULL || strlen(str) > 0) {
    sz_strlcpy(buf, str);
    ntokens = get_tokens(buf, arg, 2, TOKEN_DELIMITERS);
  }
  if (ntokens > 2 || ntokens < 1) {
    cmd_reply(CMD_TEAM, caller, C_SYNTAX,
	      _("Undefined argument.  Usage: team <player> [team]."));
    goto cleanup;
  }

  pplayer = find_player_by_name_prefix(arg[0], &match_result);
  if (pplayer == NULL) {
    cmd_reply_no_such_player(CMD_TEAM, caller, arg[0], match_result);
    goto cleanup;
  }

  if (!check && pplayer->team != TEAM_NONE) {
    team_remove_player(pplayer);
  }

  if (ntokens == 1) {
    /* Remove from team */
    if (!check) {
      cmd_reply(CMD_TEAM, caller, C_OK, _("Player %s is made teamless."), 
          pplayer->name);
    }
    res = TRUE;
    goto cleanup;
  }

  if (!is_ascii_name(arg[1])) {
    cmd_reply(CMD_TEAM, caller, C_SYNTAX,
	      _("Only ASCII characters are allowed for team names."));
    goto cleanup;
  }
  if (is_barbarian(pplayer)) {
    /* This can happen if we change team settings on a loaded game. */
    cmd_reply(CMD_TEAM, caller, C_SYNTAX, _("Cannot team a barbarian."));
    goto cleanup;
  }
  if (pplayer->is_observer) {
    /* Not allowed! */
    cmd_reply(CMD_TEAM, caller, C_SYNTAX, _("Cannot team an observer."));
    goto cleanup;
  }
  if (!check) {
    team_add_player(pplayer, arg[1]);
    cmd_reply(CMD_TEAM, caller, C_OK, _("Player %s set to team %s."),
	      pplayer->name, team_get_by_id(pplayer->team)->name);
  }
  res = TRUE;

  cleanup:
  for (i = 0; i < ntokens; i++) {
    free(arg[i]);
  }
  return res;
}

/******************************************************************
  Make or participate in a vote.
******************************************************************/
static bool vote_command(struct connection *caller, char *str,
                         bool check)
{
  char buf[MAX_LEN_CONSOLE_LINE];
  char *arg[3];
  int ntokens = 0, i;
  const char *usage = _("Undefined arguments. Usage: vote yes|no "
                        "[vote number].");
  int idx;
  bool res = FALSE;

  if (caller == NULL || caller->player == NULL) {
    cmd_reply(CMD_VOTE, caller, C_FAIL, _("This command is client only."));
    return FALSE;
  } else if (caller->player->is_observer || caller->observer) {
    cmd_reply(CMD_VOTE, caller, C_FAIL, _("Observers cannot vote."));
    return FALSE;
  } else if (!str || strlen(str) == 0) {
    int j = 0;

    for (i = 0; i < MAX_NUM_PLAYERS; i++) {
      struct voting *vote = &votes[i];

      if (vote->command[0] != '\0') {
        j++;
        cmd_reply(CMD_VOTE, caller, C_COMMENT,
		  _("Vote %d \"%s\": %d for, %d against"),
		  vote->vote_no, vote->command, vote->yes, 
                  vote->no);
      }
    }
    if (j == 0) {
      cmd_reply(CMD_VOTE, caller, C_COMMENT,
		_("There are no votes going on."));
    }
    return FALSE; /* see below */
  } if (check) {
    return FALSE; /* cannot vote over having vote! */
  }
  idx = caller->player->player_no;

  sz_strlcpy(buf, str);
  ntokens = get_tokens(buf, arg, 2, TOKEN_DELIMITERS);

  if (strcmp(arg[0], "yes") == 0
      || strcmp(arg[0], "no") == 0) {
    int which = -1;
    struct voting *vote = NULL;

    if (ntokens == 1) {
      /* Applies to last vote */
      if (last_vote > -1) {
        which = last_vote;
      } else {
        cmd_reply(CMD_VOTE, caller, C_FAIL, _("No legal last vote."));
        goto cleanup;
      }
    } else {
      if (sscanf(arg[1], "%d", &which) <= 0) {
        cmd_reply(CMD_VOTE, caller, C_SYNTAX, _("Value must be integer."));
        goto cleanup;
      }
    }
    /* Ok, now try to find this vote */
    for (i = 0; i < MAX_NUM_PLAYERS; i++) {
      if (votes[i].vote_no == which) {
        vote = &votes[i];
      }
    }
    if (which > last_vote || !vote || vote->command[0] == '\0') {
      cmd_reply(CMD_VOTE, caller, C_FAIL, _("No such vote (%d)."), which);
      goto cleanup;
    }
    if (strcmp(arg[0], "yes") == 0) {
      cmd_reply(CMD_VOTE, caller, C_COMMENT, _("You voted for \"%s\""), 
                vote->command);
      vote->votes_cast[caller->player->player_no] = VOTE_YES;
    } else if (strcmp(arg[0], "no") == 0) {
      cmd_reply(CMD_VOTE, caller, C_COMMENT, _("You voted against \"%s\""), 
                vote->command);
      vote->votes_cast[caller->player->player_no] = VOTE_NO;
    }
    check_vote(vote);
  } else {
    cmd_reply(CMD_VOTE, caller, C_SYNTAX, usage);
    goto cleanup;
  }

  res = TRUE;
  cleanup:
  for (i = 0; i < ntokens; i++) {
    free(arg[i]);
  }
  return res;
}

/******************************************************************
  ...
******************************************************************/
static bool debug_command(struct connection *caller, char *str, 
                          bool check)
{
  char buf[MAX_LEN_CONSOLE_LINE];
  char *arg[3];
  int ntokens = 0, i;
  const char *usage = _("Undefined arguments. Usage: debug <player "
			"<player> | city <x> <y> | units <x> <y> | "
			"unit <id>>.");

  if (server_state != RUN_GAME_STATE) {
    cmd_reply(CMD_DEBUG, caller, C_SYNTAX,
              _("Can only use this command once game has begun."));
    return FALSE;
  }
  if (check) {
    return TRUE; /* whatever! */
  }

  if (str != NULL && strlen(str) > 0) {
    sz_strlcpy(buf, str);
    ntokens = get_tokens(buf, arg, 3, TOKEN_DELIMITERS);
  } else {
    ntokens = 0;
  }

  if (ntokens > 0 && strcmp(arg[0], "player") == 0) {
    struct player *pplayer;
    enum m_pre_result match_result;

    if (ntokens != 2) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, usage);
      goto cleanup;
    }
    pplayer = find_player_by_name_prefix(arg[1], &match_result);
    if (pplayer == NULL) {
      cmd_reply_no_such_player(CMD_DEBUG, caller, arg[1], match_result);
      goto cleanup;
    }
    if (pplayer->debug) {
      pplayer->debug = FALSE;
      cmd_reply(CMD_DEBUG, caller, C_OK, _("%s no longer debugged"), 
                pplayer->name);
    } else {
      pplayer->debug = TRUE;
      cmd_reply(CMD_DEBUG, caller, C_OK, _("%s debugged"), pplayer->name);
      /* TODO: print some info about the player here */
    }
  } else if (ntokens > 0 && strcmp(arg[0], "city") == 0) {
    int x, y;
    struct tile *ptile;
    struct city *pcity;

    if (ntokens != 3) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, usage);
      goto cleanup;
    }
    if (sscanf(arg[1], "%d", &x) != 1 || sscanf(arg[2], "%d", &y) != 1) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, _("Value 2 & 3 must be integer."));
      goto cleanup;
    }
    if (!(ptile = map_pos_to_tile(x, y))) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, _("Bad map coordinates."));
      goto cleanup;
    }
    pcity = ptile->city;
    if (!pcity) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, _("No city at this coordinate."));
      goto cleanup;
    }
    if (pcity->debug) {
      pcity->debug = FALSE;
      cmd_reply(CMD_DEBUG, caller, C_OK, _("%s no longer debugged"),
                pcity->name);
    } else {
      pcity->debug = TRUE;
      CITY_LOG(LOG_NORMAL, pcity, "debugged");
      pcity->ai.next_recalc = 0; /* force recalc of city next turn */
    }
  } else if (ntokens > 0 && strcmp(arg[0], "units") == 0) {
    int x, y;
    struct tile *ptile;

    if (ntokens != 3) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, usage);
      goto cleanup;
    }
    if (sscanf(arg[1], "%d", &x) != 1 || sscanf(arg[2], "%d", &y) != 1) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, _("Value 2 & 3 must be integer."));
      goto cleanup;
    }
    if (!(ptile = map_pos_to_tile(x, y))) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, _("Bad map coordinates."));
      goto cleanup;
    }
    unit_list_iterate(ptile->units, punit) {
      if (punit->debug) {
        punit->debug = FALSE;
        cmd_reply(CMD_DEBUG, caller, C_OK, _("%s's %s no longer debugged."),
                  unit_owner(punit)->name, unit_name(punit->type));
      } else {
        punit->debug = TRUE;
        UNIT_LOG(LOG_NORMAL, punit, _("%s's %s debugged."),
                 unit_owner(punit)->name, unit_name(punit->type));
      }
    } unit_list_iterate_end;
  } else if (ntokens > 0 && strcmp(arg[0], "unit") == 0) {
    int id;
    struct unit *punit;

    if (ntokens != 2) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, usage);
      goto cleanup;
    }
    if (sscanf(arg[1], "%d", &id) != 1) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, _("Value 2 must be integer."));
      goto cleanup;
    }
    if (!(punit = find_unit_by_id(id))) {
      cmd_reply(CMD_DEBUG, caller, C_SYNTAX, _("Unit %d does not exist."), id);
      goto cleanup;
    }
    if (punit->debug) {
      punit->debug = FALSE;
      cmd_reply(CMD_DEBUG, caller, C_OK, _("%s's %s no longer debugged."),
                unit_owner(punit)->name, unit_name(punit->type));
    } else {
      punit->debug = TRUE;
      UNIT_LOG(LOG_NORMAL, punit, _("%s's %s debugged."),
               unit_owner(punit)->name, unit_name(punit->type));
    }
  } else {
    cmd_reply(CMD_DEBUG, caller, C_SYNTAX, usage);
  }
  cleanup:
  for (i = 0; i < ntokens; i++) {
    free(arg[i]);
  }
  return TRUE;
}

/******************************************************************
  ...
******************************************************************/
static bool set_command(struct connection *caller, char *str, bool check)
{
  char command[MAX_LEN_CONSOLE_LINE], arg[MAX_LEN_CONSOLE_LINE], *cptr_s, *cptr_d;
  int val, cmd;
  struct settings_s *op;
  bool do_update;
  char buffer[500];

  for (cptr_s = str; *cptr_s != '\0' && !is_ok_opt_name_char(*cptr_s);
       cptr_s++) {
    /* nothing */
  }

  for(cptr_d=command;
      *cptr_s != '\0' && is_ok_opt_name_char(*cptr_s);
      cptr_s++, cptr_d++) {
    *cptr_d=*cptr_s;
  }
  *cptr_d='\0';
  
  for (; *cptr_s != '\0' && is_ok_opt_name_value_sep_char(*cptr_s); cptr_s++) {
    /* nothing */
  }

  for (cptr_d = arg; *cptr_s != '\0' && is_ok_opt_value_char(*cptr_s); cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  cmd = lookup_option(command);
  if (cmd==-1) {
    cmd_reply(CMD_SET, caller, C_SYNTAX,
	      _("Undefined argument.  Usage: set <option> <value>."));
    return FALSE;
  }
  else if (cmd==-2) {
    cmd_reply(CMD_SET, caller, C_SYNTAX,
	      _("Ambiguous option name."));
    return FALSE;
  }
  if (!may_set_option(caller,cmd) && !check) {
     cmd_reply(CMD_SET, caller, C_FAIL,
	       _("You are not allowed to set this option."));
    return FALSE;
  }
  if (!sset_is_changeable(cmd)) {
    cmd_reply(CMD_SET, caller, C_BOUNCE,
	      _("This setting can't be modified after the game has started."));
    return FALSE;
  }

  op = &settings[cmd];

  do_update = FALSE;
  buffer[0] = '\0';

  switch (op->type) {
  case SSET_BOOL:
    if (sscanf(arg, "%d", &val) != 1) {
      cmd_reply(CMD_SET, caller, C_SYNTAX, _("Value must be an integer."));
      return FALSE;
    } else if (val != 0 && val != 1) {
      cmd_reply(CMD_SET, caller, C_SYNTAX,
		_("Value out of range (minimum: 0, maximum: 1)."));
      return FALSE;
    } else {
      const char *reject_message = NULL;
      bool b_val = (val != 0);

      if (settings[cmd].bool_validate
	  && !settings[cmd].bool_validate(b_val, &reject_message)) {
	cmd_reply(CMD_SET, caller, C_SYNTAX, reject_message);
        return FALSE;
      } else if (!check) {
	*(op->bool_value) = b_val;
	my_snprintf(buffer, sizeof(buffer),
		    _("Option: %s has been set to %d."), op->name,
		    *(op->bool_value) ? 1 : 0);
	do_update = TRUE;
      }
    }
    break;

  case SSET_INT:
    if (sscanf(arg, "%d", &val) != 1) {
      cmd_reply(CMD_SET, caller, C_SYNTAX, _("Value must be an integer."));
      return FALSE;
    } else if (val < op->int_min_value || val > op->int_max_value) {
      cmd_reply(CMD_SET, caller, C_SYNTAX,
		_("Value out of range (minimum: %d, maximum: %d)."),
		op->int_min_value, op->int_max_value);
      return FALSE;
    } else {
      const char *reject_message = NULL;

      if (settings[cmd].int_validate
	  && !settings[cmd].int_validate(val, &reject_message)) {
	cmd_reply(CMD_SET, caller, C_SYNTAX, reject_message);
        return FALSE;
      } else if (!check) {
	*(op->int_value) = val;
	my_snprintf(buffer, sizeof(buffer),
		    _("Option: %s has been set to %d."), op->name,
		    *(op->int_value));
	do_update = TRUE;
      }
    }
    break;

  case SSET_STRING:
    if (strlen(arg) >= op->string_value_size) {
      cmd_reply(CMD_SET, caller, C_SYNTAX,
		_("String value too long.  Usage: set <option> <value>."));
      return FALSE;
    } else {
      const char *reject_message = NULL;

      if (settings[cmd].string_validate
	  && !settings[cmd].string_validate(arg, &reject_message)) {
	cmd_reply(CMD_SET, caller, C_SYNTAX, reject_message);
        return FALSE;
      } else if (!check) {
	strcpy(op->string_value, arg);
	my_snprintf(buffer, sizeof(buffer),
		    _("Option: %s has been set to \"%s\"."), op->name,
		    op->string_value);
	do_update = TRUE;
      }
    }
    break;
  }

  if (!check && strlen(buffer) > 0 && sset_is_to_client(cmd)) {
    notify_conn(NULL, "%s", buffer);
  }

  if (!check && do_update) {
    send_server_info_to_metaserver(META_INFO);
    /* 
     * send any modified game parameters to the clients -- if sent
     * before RUN_GAME_STATE, triggers a popdown_races_dialog() call
     * in client/packhand.c#handle_game_info() 
     */
    if (server_state == RUN_GAME_STATE) {
      send_game_info(NULL);
    }
  }
  return TRUE;
}

/**************************************************************************
 check game.allow_take for permission to take or observe a player
**************************************************************************/
static bool is_allowed_to_take(struct player *pplayer, bool will_obs, 
                               char *msg)
{
  const char *allow;

  if (pplayer->is_observer) {
    if (!(allow = strchr(game.allow_take, (game.is_new_game ? 'O' : 'o')))) {
      if (will_obs) {
        mystrlcpy(msg, _("Sorry, one can't observe globally in this game."),
                  MAX_LEN_MSG);
      } else {
        mystrlcpy(msg, _("Sorry, you can't take a global observer. Observe "
                         "it instead."),
                  MAX_LEN_MSG);
      }
      return FALSE;
    }
  } else if (is_barbarian(pplayer)) {
    if (!(allow = strchr(game.allow_take, 'b'))) {
      if (will_obs) {
        mystrlcpy(msg, _("Sorry, one can't observe barbarians in this game."),
                  MAX_LEN_MSG);
      } else {
        mystrlcpy(msg, _("Sorry, one can't take barbarians in this game."),
                  MAX_LEN_MSG);
      }
      return FALSE;
    }
  } else if (!pplayer->is_alive) {
    if (!(allow = strchr(game.allow_take, 'd'))) {
      if (will_obs) {
        mystrlcpy(msg, _("Sorry, one can't observe dead players in this game."),
                  MAX_LEN_MSG);
      } else {
        mystrlcpy(msg, _("Sorry, one can't take dead players in this game."),
                  MAX_LEN_MSG);
      }
      return FALSE;
    }
  } else if (pplayer->ai.control) {
    if (!(allow = strchr(game.allow_take, (game.is_new_game ? 'A' : 'a')))) {
      if (will_obs) {
        mystrlcpy(msg, _("Sorry, one can't observe AI players in this game."),
                  MAX_LEN_MSG);
      } else {
        mystrlcpy(msg, _("Sorry, one can't take AI players in this game."),
                  MAX_LEN_MSG);
      }
      return FALSE;
    }
  } else { 
    if (!(allow = strchr(game.allow_take, (game.is_new_game ? 'H' : 'h')))) {
      if (will_obs) {
        mystrlcpy(msg, 
                  _("Sorry, one can't observe human players in this game."),
                  MAX_LEN_MSG);
      } else {
        mystrlcpy(msg, _("Sorry, one can't take human players in this game."),
                  MAX_LEN_MSG);
      }
      return FALSE;
    }
  }

  allow++;

  if (will_obs && (*allow == '2' || *allow == '3')) {
    mystrlcpy(msg, _("Sorry, one can't observe in this game."), MAX_LEN_MSG);
    return FALSE;
  }

  if (!will_obs && *allow == '4') {
    mystrlcpy(msg, _("Sorry, one can't take players in this game."),
              MAX_LEN_MSG);
    return FALSE;
  }

  if (!will_obs && pplayer->is_connected && (*allow == '1' || *allow == '3')) {
    mystrlcpy(msg, _("Sorry, one can't take players already connected "
                     "in this game."), MAX_LEN_MSG);
    return FALSE;
  }

  return TRUE;
}

/**************************************************************************
 Observe another player. If we were already attached, detach 
 (see detach_command()). The console and those with ALLOW_HACK can
 use the two-argument command and force others to observe.
**************************************************************************/
static bool observe_command(struct connection *caller, char *str, bool check)
{
  int i = 0, ntokens = 0;
  char buf[MAX_LEN_CONSOLE_LINE], *arg[2], msg[MAX_LEN_MSG];  
  bool is_newgame = (server_state == PRE_GAME_STATE || 
                     server_state == SELECT_RACES_STATE) && game.is_new_game;
  
  enum m_pre_result result;
  struct connection *pconn = NULL;
  struct player *pplayer = NULL;
  bool res = FALSE;
  
  /******** PART I: fill pconn and pplayer ********/

  sz_strlcpy(buf, str);
  ntokens = get_tokens(buf, arg, 2, TOKEN_DELIMITERS);
  
  /* check syntax, only certain syntax if allowed depending on the caller */
  if (!caller && ntokens < 1) {
    cmd_reply(CMD_OBSERVE, caller, C_SYNTAX,
              _("Usage: observe [connection-name [player-name]]"));
    goto end;
  } 

  if (ntokens == 2 && (caller && caller->access_level != ALLOW_HACK)) {
    cmd_reply(CMD_OBSERVE, caller, C_SYNTAX,
              _("Usage: observe [player-name]"));
    goto end;
  }

  /* match connection if we're console, match a player if we're not */
  if (ntokens == 1) {
    if (!caller && !(pconn = find_conn_by_user_prefix(arg[0], &result))) {
      cmd_reply_no_such_conn(CMD_OBSERVE, caller, arg[0], result);
      goto end;
    } else if (caller 
               && !(pplayer = find_player_by_name_prefix(arg[0], &result))) {
      cmd_reply_no_such_player(CMD_OBSERVE, caller, arg[0], result);
      goto end;
    }
  }

  /* get connection name then player name */
  if (ntokens == 2) {
    if (!(pconn = find_conn_by_user_prefix(arg[0], &result))) {
      cmd_reply_no_such_conn(CMD_OBSERVE, caller, arg[0], result);
      goto end;
    }
    if (!(pplayer = find_player_by_name_prefix(arg[1], &result))) {
      cmd_reply_no_such_player(CMD_OBSERVE, caller, arg[1], result);
      goto end;
    }
  }

  /* if we can't force other connections to observe, assign us to be pconn. */
  if (!pconn) {
    pconn = caller;
  }

  /* if we have no pplayer, it means that we want to be a global observer */
  if (!pplayer) {
    /* check if a global  observer has already been created */
    players_iterate(aplayer) {
      if (aplayer->is_observer) {
        pplayer = aplayer;
        break;
      }
    } players_iterate_end;

    /* we need to create a new player */
    if (!pplayer) {
      if (game.nplayers >= MAX_NUM_PLAYERS) {
        notify_conn(NULL, _("Game: A global observer cannot be created: too "
			    "many regular players."));
        goto end;
      }

      pplayer = &game.players[game.nplayers];
      server_player_init(pplayer, 
                         (server_state == RUN_GAME_STATE) || !game.is_new_game);
      sz_strlcpy(pplayer->name, OBSERVER_NAME);
      sz_strlcpy(pplayer->username, ANON_USER_NAME);

      pplayer->is_connected = FALSE;
      pplayer->is_observer = TRUE;
      pplayer->capital = TRUE;
      pplayer->turn_done = TRUE;
      pplayer->embassy = 0;   /* no embassys */
      pplayer->is_alive = FALSE;
      pplayer->was_created = FALSE;

      if ((server_state == RUN_GAME_STATE) || !game.is_new_game) {
        pplayer->nation = OBSERVER_NATION;
        init_tech(pplayer);
	give_initial_techs(pplayer);
        map_know_and_see_all(pplayer);
      }

      game.nplayers++;

      /* tell everyone that game.nplayers has been updated */
      send_game_info(NULL);
      send_player_info(pplayer, NULL);

      notify_conn(NULL, _("Game: A global observer has been created"));
    }
  }

  /******** PART II: do the observing ********/

  /* check allowtake for permission */
  if (!is_allowed_to_take(pplayer, TRUE, msg)) {
    cmd_reply(CMD_OBSERVE, caller, C_FAIL, msg);
    goto end;
  }

  /* observing your own player (during pregame) makes no sense. */
  if (pconn->player == pplayer && !pconn->observer
      && is_newgame && !pplayer->was_created) {
    cmd_reply(CMD_OBSERVE, caller, C_FAIL, 
              _("%s already controls %s. Using 'observe' would remove %s"),
              pconn->username, pplayer->name, pplayer->name);
    goto end;
  }

  /* attempting to observe a player you're already observing should fail. */
  if (pconn->player == pplayer && pconn->observer) {
    cmd_reply(CMD_OBSERVE, caller, C_FAIL,
              _("%s is already observing %s."),  
              pconn->username, pplayer->name);
    goto end;
  }

  res = TRUE; /* all tests passed */
  if (check) {
    goto end;
  }

  /* if we want to switch players, reset the client */
  if (pconn->player && server_state == RUN_GAME_STATE) {
    send_game_state(&pconn->self, CLIENT_PRE_GAME_STATE);
    send_conn_info(&game.est_connections,  &pconn->self);
  }

  /* if the connection is already attached to a player,
   * unattach and cleanup old player (rename, remove, etc) */
  if (pconn->player) {
    char name[MAX_LEN_NAME];

    /* if a pconn->player is removed, we'll lose pplayer */
    sz_strlcpy(name, pplayer->name);
  
    detach_command(NULL, pconn->username, FALSE);

    /* find pplayer again, the pointer might have been changed */
    pplayer = find_player_by_name(name);
  } 

  /* we don't want the connection's username on another player */
  players_iterate(aplayer) {
    if (strncmp(aplayer->username, pconn->username, MAX_LEN_NAME) == 0) {
      sz_strlcpy(aplayer->username, ANON_USER_NAME);
    }
  } players_iterate_end;

  /* attach pconn to new player as an observer */
  pconn->observer = TRUE; /* do this before attach! */
  attach_connection_to_player(pconn, pplayer);
  send_conn_info(&pconn->self, &game.est_connections);

  if (server_state == RUN_GAME_STATE) {
    send_packet_freeze_hint(pconn);
    send_rulesets(&pconn->self);
    send_all_info(&pconn->self);
    send_game_state(&pconn->self, CLIENT_GAME_RUNNING_STATE);
    send_player_info(NULL, NULL);
    send_diplomatic_meetings(pconn);
    send_packet_thaw_hint(pconn);
    send_packet_start_turn(pconn);
  }

  cmd_reply(CMD_OBSERVE, caller, C_OK, _("%s now observes %s"),
            pconn->username, pplayer->name);

  end:;
  /* free our args */
  for (i = 0; i < ntokens; i++) {
    free(arg[i]);
  }
  return res;
}

/**************************************************************************
  Take over a player. If a connection already has control of that player, 
  disallow it. 

  If there are two arguments, treat the first as the connection name and the
  second as the player name (only hack and the console can do this).
  Otherwise, there should be one argument, that being the player that the 
  caller wants to take.
**************************************************************************/
static bool take_command(struct connection *caller, char *str, bool check)
{
  int i = 0, ntokens = 0;
  char buf[MAX_LEN_CONSOLE_LINE], *arg[2], msg[MAX_LEN_MSG];
  bool is_newgame = (server_state == PRE_GAME_STATE || 
                     server_state == SELECT_RACES_STATE) && game.is_new_game;

  enum m_pre_result match_result;
  struct connection *pconn = NULL;
  struct player *pplayer = NULL;
  bool res = FALSE;
  
  /******** PART I: fill pconn and pplayer ********/

  sz_strlcpy(buf, str);
  ntokens = get_tokens(buf, arg, 2, TOKEN_DELIMITERS);
  
  /* check syntax */
  if (!caller && ntokens != 2) {
    cmd_reply(CMD_TAKE, caller, C_SYNTAX,
              _("Usage: take <connection-name> <player-name>"));
    goto end;
  }

  if (caller && caller->access_level != ALLOW_HACK && ntokens != 1) {
    cmd_reply(CMD_TAKE, caller, C_SYNTAX, _("Usage: take <player-name>"));
    goto end;
  }

  if (ntokens == 0) {
    cmd_reply(CMD_TAKE, caller, C_SYNTAX,
              _("Usage: take [connection-name] <player-name>"));
    goto end;
  }

  if (ntokens == 2) {
    if (!(pconn = find_conn_by_user_prefix(arg[i], &match_result))) {
      cmd_reply_no_such_conn(CMD_TAKE, caller, arg[i], match_result);
      goto end;
    }
    i++; /* found a conn, now reference the second argument */
  }

  if (!(pplayer = find_player_by_name_prefix(arg[i], &match_result))) {
    cmd_reply_no_such_player(CMD_TAKE, caller, arg[i], match_result);
    goto end;
  }

  /* if we don't assign other connections to players, assign us to be pconn. */
  if (!pconn) {
    pconn = caller;
  }

  /******** PART II: do the attaching ********/

  /* check allowtake for permission */
  if (!is_allowed_to_take(pplayer, FALSE, msg)) {
    cmd_reply(CMD_TAKE, caller, C_FAIL, msg);
    goto end;
  }

  /* you may not take over a global observer */
  if (pplayer->is_observer) {
    cmd_reply(CMD_TAKE, caller, C_FAIL, _("%s cannot be taken."),
              pplayer->name);
    goto end;
  } 

  /* taking your own player makes no sense. */
  if (pconn->player == pplayer && !pconn->observer) {
    cmd_reply(CMD_TAKE, caller, C_FAIL, _("%s already controls %s"),
              pconn->username, pplayer->name);
    goto end;
  } 

  res = TRUE;
  if (check) {
    goto end;
  }

  /* if we want to switch players, reset the client if the game is running */
  if (pconn->player && server_state == RUN_GAME_STATE) {
    send_game_state(&pconn->self, CLIENT_PRE_GAME_STATE);
    send_player_info_c(NULL, &pconn->self);
    send_conn_info(&game.est_connections,  &pconn->self);
  }

  /* if we're taking another player with a user attached, 
   * forcibly detach the user from the player. */
  conn_list_iterate(pplayer->connections, aconn) {
    if (!aconn->observer) {
      if (server_state == RUN_GAME_STATE) {
        send_game_state(&aconn->self, CLIENT_PRE_GAME_STATE);
      }
      notify_conn(&aconn->self, _("being detached from %s."), pplayer->name);
      unattach_connection_from_player(aconn);
      send_conn_info(&aconn->self, &game.est_connections);
    }
  } conn_list_iterate_end;

  /* if the connection is already attached to a player,
   * unattach and cleanup old player (rename, remove, etc) */
  if (pconn->player) {
    char name[MAX_LEN_NAME];

    /* if a pconn->player is removed, we'll lose pplayer */
    sz_strlcpy(name, pplayer->name);

    detach_command(NULL, pconn->username, FALSE);

    /* find pplayer again, the pointer might have been changed */
    pplayer = find_player_by_name(name);
  }

  /* we don't want the connection's username on another player */
  players_iterate(aplayer) {
    if (strncmp(aplayer->username, pconn->username, MAX_LEN_NAME) == 0) {
      sz_strlcpy(aplayer->username, ANON_USER_NAME);
    }
  } players_iterate_end;

  /* now attach to new player */
  attach_connection_to_player(pconn, pplayer);
  send_conn_info(&pconn->self, &game.est_connections);
 
  /* if pplayer wasn't /created, and we're still in pregame, change its name */
  if (!pplayer->was_created && is_newgame) {
    sz_strlcpy(pplayer->name, pconn->username);
  }

  if (server_state == RUN_GAME_STATE) {
    send_packet_freeze_hint(pconn);
    send_rulesets(&pconn->self);
    send_all_info(&pconn->self);
    send_game_state(&pconn->self, CLIENT_GAME_RUNNING_STATE);
    send_player_info(NULL, NULL);
    send_diplomatic_meetings(pconn);
    send_packet_thaw_hint(pconn);
    send_packet_start_turn(pconn);
  }

  /* aitoggle the player back to human if necessary. */
  if (pplayer->ai.control && game.auto_ai_toggle) {
    toggle_ai_player_direct(NULL, pplayer);
  }

  /* yes this has to go after the toggle check */
  if (server_state == RUN_GAME_STATE) {
    gamelog(GAMELOG_PLAYER, pplayer);
  }

  cmd_reply(CMD_TAKE, caller, C_OK, _("%s now controls %s (%s, %s)"), 
            pconn->username, pplayer->name, 
            is_barbarian(pplayer) ? _("Barbarian") : pplayer->ai.control ?
            _("AI") : _("Human"), pplayer->is_alive ? _("Alive") : _("Dead"));

  end:;
  /* free our args */
  for (i = 0; i < ntokens; i++) {
    free(arg[i]);
  }
  return res;
}

/**************************************************************************
  Detach from a player. if that player wasn't /created and you were 
  controlling the player, remove it (and then detach any observers as well).
**************************************************************************/
static bool detach_command(struct connection *caller, char *str, bool check)
{
  int i = 0, ntokens = 0;
  char buf[MAX_LEN_CONSOLE_LINE], *arg[1];

  enum m_pre_result match_result;
  struct connection *pconn = NULL;
  struct player *pplayer = NULL;
  bool is_newgame = (server_state == PRE_GAME_STATE || 
                     server_state == SELECT_RACES_STATE) && game.is_new_game;
  bool one_obs_among_many = FALSE, res = FALSE;

  sz_strlcpy(buf, str);
  ntokens = get_tokens(buf, arg, 1, TOKEN_DELIMITERS);

  if (!caller && ntokens == 0) {
    cmd_reply(CMD_DETACH, caller, C_SYNTAX,
              _("Usage: detach <connection-name>"));
    goto end;
  }

  /* match the connection if the argument was given */
  if (ntokens == 1 
      && !(pconn = find_conn_by_user_prefix(arg[0], &match_result))) {
    cmd_reply_no_such_conn(CMD_DETACH, caller, arg[0], match_result);
    goto end;
  }

  /* if no argument is given, the caller wants to detach himself */
  if (!pconn) {
    pconn = caller;
  }

  /* if pconn and caller are not the same, only continue 
   * if we're console, or we have ALLOW_HACK */
  if (pconn != caller  && caller && caller->access_level != ALLOW_HACK) {
    cmd_reply(CMD_DETACH, caller, C_FAIL, 
                _("You can not detach other users."));
    goto end;
  }

  pplayer = pconn->player;

  /* must have someone to detach from... */
  if (!pplayer) {
    cmd_reply(CMD_DETACH, caller, C_FAIL, 
              _("%s is not attached to any player."), pconn->username);
    goto end;
  }

  /* a special case for global observers: we don't want to remove the
   * observer player in pregame if someone else is also observing it */
  if (pplayer->is_observer && conn_list_size(&pplayer->connections) > 1) {
    one_obs_among_many = TRUE;
  }

  res = TRUE;
  if (check) {
    goto end;
  }

  /* if we want to detach while the game is running, reset the client */
  if (server_state == RUN_GAME_STATE) {
    send_game_state(&pconn->self, CLIENT_PRE_GAME_STATE);
    send_game_info(&pconn->self);
    send_player_info_c(NULL, &pconn->self);
    send_conn_info(&game.est_connections, &pconn->self);
  }

  /* actually do the detaching */
  unattach_connection_from_player(pconn);
  send_conn_info(&pconn->self, &game.est_connections);
  cmd_reply(CMD_DETACH, caller, C_COMMENT,
            _("%s detaching from %s"), pconn->username, pplayer->name);

  /* only remove the player if the game is new and in pregame, 
   * the player wasn't /created, and no one is controlling it 
   * and we were observing but no one else was... */
  if (!pplayer->is_connected && !pplayer->was_created && is_newgame
      && !one_obs_among_many) {
    /* detach any observers */
    conn_list_iterate(pplayer->connections, aconn) {
      if (aconn->observer) {
        unattach_connection_from_player(aconn);
        send_conn_info(&aconn->self, &game.est_connections);
        notify_conn(&aconn->self, _("detaching from %s."), pplayer->name);
      }
    } conn_list_iterate_end;

    /* actually do the removal */
    game_remove_player(pplayer);
    game_renumber_players(pplayer->player_no);
    player_init(&game.players[game.nplayers]);
  }

  if (!pplayer->is_connected) {
    /* aitoggle the player if no longer connected. */
    if (game.auto_ai_toggle && !pplayer->ai.control) {
      toggle_ai_player_direct(NULL, pplayer);
    }
    /* reset username if in pregame. */
    if (is_newgame) {
      sz_strlcpy(pplayer->username, ANON_USER_NAME);
    }
  }

  if (server_state == RUN_GAME_STATE) {
    gamelog(GAMELOG_PLAYER, pplayer);
  }

  end:;
  /* free our args */
  for (i = 0; i < ntokens; i++) {
    free(arg[i]);
  }
  return res;
}

/**************************************************************************
  After a /load is completed, a reply is sent to all connections to tell
  them about the load.  This information is used by the conndlg to
  set up the graphical interface for starting the game.
**************************************************************************/
static void send_load_game_info(bool load_successful)
{
  struct packet_game_load packet;

  /* Clear everything to be safe. */
  memset(&packet, 0, sizeof(packet));

  sz_strlcpy(packet.load_filename, srvarg.load_filename);
  packet.load_successful = load_successful;

  if (load_successful) {
    int i = 0;

    players_iterate(pplayer) {
      if (game.nation_count && is_barbarian(pplayer)) {
	continue;
      }

      sz_strlcpy(packet.name[i], pplayer->name);
      sz_strlcpy(packet.username[i], pplayer->username);
      if (game.nation_count) {
	sz_strlcpy(packet.nation_name[i], get_nation_name(pplayer->nation));
	sz_strlcpy(packet.nation_flag[i],
	    get_nation_by_plr(pplayer)->flag_graphic_str);
      } else { /* No nations picked */
	sz_strlcpy(packet.nation_name[i], "");
	sz_strlcpy(packet.nation_flag[i], "");
      }
      packet.is_alive[i] = pplayer->is_alive;
      packet.is_ai[i] = pplayer->ai.control;
      i++;
    } players_iterate_end;

    packet.nplayers = i;
  } else {
    packet.nplayers = 0;
  }

  lsend_packet_game_load(&game.est_connections, &packet);
}

/**************************************************************************
  ...
**************************************************************************/
bool load_command(struct connection *caller, char *filename, bool check)
{
  struct timer *loadtimer, *uloadtimer;  
  struct section_file file;
  char arg[strlen(filename) + 1];

  /* We make a local copy because the parameter might be a pointer to 
   * srvarg.load_filename, which we edit down below. */
  sz_strlcpy(arg, filename);

  if (arg[0] == '\0') {
    cmd_reply(CMD_LOAD, caller, C_FAIL, _("Usage: load <filename>"));
    send_load_game_info(FALSE);
    return FALSE;
  }

  if (server_state != PRE_GAME_STATE) {
    cmd_reply(CMD_LOAD, caller, C_FAIL, _("Can't load a game while another "
                                          "is running."));
    send_load_game_info(FALSE);
    return FALSE;
  }

  /* attempt to parse the file */

  if (!section_file_load_nodup(&file, arg)) {
    cmd_reply(CMD_LOAD, caller, C_FAIL, _("Couldn't load savefile: %s"), arg);
    send_load_game_info(FALSE);
    return FALSE;
  }

  if (check) {
    return TRUE;
  }

  /* we found it, free all structures */
  server_game_free();

  game_init();

  loadtimer = new_timer_start(TIMER_CPU, TIMER_ACTIVE);
  uloadtimer = new_timer_start(TIMER_USER, TIMER_ACTIVE);

  sz_strlcpy(srvarg.load_filename, arg);

  game_load(&file);
  section_file_check_unused(&file, arg);
  section_file_free(&file);

  freelog(LOG_VERBOSE, "Load time: %g seconds (%g apparent)",
          read_timer_seconds_free(loadtimer),
          read_timer_seconds_free(uloadtimer));

  sanity_check();

  /* Everything seemed to load ok; spread the good news. */
  send_load_game_info(TRUE);

  /* attach connections to players. currently, this applies only 
   * to connections that have the correct username. Any attachments
   * made before the game load are unattached. */
  conn_list_iterate(game.est_connections, pconn) {
    if (pconn->player) {
      unattach_connection_from_player(pconn);
    }
    players_iterate(pplayer) {
      if (strcmp(pconn->username, pplayer->username) == 0) {
        attach_connection_to_player(pconn, pplayer);
        break;
      }
    } players_iterate_end;
  } conn_list_iterate_end;
  return TRUE;
}

/**************************************************************************
  ...
**************************************************************************/
static bool set_rulesetdir(struct connection *caller, char *str, bool check)
{
  char filename[512], *pfilename;
  if ((str == NULL) || (strlen(str)==0)) {
    cmd_reply(CMD_RULESETDIR, caller, C_SYNTAX,
             _("Current ruleset directory is \"%s\""), game.rulesetdir);
    return FALSE;
  }
  my_snprintf(filename, sizeof(filename), "%s", str);
  pfilename = datafilename(filename);
  if (!pfilename) {
    cmd_reply(CMD_RULESETDIR, caller, C_SYNTAX,
             _("Ruleset directory \"%s\" not found"), str);
    return FALSE;
  }
  if (!check) {
    cmd_reply(CMD_RULESETDIR, caller, C_OK, 
              _("Ruleset directory set to \"%s\""), str);
    sz_strlcpy(game.rulesetdir, str);
  }
  return TRUE;
}

/**************************************************************************
  Cutting away a trailing comment by putting a '\0' on the '#'. The
  method handles # in single or double quotes. It also takes care of
  "\#".
**************************************************************************/
static void cut_comment(char *str)
{
  int i;
  bool in_single_quotes = FALSE, in_double_quotes = FALSE;

  freelog(LOG_DEBUG,"cut_comment(str='%s')",str);

  for (i = 0; i < strlen(str); i++) {
    if (str[i] == '"' && !in_single_quotes) {
      in_double_quotes = !in_double_quotes;
    } else if (str[i] == '\'' && !in_double_quotes) {
      in_single_quotes = !in_single_quotes;
    } else if (str[i] == '#' && !(in_single_quotes || in_double_quotes)
	       && (i == 0 || str[i - 1] != '\\')) {
      str[i] = '\0';
      break;
    }
  }
  freelog(LOG_DEBUG,"cut_comment: returning '%s'",str);
}

/**************************************************************************
...
**************************************************************************/
static bool quit_game(struct connection *caller, bool check)
{
  if (!check) {
    cmd_reply(CMD_QUIT, caller, C_OK, _("Goodbye."));
    gamelog(GAMELOG_JUDGE, GL_NONE);
    gamelog(GAMELOG_END);
    server_quit();
  }
  return TRUE;
}

/**************************************************************************
  Handle "command input", which could really come from stdin on console,
  or from client chat command, or read from file with -r, etc.
  caller==NULL means console, str is the input, which may optionally
  start with SERVER_COMMAND_PREFIX character.

  If check is TRUE, then do nothing, just check syntax.
**************************************************************************/
bool handle_stdin_input(struct connection *caller, char *str, bool check)
{
  char command[MAX_LEN_CONSOLE_LINE], arg[MAX_LEN_CONSOLE_LINE],
      allargs[MAX_LEN_CONSOLE_LINE], full_command[MAX_LEN_CONSOLE_LINE],
      *cptr_s, *cptr_d;
  int i;
  enum command_id cmd;

  /* notify to the server console */
  if (!check && caller) {
    con_write(C_COMMENT, "%s: '%s'", caller->username, str);
  }

  /* if the caller may not use any commands at all, don't waste any time */
  if (may_use_nothing(caller)) {
    cmd_reply(CMD_HELP, caller, C_FAIL,
	_("Sorry, you are not allowed to use server commands."));
     return FALSE;
  }

  /* Is it a comment or a blank line? */
  /* line is comment if the first non-whitespace character is '#': */
  cptr_s = skip_leading_spaces(str);
  if (*cptr_s == '\0' || *cptr_s == '#') {
    return FALSE;
  }

  /* commands may be prefixed with SERVER_COMMAND_PREFIX, even when
     given on the server command line - rp */
  if (*cptr_s == SERVER_COMMAND_PREFIX) cptr_s++;

  for (; *cptr_s != '\0' && !my_isalnum(*cptr_s); cptr_s++) {
    /* nothing */
  }

  /* copy the full command, in case we need it for voting purposes. */
  sz_strlcpy(full_command, cptr_s);

  /*
   * cptr_s points now to the beginning of the real command. It has
   * skipped leading whitespace, the SERVER_COMMAND_PREFIX and any
   * other non-alphanumeric characters.
   */
  for (cptr_d = command; *cptr_s != '\0' && my_isalnum(*cptr_s) &&
      cptr_d < command+sizeof(command)-1; cptr_s++, cptr_d++)
    *cptr_d=*cptr_s;
  *cptr_d='\0';

  cmd = command_named(command, FALSE);
  if (cmd == CMD_AMBIGUOUS) {
    cmd = command_named(command, TRUE);
    cmd_reply(cmd, caller, C_SYNTAX,
	_("Warning: '%s' interpreted as '%s', but it is ambiguous."
	  "  Try '%shelp'."),
	command, commands[cmd].name, caller?"/":"");
  } else if (cmd == CMD_UNRECOGNIZED) {
    cmd_reply(cmd, caller, C_SYNTAX,
	_("Unknown command.  Try '%shelp'."), caller?"/":"");
    return FALSE;
  }

  /* Use a vote to elevate access from info to ctrl? */
  if (caller 
      && caller->player
      && !check
      && !caller->player->is_observer
      && caller->access_level == ALLOW_INFO
      && access_level(cmd) == ALLOW_CTRL) {
    int idx = caller->player->player_no;

    /* If we already have a vote going, cancel it in favour of the new
     * vote command. You can only have one vote at a time. */
    if (votes[idx].command[0] != '\0') {
      cmd_reply(CMD_VOTE, caller, C_COMMENT,
		_("Your new vote cancelled your "
		  "previous vote."));
      votes[idx].command[0] = '\0';
    }

    /* Check if the vote command would succeed. */
    if (handle_stdin_input(caller, full_command, TRUE)) {
      last_vote++;
      notify_conn(NULL, _("New vote, no. %d, by %s: %s."), last_vote, 
		  caller->player->name, full_command);
      sz_strlcpy(votes[idx].command, full_command);
      votes[idx].vote_no = last_vote;
      votes[idx].full_turn = FALSE; /* just to be sure */
      memset(votes[idx].votes_cast, VOTE_NONE, sizeof(votes[idx].votes_cast));
      votes[idx].votes_cast[idx] = VOTE_YES; /* vote on your own suggestion */
      check_vote(&votes[idx]); /* update vote numbers, maybe auto-accept */
      return TRUE;
    } else {
      cmd_reply(CMD_VOTE, caller, C_FAIL, "Your new vote (\"%s\") was not "
                "legal or was not recognized.", full_command);
      return FALSE;
    }
  }
  if (caller
      && !(check && caller->access_level >= ALLOW_INFO 
           && access_level(cmd) == ALLOW_CTRL)
      && caller->access_level < access_level(cmd)) {
    cmd_reply(cmd, caller, C_FAIL,
	      _("You are not allowed to use this command."));
    return FALSE;
  }

  cptr_s = skip_leading_spaces(cptr_s);
  sz_strlcpy(arg, cptr_s);

  cut_comment(arg);

  /* keep this before we cut everything after a space */
  sz_strlcpy(allargs, cptr_s);
  cut_comment(allargs);

  i=strlen(arg)-1;
  while(i>0 && my_isspace(arg[i]))
    arg[i--]='\0';

  if (!check && commands[cmd].game_level > ALLOW_INFO) {
    /*
     * this command will affect the game - inform all players.
     * We quite purposely do not use access_level() here.
     *
     * use command,arg instead of str because of the trailing
     * newline in str when it comes from the server command line
     */
    notify_conn(NULL, "%s: '%s %s'",
      caller ? caller->username : _("(server prompt)"), command, arg);
  }

  switch(cmd) {
  case CMD_REMOVE:
    return remove_player(caller, arg, check);
  case CMD_SAVE:
    return save_command(caller,arg, check);
  case CMD_LOAD:
    return load_command(caller, arg, check);
  case CMD_METAPATCHES:
    return metapatches_command(caller, arg, check);
  case CMD_METATOPIC:
    return metatopic_command(caller, arg, check);
  case CMD_METAMESSAGE:
    return metamessage_command(caller, arg, check);
  case CMD_METACONN:
    return metaconnection_command(caller, arg, check);
  case CMD_METASERVER:
    return metaserver_command(caller, arg, check);
  case CMD_HELP:
    return show_help(caller, arg);
  case CMD_SRVID:
    return show_serverid(caller, arg);
  case CMD_LIST:
    return show_list(caller, arg);
  case CMD_AITOGGLE:
    return toggle_ai_player(caller, arg, check);
  case CMD_TAKE:
    return take_command(caller, arg, check);
  case CMD_OBSERVE:
    return observe_command(caller, arg, check);
  case CMD_DETACH:
    return detach_command(caller, arg, check);
  case CMD_CREATE:
    return create_ai_player(caller, arg, check);
  case CMD_AWAY:
    return set_away(caller, arg, check);
  case CMD_NOVICE:
    return set_ai_level(caller, arg, 2, check);
  case CMD_EASY:
    return set_ai_level(caller, arg, 3, check);
  case CMD_NORMAL:
    return set_ai_level(caller, arg, 5, check);
  case CMD_HARD:
    return set_ai_level(caller, arg, 7, check);
  case CMD_EXPERIMENTAL:
    return set_ai_level(caller, arg, 10, check);
  case CMD_QUIT:
    return quit_game(caller, check);
  case CMD_CUT:
    return cut_client_connection(caller, arg, check);
  case CMD_SHOW:
    return show_command(caller, arg, check);
  case CMD_EXPLAIN:
    return explain_option(caller, arg, check);
  case CMD_DEBUG:
    return debug_command(caller, arg, check);
  case CMD_SET:
    return set_command(caller, arg, check);
  case CMD_TEAM:
    return team_command(caller, arg, check);
  case CMD_RULESETDIR:
    return set_rulesetdir(caller, arg, check);
  case CMD_SCORE:
    if (server_state == RUN_GAME_STATE || server_state == GAME_OVER_STATE) {
      if (!check) {
        report_progress_scores();
      }
      return TRUE;
    } else {
      cmd_reply(cmd, caller, C_SYNTAX,
		_("The game must be running before you can see the score."));
      return FALSE;
    }
  case CMD_WALL:
    return wall(arg, check);
  case CMD_VOTE:
    return vote_command(caller, arg, check);
  case CMD_READ_SCRIPT:
    return read_command(caller, arg, check);
  case CMD_WRITE_SCRIPT:
    return write_command(caller, arg, check);
  case CMD_RFCSTYLE:	/* see console.h for an explanation */
    if (!check) {
      con_set_style(!con_get_style());
    }
    return TRUE;
  case CMD_CMDLEVEL:
    return cmdlevel_command(caller, arg, check);
  case CMD_FIRSTLEVEL:
    return firstlevel_command(caller, check);
  case CMD_TIMEOUT:
    return timeout_command(caller, allargs, check);
  case CMD_START_GAME:
    return start_command(caller, arg, check);
  case CMD_END_GAME:
    return end_command(caller, arg, check);
  case CMD_NUM:
  case CMD_UNRECOGNIZED:
  case CMD_AMBIGUOUS:
  default:
    freelog(LOG_FATAL, "bug in civserver: impossible command recognized; bye!");
    assert(0);
  }
  return FALSE; /* should NEVER happen but we need to satisfy some compilers */
}

/**************************************************************************
  End the game and accord victory to the listed players, if any.
**************************************************************************/
static bool end_command(struct connection *caller, char *str, bool check)
{
  if (server_state == RUN_GAME_STATE) {
    char *arg[MAX_NUM_PLAYERS];
    int ntokens = 0, i;
    enum m_pre_result plr_result;
    bool result = TRUE;
    char buf[MAX_LEN_CONSOLE_LINE];

    if (str != NULL || strlen(str) > 0) {
      sz_strlcpy(buf, str);
      ntokens = get_tokens(buf, arg, MAX_NUM_PLAYERS, TOKEN_DELIMITERS);
    }
    /* Ensure players exist */
    for (i = 0; i < ntokens; i++) {
      struct player *pplayer = find_player_by_name_prefix(arg[i], &plr_result);

      if (!pplayer) {
        cmd_reply_no_such_player(CMD_TEAM, caller, arg[i], plr_result);
        result = FALSE;
        goto cleanup;
      } else if (pplayer->is_alive == FALSE) {
        cmd_reply(CMD_END_GAME, caller, C_FAIL, _("But %s is dead!"),
                  pplayer->name);
        result = FALSE;
        goto cleanup;
      }
    }
    if (check) {
      goto cleanup;
    }
    if (ntokens > 0) {
      /* Mark players for victory. */
      for (i = 0; i < ntokens; i++) {
        BV_SET(srvarg.draw, 
               find_player_by_name_prefix(arg[i], &plr_result)->player_no);
      }
    }
    server_state = GAME_OVER_STATE;
    force_end_of_sniff = TRUE;
    cmd_reply(CMD_END_GAME, caller, C_OK,
              _("Ending the game. The server will restart once all clients "
              "have disconnected."));

    cleanup:
    for (i = 0; i < ntokens; i++) {
      free(arg[i]);
    }
    return TRUE;
  } else {
    cmd_reply(CMD_END_GAME, caller, C_FAIL, 
              _("Cannot end the game: no game running."));
    return FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
static bool start_command(struct connection *caller, char *name, bool check)
{
  switch (server_state) {
  case PRE_GAME_STATE:
    /* Sanity check scenario */
    if (game.is_new_game && !check) {
      if (map.num_start_positions > 0
	  && game.max_players > map.num_start_positions) {
	/* If we load a pre-generated map (i.e., a scenario) it is possible
	 * to increase the number of players beyond the number supported by
	 * the scenario.  The solution is a hack: cut the extra players
	 * when the game starts. */
	freelog(LOG_VERBOSE, "Reduced maxplayers from %i to %i to fit "
	        "to the number of start positions.",
		game.max_players, map.num_start_positions);
	game.max_players = map.num_start_positions;
      }

      if (get_num_nonobserver_players() > game.max_players) {
	/* Because of the way player ids are renumbered during
	   server_remove_player() this is correct */
        while (get_num_nonobserver_players() > game.max_players) {
	  /* This may erronously remove observer players sometimes.  This
	   * is a bug but non-fatal. */
	  server_remove_player(get_player(game.max_players));
        }

	freelog(LOG_VERBOSE,
		"Had to cut down the number of players to the "
		"number of map start positions, there must be "
		"something wrong with the savegame or you "
		"adjusted the maxplayers value.");
      }
    }

    /* check min_players */
    if (get_num_nonobserver_players() < game.min_players) {
      cmd_reply(CMD_START_GAME, caller, C_FAIL,
		_("Not enough players, game will not start."));
      return FALSE;
    } else if (check) {
      return TRUE;
    } else if (!caller) {
      start_game();
      return TRUE;
    } else if (!caller->player || !caller->player->is_connected) {
      /* A detached or observer player can't do /start. */
      return TRUE;
    } else {
      int started = 0, notstarted = 0;
      const int percent_required = 100;

      /* Note this is called even if the player has pressed /start once
       * before.  This is a good thing given that no other code supports
       * is_started yet.  For instance if a player leaves everyone left
       * might have pressed /start already but the start won't happen
       * until someone presses it again.  Also you can press start more
       * than once to remind other people to start (which is a good thing
       * until somebody does it too much and it gets labeled as spam). */
      caller->player->is_started = TRUE;
      players_iterate(pplayer) {
	if (pplayer->is_connected) {
	  if (pplayer->is_started) {
	    started++;
	  } else {
	    notstarted++;
	  }
	}
      } players_iterate_end;
      if (started * 100 < (started + notstarted) * percent_required) {
	notify_conn(NULL, _("Waiting to start game: %d out of %d players "
			    "are ready to start."),
		    started, started + notstarted);
	return TRUE;
      }
      notify_conn(NULL, _("All players are ready; starting game."));
      start_game();
      return TRUE;
    }
  case GAME_OVER_STATE:
    /* TRANS: given when /start is invoked during gameover. */
    cmd_reply(CMD_START_GAME, caller, C_FAIL,
              _("Cannot start the game: the game is waiting for all clients "
              "to disconnect."));
    return FALSE;
  case SELECT_RACES_STATE:
    /* TRANS: given when /start is invoked during nation selection. */
    cmd_reply(CMD_START_GAME, caller, C_FAIL,
              _("Cannot start the game: it has already been started."));
    return FALSE;
  case RUN_GAME_STATE:
    /* TRANS: given when /start is invoked while the game is running. */
    cmd_reply(CMD_START_GAME, caller, C_FAIL,
              _("Cannot start the game: it is already running."));
    return FALSE;
  }
  assert(FALSE);
  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static bool cut_client_connection(struct connection *caller, char *name, 
                                  bool check)
{
  enum m_pre_result match_result;
  struct connection *ptarget;
  struct player *pplayer;

  ptarget = find_conn_by_user_prefix(name, &match_result);

  if (!ptarget) {
    cmd_reply_no_such_conn(CMD_CUT, caller, name, match_result);
    return FALSE;
  } else if (check) {
    return TRUE;
  }

  pplayer = ptarget->player;

  cmd_reply(CMD_CUT, caller, C_DISCONNECTED,
	    _("Cutting connection %s."), ptarget->username);
  lost_connection_to_client(ptarget);
  close_connection(ptarget);

  /* if we cut the connection, unassign the login name */
  if (pplayer) {
    sz_strlcpy(pplayer->username, ANON_USER_NAME);
  }
  return TRUE;
}

/**************************************************************************
 Show caller introductory help about the server.
 help_cmd is the command the player used.
**************************************************************************/
static void show_help_intro(struct connection *caller, enum command_id help_cmd)
{
  /* This is formated like extra_help entries for settings and commands: */
  const char *help =
    _("Welcome - this is the introductory help text for the Freeciv server.\n\n"
      "Two important server concepts are Commands and Options.\n"
      "Commands, such as 'help', are used to interact with the server.\n"
      "Some commands take one or more arguments, separated by spaces.\n"
      "In many cases commands and command arguments may be abbreviated.\n"
      "Options are settings which control the server as it is running.\n\n"
      "To find out how to get more information about commands and options,\n"
      "use 'help help'.\n\n"
      "For the impatient, the main commands to get going are:\n"
      "  show   -  to see current options\n"
      "  set    -  to set options\n"
      "  start  -  to start the game once players have connected\n"
      "  save   -  to save the current game\n"
      "  quit   -  to exit");

  static struct astring abuf = ASTRING_INIT;
      
  astr_minsize(&abuf, strlen(help)+1);
  strcpy(abuf.str, help);
  wordwrap_string(abuf.str, 78);
  cmd_reply(help_cmd, caller, C_COMMENT, abuf.str);
}

/**************************************************************************
 Show the caller detailed help for the single COMMAND given by id.
 help_cmd is the command the player used.
**************************************************************************/
static void show_help_command(struct connection *caller,
			      enum command_id help_cmd,
			      enum command_id id)
{
  const struct command *cmd = &commands[id];
  
  if (cmd->short_help) {
    cmd_reply(help_cmd, caller, C_COMMENT,
	      "%s %s  -  %s", _("Command:"), cmd->name, _(cmd->short_help));
  } else {
    cmd_reply(help_cmd, caller, C_COMMENT,
	      "%s %s", _("Command:"), cmd->name);
  }
  if (cmd->synopsis) {
    /* line up the synopsis lines: */
    const char *syn = _("Synopsis: ");
    size_t synlen = strlen(syn);
    char prefix[40];

    my_snprintf(prefix, sizeof(prefix), "%*s", (int) synlen, " ");
    cmd_reply_prefix(help_cmd, caller, C_COMMENT, prefix,
		     "%s%s", syn, _(cmd->synopsis));
  }
  cmd_reply(help_cmd, caller, C_COMMENT,
	    _("Level: %s"), cmdlevel_name(cmd->game_level));
  if (cmd->game_level != cmd->pregame_level) {
    cmd_reply(help_cmd, caller, C_COMMENT,
	      _("Pregame level: %s"), cmdlevel_name(cmd->pregame_level));
  }
  if (cmd->extra_help) {
    static struct astring abuf = ASTRING_INIT;
    const char *help = _(cmd->extra_help);
      
    astr_minsize(&abuf, strlen(help)+1);
    strcpy(abuf.str, help);
    wordwrap_string(abuf.str, 76);
    cmd_reply(help_cmd, caller, C_COMMENT, _("Description:"));
    cmd_reply_prefix(help_cmd, caller, C_COMMENT, "  ",
		     "  %s", abuf.str);
  }
}

/**************************************************************************
 Show the caller list of COMMANDS.
 help_cmd is the command the player used.
**************************************************************************/
static void show_help_command_list(struct connection *caller,
				  enum command_id help_cmd)
{
  enum command_id i;
  
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
  cmd_reply(help_cmd, caller, C_COMMENT,
	    _("The following server commands are available:"));
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
  if(!caller && con_get_style()) {
    for (i=0; i<CMD_NUM; i++) {
      cmd_reply(help_cmd, caller, C_COMMENT, "%s", commands[i].name);
    }
  } else {
    char buf[MAX_LEN_CONSOLE_LINE];
    int j;
    
    buf[0] = '\0';
    for (i=0, j=0; i<CMD_NUM; i++) {
      if (may_use(caller, i)) {
	cat_snprintf(buf, sizeof(buf), "%-19s", commands[i].name);
	if((++j % 4) == 0) {
	  cmd_reply(help_cmd, caller, C_COMMENT, buf);
	  buf[0] = '\0';
	}
      }
    }
    if (buf[0] != '\0')
      cmd_reply(help_cmd, caller, C_COMMENT, buf);
  }
  cmd_reply(help_cmd, caller, C_COMMENT, horiz_line);
}

/**************************************************************************
  Additional 'help' arguments
**************************************************************************/
enum HELP_GENERAL_ARGS { HELP_GENERAL_COMMANDS, HELP_GENERAL_OPTIONS,
			 HELP_GENERAL_NUM /* Must be last */ };
static const char * const help_general_args[] = {
  "commands", "options", NULL
};

/**************************************************************************
  Unified indices for help arguments:
    CMD_NUM           -  Server commands
    HELP_GENERAL_NUM  -  General help arguments, above
    SETTINGS_NUM      -  Server options 
**************************************************************************/
#define HELP_ARG_NUM (CMD_NUM + HELP_GENERAL_NUM + SETTINGS_NUM)

/**************************************************************************
  Convert unified helparg index to string; see above.
**************************************************************************/
static const char *helparg_accessor(int i) {
  if (i<CMD_NUM)
    return cmdname_accessor(i);
  i -= CMD_NUM;
  if (i<HELP_GENERAL_NUM)
    return help_general_args[i];
  i -= HELP_GENERAL_NUM;
  return optname_accessor(i);
}
/**************************************************************************
...
**************************************************************************/
static bool show_help(struct connection *caller, char *arg)
{
  enum m_pre_result match_result;
  int ind;

  assert(!may_use_nothing(caller));
    /* no commands means no help, either */

  match_result = match_prefix(helparg_accessor, HELP_ARG_NUM, 0,
			      mystrncasecmp, arg, &ind);

  if (match_result==M_PRE_EMPTY) {
    show_help_intro(caller, CMD_HELP);
    return FALSE;
  }
  if (match_result==M_PRE_AMBIGUOUS) {
    cmd_reply(CMD_HELP, caller, C_FAIL,
	      _("Help argument '%s' is ambiguous."), arg);
    return FALSE;
  }
  if (match_result==M_PRE_FAIL) {
    cmd_reply(CMD_HELP, caller, C_FAIL,
	      _("No match for help argument '%s'."), arg);
    return FALSE;
  }

  /* other cases should be above */
  assert(match_result < M_PRE_AMBIGUOUS);
  
  if (ind < CMD_NUM) {
    show_help_command(caller, CMD_HELP, ind);
    return TRUE;
  }
  ind -= CMD_NUM;
  
  if (ind == HELP_GENERAL_OPTIONS) {
    show_help_option_list(caller, CMD_HELP);
    return TRUE;
  }
  if (ind == HELP_GENERAL_COMMANDS) {
    show_help_command_list(caller, CMD_HELP);
    return TRUE;
  }
  ind -= HELP_GENERAL_NUM;
  
  if (ind < SETTINGS_NUM) {
    show_help_option(caller, CMD_HELP, ind);
    return TRUE;
  }
  
  /* should have finished by now */
  freelog(LOG_ERROR, "Bug in show_help!");
  return FALSE;
}

/**************************************************************************
  'list' arguments
**************************************************************************/
enum LIST_ARGS { LIST_PLAYERS, LIST_CONNECTIONS,
		 LIST_ARG_NUM /* Must be last */ };
static const char * const list_args[] = {
  "players", "connections", NULL
};
static const char *listarg_accessor(int i) {
  return list_args[i];
}

/**************************************************************************
  Show list of players or connections, or connection statistics.
**************************************************************************/
static bool show_list(struct connection *caller, char *arg)
{
  enum m_pre_result match_result;
  int ind;

  remove_leading_trailing_spaces(arg);
  match_result = match_prefix(listarg_accessor, LIST_ARG_NUM, 0,
			      mystrncasecmp, arg, &ind);

  if (match_result > M_PRE_EMPTY) {
    cmd_reply(CMD_LIST, caller, C_SYNTAX,
	      _("Bad list argument: '%s'.  Try '%shelp list'."),
	      arg, (caller?"/":""));
    return FALSE;
  }

  if (match_result == M_PRE_EMPTY) {
    ind = LIST_PLAYERS;
  }

  switch(ind) {
  case LIST_PLAYERS:
    show_players(caller);
    return TRUE;
  case LIST_CONNECTIONS:
    show_connections(caller);
    return TRUE;
  default:
    cmd_reply(CMD_LIST, caller, C_FAIL,
	      "Internal error: ind %d in show_list", ind);
    freelog(LOG_ERROR, "Internal error: ind %d in show_list", ind);
    return FALSE;
  }
}

/**************************************************************************
...
**************************************************************************/
void show_players(struct connection *caller)
{
  char buf[MAX_LEN_CONSOLE_LINE], buf2[MAX_LEN_CONSOLE_LINE];
  int n;

  cmd_reply(CMD_LIST, caller, C_COMMENT, _("List of players:"));
  cmd_reply(CMD_LIST, caller, C_COMMENT, horiz_line);


  if (game.nplayers == 0)
    cmd_reply(CMD_LIST, caller, C_WARNING, _("<no players>"));
  else
  {
    players_iterate(pplayer) {

      /* Low access level callers don't get to see barbarians in list: */
      if (is_barbarian(pplayer) && caller
	  && (caller->access_level < ALLOW_CTRL)) {
	continue;
      }

      /* buf2 contains stuff in brackets after playername:
       * [username,] AI/Barbarian/Human [,Dead] [, skill level] [, nation]
       */
      buf2[0] = '\0';
      if (strlen(pplayer->username) > 0
	  && strcmp(pplayer->username, "nouser") != 0) {
	my_snprintf(buf2, sizeof(buf2), _("user %s, "), pplayer->username);
      }
      
      if (is_barbarian(pplayer)) {
	sz_strlcat(buf2, _("Barbarian"));
      } else if (pplayer->ai.control) {
	sz_strlcat(buf2, _("AI"));
      } else {
	sz_strlcat(buf2, _("Human"));
      }
      if (!pplayer->is_alive) {
	sz_strlcat(buf2, _(", Dead"));
      }
      if(pplayer->ai.control) {
	cat_snprintf(buf2, sizeof(buf2), _(", difficulty level %s"),
		     name_of_skill_level(pplayer->ai.skill_level));
      }
      if (!game.is_new_game) {
	cat_snprintf(buf2, sizeof(buf2), _(", nation %s"),
		     get_nation_name_plural(pplayer->nation));
      }
      if (pplayer->team != TEAM_NONE) {
        cat_snprintf(buf2, sizeof(buf2), _(", team %s"),
                     team_get_by_id(pplayer->team)->name);
      }
      if (server_state == PRE_GAME_STATE && pplayer->is_connected) {
	if (pplayer->is_started) {
	  cat_snprintf(buf2, sizeof(buf2), _(", ready"));
	} else {
	  cat_snprintf(buf2, sizeof(buf2), _(", not ready"));
	}
      }
      my_snprintf(buf, sizeof(buf), "%s (%s)", pplayer->name, buf2);
      
      n = conn_list_size(&pplayer->connections);
      if (n > 0) {
        cat_snprintf(buf, sizeof(buf), 
                     PL_(" %d connection:", " %d connections:", n), n);
      }
      cmd_reply(CMD_LIST, caller, C_COMMENT, "%s", buf);
      
      conn_list_iterate(pplayer->connections, pconn) {
	if (!pconn->used) {
	  /* A bug that we haven't been able to trace leaves unused
	   * connections on the lists.  We skip them. */
	  continue;
	}
	my_snprintf(buf, sizeof(buf),
		    _("  %s from %s (command access level %s), bufsize=%dkb"),
		    pconn->username, pconn->addr, 
		    cmdlevel_name(pconn->access_level),
		    (pconn->send_buffer->nsize>>10));
	if (pconn->observer) {
	  sz_strlcat(buf, _(" (observer mode)"));
	}
	cmd_reply(CMD_LIST, caller, C_COMMENT, "%s", buf);
      } conn_list_iterate_end;
    } players_iterate_end;
  }
  cmd_reply(CMD_LIST, caller, C_COMMENT, horiz_line);
}

/**************************************************************************
  List connections; initially mainly for debugging
**************************************************************************/
static void show_connections(struct connection *caller)
{
  char buf[MAX_LEN_CONSOLE_LINE];
  
  cmd_reply(CMD_LIST, caller, C_COMMENT, _("List of connections to server:"));
  cmd_reply(CMD_LIST, caller, C_COMMENT, horiz_line);

  if (conn_list_size(&game.all_connections) == 0) {
    cmd_reply(CMD_LIST, caller, C_WARNING, _("<no connections>"));
  }
  else {
    conn_list_iterate(game.all_connections, pconn) {
      sz_strlcpy(buf, conn_description(pconn));
      if (pconn->established) {
	cat_snprintf(buf, sizeof(buf), " command access level %s",
		     cmdlevel_name(pconn->access_level));
      }
      cmd_reply(CMD_LIST, caller, C_COMMENT, "%s", buf);
    }
    conn_list_iterate_end;
  }
  cmd_reply(CMD_LIST, caller, C_COMMENT, horiz_line);
}

#ifdef HAVE_LIBREADLINE
/********************* RL completion functions ***************************/
/* To properly complete both commands, player names, options and filenames
   there is one array per type of completion with the commands that
   the type is relevant for.
*/

/**************************************************************************
  A generalised generator function: text and state are "standard"
  parameters to a readline generator function;
  num is number of possible completions, and index2str is a function
  which returns each possible completion string by index.
**************************************************************************/
static char *generic_generator(const char *text, int state, int num,
			       const char*(*index2str)(int))
{
  static int list_index, len;
  const char *name;
  char *mytext = local_to_internal_string_malloc(text);

  /* This function takes a string (text) in the local format and must return
   * a string in the local format.  However comparisons are done against
   * names that are in the internal format (UTF-8).  Thus we have to convert
   * the text function from the local to the internal format before doing
   * the comparison, and convert the string we return *back* to the
   * local format when returning it. */

  /* If this is a new word to complete, initialize now.  This includes
     saving the length of TEXT for efficiency, and initializing the index
     variable to 0. */
  if (state == 0) {
    list_index = 0;
    len = strlen (mytext);
  }

  /* Return the next name which partially matches: */
  while (list_index < num) {
    name = index2str(list_index);
    list_index++;

    if (mystrncasecmp (name, mytext, len) == 0) {
      free(mytext);
      return internal_to_local_string_malloc(name);
    }
  }
  free(mytext);

  /* If no names matched, then return NULL. */
  return ((char *)NULL);
}

/**************************************************************************
The valid commands at the root of the prompt.
**************************************************************************/
static char *command_generator(const char *text, int state)
{
  return generic_generator(text, state, CMD_NUM, cmdname_accessor);
}

/**************************************************************************
The valid arguments to "set" and "explain"
**************************************************************************/
static char *option_generator(const char *text, int state)
{
  return generic_generator(text, state, SETTINGS_NUM, optname_accessor);
}

/**************************************************************************
  The valid arguments to "show"
**************************************************************************/
static char *olevel_generator(const char *text, int state)
{
  return generic_generator(text, state, SETTINGS_NUM + OLEVELS_NUM,
			   olvlname_accessor);
}

/**************************************************************************
The player names.
**************************************************************************/
static const char *playername_accessor(int idx)
{
  return get_player(idx)->name;
}
static char *player_generator(const char *text, int state)
{
  return generic_generator(text, state, game.nplayers, playername_accessor);
}

/**************************************************************************
The connection user names, from game.all_connections.
**************************************************************************/
static const char *connection_name_accessor(int idx)
{
  return conn_list_get(&game.all_connections, idx)->username;
}
static char *connection_generator(const char *text, int state)
{
  return generic_generator(text, state, conn_list_size(&game.all_connections),
			   connection_name_accessor);
}

/**************************************************************************
The valid arguments for the first argument to "cmdlevel".
Extra accessor function since cmdlevel_name() takes enum argument, not int.
**************************************************************************/
static const char *cmdlevel_arg1_accessor(int idx)
{
  return cmdlevel_name(idx);
}
static char *cmdlevel_arg1_generator(const char *text, int state)
{
  return generic_generator(text, state, ALLOW_NUM, cmdlevel_arg1_accessor);
}

/**************************************************************************
The valid arguments for the second argument to "cmdlevel":
"first" or "new" or a connection name.
**************************************************************************/
static const char *cmdlevel_arg2_accessor(int idx)
{
  return ((idx==0) ? "first" :
	  (idx==1) ? "new" :
	  connection_name_accessor(idx-2));
}
static char *cmdlevel_arg2_generator(const char *text, int state)
{
  return generic_generator(text, state,
			   2 + conn_list_size(&game.all_connections),
			   cmdlevel_arg2_accessor);
}

/**************************************************************************
The valid first arguments to "help".
**************************************************************************/
static char *help_generator(const char *text, int state)
{
  return generic_generator(text, state, HELP_ARG_NUM, helparg_accessor);
}

/**************************************************************************
The valid first arguments to "list".
**************************************************************************/
static char *list_generator(const char *text, int state)
{
  return generic_generator(text, state, LIST_ARG_NUM, listarg_accessor);
}

/**************************************************************************
returns whether the characters before the start position in rl_line_buffer
is of the form [non-alpha]*cmd[non-alpha]*
allow_fluff changes the regexp to [non-alpha]*cmd[non-alpha].*
**************************************************************************/
static bool contains_str_before_start(int start, const char *cmd, bool allow_fluff)
{
  char *str_itr = rl_line_buffer;
  int cmd_len = strlen(cmd);

  while (str_itr < rl_line_buffer + start && !my_isalnum(*str_itr))
    str_itr++;

  if (mystrncasecmp(str_itr, cmd, cmd_len) != 0)
    return FALSE;
  str_itr += cmd_len;

  if (my_isalnum(*str_itr)) /* not a distinct word */
    return FALSE;

  if (!allow_fluff) {
    for (; str_itr < rl_line_buffer + start; str_itr++)
      if (my_isalnum(*str_itr))
	return FALSE;
  }

  return TRUE;
}

/**************************************************************************
...
**************************************************************************/
static bool is_command(int start)
{
  char *str_itr;

  if (contains_str_before_start(start, commands[CMD_HELP].name, FALSE))
    return TRUE;

  /* if there is only it is also OK */
  str_itr = rl_line_buffer;
  while (str_itr - rl_line_buffer < start) {
    if (my_isalnum(*str_itr))
      return FALSE;
    str_itr++;
  }
  return TRUE;
}

/**************************************************************************
Commands that may be followed by a player name
**************************************************************************/
static const int player_cmd[] = {
  CMD_AITOGGLE,
  CMD_NOVICE,
  CMD_EASY,
  CMD_NORMAL,
  CMD_HARD,
  CMD_EXPERIMENTAL,
  CMD_REMOVE,
  CMD_TEAM,
  -1
};

/**************************************************************************
number of tokens in rl_line_buffer before start
**************************************************************************/
static int num_tokens(int start)
{
  int res = 0;
  bool alnum = FALSE;
  char *chptr = rl_line_buffer;

  while (chptr - rl_line_buffer < start) {
    if (my_isalnum(*chptr)) {
      if (!alnum) {
	alnum = TRUE;
	res++;
      }
    } else {
      alnum = FALSE;
    }
    chptr++;
  }

  return res;
}

/**************************************************************************
...
**************************************************************************/
static bool is_player(int start)
{
  int i = 0;

  while (player_cmd[i] != -1) {
    if (contains_str_before_start(start, commands[player_cmd[i]].name, FALSE)) {
      return TRUE;
    }
    i++;
  }

  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static bool is_connection(int start)
{
  return contains_str_before_start(start, commands[CMD_CUT].name, FALSE);
}

/**************************************************************************
...
**************************************************************************/
static bool is_cmdlevel_arg2(int start)
{
  return (contains_str_before_start(start, commands[CMD_CMDLEVEL].name, TRUE)
	  && num_tokens(start) == 2);
}

/**************************************************************************
...
**************************************************************************/
static bool is_cmdlevel_arg1(int start)
{
  return contains_str_before_start(start, commands[CMD_CMDLEVEL].name, FALSE);
}

/**************************************************************************
  Commands that may be followed by a server option name

  CMD_SHOW is handled by option_level_cmd, which is for both option levels
  and server options
**************************************************************************/
static const int server_option_cmd[] = {
  CMD_EXPLAIN,
  CMD_SET,
  -1
};

/**************************************************************************
  Returns TRUE if the readline buffer string matches a server option at
  the given position.
**************************************************************************/
static bool is_server_option(int start)
{
  int i = 0;

  while (server_option_cmd[i] != -1) {
    if (contains_str_before_start(start, commands[server_option_cmd[i]].name,
				  FALSE)) {
      return TRUE;
    }
    i++;
  }

  return FALSE;
}

/**************************************************************************
  Commands that may be followed by an option level or server option
**************************************************************************/
static const int option_level_cmd[] = {
  CMD_SHOW,
  -1
};

/**************************************************************************
  Returns true if the readline buffer string matches an option level or an
  option at the given position.
**************************************************************************/
static bool is_option_level(int start)
{
  int i = 0;

  while (option_level_cmd[i] != -1) {
    if (contains_str_before_start(start, commands[option_level_cmd[i]].name,
				  FALSE)) {
      return TRUE;
    }
    i++;
  }

  return FALSE;
}

/**************************************************************************
Commands that may be followed by a filename
**************************************************************************/
static const int filename_cmd[] = {
  CMD_LOAD,
  CMD_SAVE,
  CMD_READ_SCRIPT,
  CMD_WRITE_SCRIPT,
  -1
};

/**************************************************************************
...
**************************************************************************/
static bool is_filename(int start)
{
  int i = 0;

  while (filename_cmd[i] != -1) {
    if (contains_str_before_start
	(start, commands[filename_cmd[i]].name, FALSE)) {
      return TRUE;
    }
    i++;
  }

  return FALSE;
}

/**************************************************************************
...
**************************************************************************/
static bool is_help(int start)
{
  return contains_str_before_start(start, commands[CMD_HELP].name, FALSE);
}

/**************************************************************************
...
**************************************************************************/
static bool is_list(int start)
{
  return contains_str_before_start(start, commands[CMD_LIST].name, FALSE);
}

/**************************************************************************
Attempt to complete on the contents of TEXT.  START and END bound the
region of rl_line_buffer that contains the word to complete.  TEXT is
the word to complete.  We can use the entire contents of rl_line_buffer
in case we want to do some simple parsing.  Return the array of matches,
or NULL if there aren't any.
**************************************************************************/
#ifdef HAVE_NEWLIBREADLINE
char **freeciv_completion(const char *text, int start, int end)
#else
char **freeciv_completion(char *text, int start, int end)
#endif
{
  char **matches = (char **)NULL;

  if (is_help(start)) {
    matches = completion_matches(text, help_generator);
  } else if (is_command(start)) {
    matches = completion_matches(text, command_generator);
  } else if (is_list(start)) {
    matches = completion_matches(text, list_generator);
  } else if (is_cmdlevel_arg2(start)) {
    matches = completion_matches(text, cmdlevel_arg2_generator);
  } else if (is_cmdlevel_arg1(start)) {
    matches = completion_matches(text, cmdlevel_arg1_generator);
  } else if (is_connection(start)) {
    matches = completion_matches(text, connection_generator);
  } else if (is_player(start)) {
    matches = completion_matches(text, player_generator);
  } else if (is_server_option(start)) {
    matches = completion_matches(text, option_generator);
  } else if (is_option_level(start)) {
    matches = completion_matches(text, olevel_generator);
  } else if (is_filename(start)) {
    /* This function we get from readline */
    matches = completion_matches(text, filename_completion_function);
  } else /* We have no idea what to do */
    matches = NULL;

  /* Don't automatically try to complete with filenames */
  rl_attempted_completion_over = 1;

  return (matches);
}

#endif /* HAVE_LIBREADLINE */

/********************************************************************
Returns whether the specified server setting (option) can currently
be changed.  Does not indicate whether it can be changed by clients.
*********************************************************************/
bool sset_is_changeable(int idx)
{
  struct settings_s *op = &settings[idx];

  switch(op->sclass) {
  case SSET_MAP_SIZE:
  case SSET_MAP_GEN:
    /* Only change map options if we don't yet have a map: */
    return map_is_empty();
  case SSET_MAP_ADD:
  case SSET_PLAYERS:
  case SSET_GAME_INIT:

  case SSET_RULES:
    /* Only change start params and most rules if we don't yet have a map,
     * or if we do have a map but its a scenario one (ie, the game has
     * never actually been started).
     */
    return (map_is_empty() || game.is_new_game);
  case SSET_RULES_FLEXIBLE:
  case SSET_META:
    /* These can always be changed: */
    return TRUE;
  default:
    freelog(LOG_ERROR, "Unexpected case %d in %s line %d",
            op->sclass, __FILE__, __LINE__);
    return FALSE;
  }
}

