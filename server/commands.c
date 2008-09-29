/********************************************************************** 
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fcintl.h"

#include "connection.h"

#include "commands.h"

struct command {
  const char *name;       /* name - will be matched by unique prefix   */
  enum cmdlevel_id level; /* access level required to use the command  */
  const char *synopsis;	  /* one or few-line summary of usage */
  const char *short_help; /* one line (about 70 chars) description */
  const char *extra_help; /* extra help information; will be line-wrapped */
};

/* Commands must match the values in enum command_id. */
static struct command commands[] = {
  {"start",	ALLOW_INFO,
   /* no translatable parameters */
   "start",
   N_("Start the game, or restart after loading a savegame."),
   N_("This command starts the game.  When starting a new game, "
      "it should be used after all human players have connected, and "
      "AI players have been created (if required), and any desired "
      "changes to initial server options have been made.  "
      "After 'start', each human player will be able to "
      "choose their nation, and then the game will begin.  "
      "This command is also required after loading a savegame "
      "for the game to recommence.  Once the game is running this command "
      "is no longer available, since it would have no effect.")
  },

  {"help",	ALLOW_INFO,
   /* TRANS: translate text between <> only */
   N_("help\n"
      "help commands\n"
      "help options\n"
      "help <command-name>\n"
      "help <option-name>"),
   N_("Show help about server commands and server options."),
   N_("With no arguments gives some introductory help.  "
      "With argument \"commands\" or \"options\" gives respectively "
      "a list of all commands or all options.  "
      "Otherwise the argument is taken as a command name or option name, "
      "and help is given for that command or option.  For options, the help "
      "information includes the current and default values for that option.  "
      "The argument may be abbreviated where unambiguous.")
  },

  {"list",	ALLOW_INFO,
   /* no translatable parameters */
   "list\n"
   "list players\n"
   "list teams\n"
   "list connections\n"
   "list scenarios",
   N_("Show a list of players, teams, connections, or scenarios."),
   N_("Show a list of players in the game, teams of players, connections to "
      "the server, or available scenarios.  The argument may be abbreviated,"
      " and defaults to 'players' if absent.")
  },
  {"quit",	ALLOW_HACK,
   /* no translatable parameters */
   "quit",
   N_("Quit the game and shutdown the server."), NULL
  },
  {"cut",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("cut <connection-name>"),
   N_("Cut a client's connection to server."),
   N_("Cut specified client's connection to the server, removing that client "
      "from the game.  If the game has not yet started that client's player "
      "is removed from the game, otherwise there is no effect on the player.  "
      "Note that this command now takes connection names, not player names.")
  },
  {"explain",	ALLOW_INFO,
   /* TRANS: translate text between <> only */
   N_("explain\n"
      "explain <option-name>"),
   N_("Explain server options."),
   N_("The 'explain' command gives a subset of the functionality of 'help', "
      "and is included for backward compatibility.  With no arguments it "
      "gives a list of options (like 'help options'), and with an argument "
      "it gives help for a particular option (like 'help <option-name>').")
  },
  {"show",	ALLOW_INFO,
   /* TRANS: translate text between <> only */
   N_("show\n"
      "show <option-name>\n"
      "show <option-prefix>"),
   N_("Show server options."),
   N_("With no arguments, shows all server options (or available options, when "
      "used by clients).  With an argument, show only the named option, "
      "or options with that prefix.")
  },
  {"wall",	ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("wall <message>"),
   N_("Send message to all connections."),
   N_("For each connected client, pops up a window showing the message "
      "entered.")
  },
  {"vote",	ALLOW_INFO,
   /* TRANS: translate text between [] only */
   N_("vote yes|no|abstain [vote number]"),
   N_("Cast a vote."),
      /* xgettext:no-c-format */
   N_("A player with info level access issuing a control level command "
      "starts a new vote for the command.  The /vote command followed by "
      "\"yes\", \"no\", or \"abstain\", and optionally a vote number, "
      "gives your vote.  If you do not add a vote number, your vote applies "
      "to the latest vote.  You can only suggest one vote at a time.  "
      "The vote will pass immediately if more than half of the voters "
      "who have not abstained vote for it, or fail immediately if at "
      "least half of the voters who have not abstained vote against it.")
  },
  {"debug",	ALLOW_CTRL,
   /* no translatable parameters */
   "debug [ diplomacy | ferries | player <player> | tech <player>"
   " | city <x> <y> | units <x> <y> | unit <id> "
   " | timing | info ]",
   N_("Turn on or off AI debugging of given entity."),
   N_("Print AI debug information about given entity and turn continous "
      "debugging output for this entity on or off."),
  },
  {"set",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("set <option-name> <value>"),
   N_("Set server option."), NULL
  },
  {"team",	ALLOW_CTRL,
   /* TRANS: translate text between <> and [] only */
   N_("team <player> [team]"),
   N_("Change, add or remove a player's team affiliation."),
   N_("Sets a player as member of a team. If no team specified, the "
      "player is set teamless. Use \"\" if names contain whitespace. "
      "A team is a group of players that start out allied, with shared "
      "vision and embassies, and fight together to achieve team victory "
      "with averaged individual scores.")
  },
  {"rulesetdir", ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("rulesetdir <directory>"),
   N_("Choose new ruleset directory or modpack."),
   N_("Choose new ruleset directory or modpack. Calling this\n "
      "without any arguments will show you the currently selected "
      "ruleset.")
  },
  {"metamessage", ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("metainfo <meta-line>"),
   N_("Set metaserver info line."),
   N_("Set user defined metaserver info line. If parameter is omitted,\n"
      "previously set metamessage will be removed. For most of the time\n"
      "user defined metamessage will be used instead of automatically\n"
      "generated messages, if it is available.")
  },
  {"metapatches", ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("metapatch <meta-line>"),
   N_("Set metaserver patches line."), NULL
  },
  {"metaconnection",	ALLOW_HACK,
   /* no translatable parameters */
   "metaconnection u|up\n"
   "metaconnection d|down\n"
   "metaconnection ?",
   N_("Control metaserver connection."),
   N_("'metaconnection ?' reports on the status of the connection to metaserver.\n"
      "'metaconnection down' or 'metac d' brings the metaserver connection down.\n"
      "'metaconnection up' or 'metac u' brings the metaserver connection up.")
  },
  {"metaserver",ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("metaserver <address>"),
   N_("Set address (URL) for metaserver to report to."), NULL
  },
  {"aitoggle",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("aitoggle <player-name>"),
   N_("Toggle AI status of player."), NULL
  },
  {"take",    ALLOW_INFO,
   /* TRANS: translate text between [] and <> only */
   N_("take [connection-name] <player-name>"),
   N_("Take over a player's place in the game."),
   N_("Only the console and connections with cmdlevel 'hack' can force "
      "other connections to take over a player. If you're not one of these, "
      "only the <player-name> argument is allowed.  If '-' is given for the "
      "player name and the connection does not already control a player, one "
      "is created and assigned to the connection.")
  },
  {"observe",    ALLOW_INFO,
   /* TRANS: translate text between [] only */
   N_("observe [connection-name] [player-name]"),
   N_("Observe a player or the whole game."),
   N_("Only the console and connections with cmdlevel 'hack' can force "
      "other connections to observe a player. If you're not one of these, "
      "only the [player-name] argument is allowed. If the console gives no "
      "player-name or the connection uses no arguments, then the connection "
      "is attached to a global observer.")
  },
  {"detach",    ALLOW_INFO,
   /* TRANS: translate text between <> only */
   N_("detach <connection-name>"),
   N_("detach from a player."),
   N_("Only the console and connections with cmdlevel 'hack' can force "
      "other connections to detach from a player.")
  },
  {"create",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("create <player-name>"),
   N_("Create an AI player with a given name."),
   N_("The 'create' command is only available before the game has "
      "been started.")
  },
  {"away",	ALLOW_INFO,
   /* no translatable parameters */
   "away",
   N_("Set yourself in away mode. The AI will watch your back."),
   N_("The AI will govern your nation but do minimal changes."),
  },
  {"novice",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("novice\n"
      "novice <player-name>"),
   N_("Set one or all AI players to 'novice'."),
   N_("With no arguments, sets all AI players to skill level 'novice', and "
      "sets the default level for any new AI players to 'novice'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"easy",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("easy\n"
      "easy <player-name>"),
   N_("Set one or all AI players to 'easy'."),
   N_("With no arguments, sets all AI players to skill level 'easy', and "
      "sets the default level for any new AI players to 'easy'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"normal",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("normal\n"
      "normal <player-name>"),
   N_("Set one or all AI players to 'normal'."),
   N_("With no arguments, sets all AI players to skill level 'normal', and "
      "sets the default level for any new AI players to 'normal'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"hard",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("hard\n"
      "hard <player-name>"),
   N_("Set one or all AI players to 'hard'."),
   N_("With no arguments, sets all AI players to skill level 'hard', and "
      "sets the default level for any new AI players to 'hard'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"cheating",  ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("cheating\n"
      "cheating <player-name>"),
   N_("Set one or all AI players to 'cheating'."),
   N_("With no arguments, sets all AI players to skill level 'cheating', and "
      "sets the default level for any new AI players to 'cheating'.  With an "
      "argument, sets the skill level for that player only.")
  },
  {"experimental",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("experimental\n"
      "experimental <player-name>"),
   N_("Set one or all AI players to 'experimental'."),
   N_("With no arguments, sets all AI players to skill 'experimental', and "
      "sets the default level for any new AI players to this.  With an "
      "argument, sets the skill level for that player only. THIS IS ONLY "
      "FOR TESTING OF NEW AI FEATURES! For ordinary servers, this option "
      "has no effect.")
  },
  {"cmdlevel",	ALLOW_HACK,  /* confusing to leave this at ALLOW_CTRL */
   /* TRANS: translate text between <> only */
   N_("cmdlevel\n"
      "cmdlevel <level>\n"
      "cmdlevel <level> new\n"
      "cmdlevel <level> first\n"
      "cmdlevel <level> <connection-name>"),
   N_("Query or set command access level access."),
   N_("The command access level controls which server commands are available\n"
      "to users via the client chatline.  The available levels are:\n"
      "    none  -  no commands\n"
      "    info  -  informational commands only\n"
      "    ctrl  -  commands that affect the game and users\n"
      "    hack  -  *all* commands - dangerous!\n"
      "With no arguments, the current command access levels are reported.\n"
      "With a single argument, the level is set for all existing "
      "connections,\nand the default is set for future connections.\n"
      "If 'new' is specified, the level is set for newly connecting clients.\n"
      "If 'first come' is specified, the 'first come' level is set; it will be\n"
      "granted to the first client to connect, or if there are connections\n"
      "already, the first client to issue the 'first' command.\n"
      "If a connection name is specified, the level is set for that "
      "connection only.\n"
      "Command access levels do not persist if a client disconnects, "
      "because some untrusted person could reconnect with the same name.  "
      "Note that this command now takes connection names, not player names."
      )
  },
  {"first", ALLOW_INFO,
   /* no translatable parameters */
   "first",
   N_("If there is none, become the game organizer with increased permissions."),
   NULL,
  },
  {"timeoutincrease", ALLOW_CTRL, 
   /* TRANS: translate text between <> only */
   N_("timeoutincrease <turn> <turninc> <value> <valuemult>"), 
   N_("See \"help timeoutincrease\"."),
   N_("Every <turn> turns, add <value> to timeout timer, then add <turninc> "
      "to <turn> and multiply <value> by <valuemult>.  Use this command in "
      "concert with the option \"timeout\". Defaults are 0 0 0 1")
  },
  {"endgame",	ALLOW_HACK,
   /* no translatable parameters */
   "endgame",
   N_("End the game immediately in a draw."), NULL,
  },
  {"surrender",	ALLOW_INFO,
   /* no translatable parameters */
   "surrender",
   N_("Concede the game."),
   N_("This tells everyone else that you concede the game, and if all "
      "but one player (or one team) have conceded the game in this way "
      "then the game ends."),
  },
  {"remove",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("remove <player-name>"),
   N_("Fully remove player from game."),
   N_("This *completely* removes a player from the game, including "
      "all cities and units etc.  Use with care!")
  },
  {"save",	ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("save\n"
      "save <file-name>"),
   N_("Save game to file."),
   N_("Save the current game to file <file-name>.  If no file-name "
      "argument is given saves to \"<auto-save name prefix><year>m.sav[.gz]\".\n"
      "To reload a savegame created by 'save', start the server with "
      "the command-line argument:\n"
      "    --file <filename>\n"
      "and use the 'start' command once players have reconnected.")
  },
  {"load",      ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("load\n"
      "load <file-name>"),
   N_("Load game from file."),
   N_("Load a game from <file-name>. Any current data including players, "
      "rulesets and server options are lost.\n")
  },
  {"read",	ALLOW_CTRL,
   /* TRANS: translate text between <> only */
   N_("read <file-name>"),
   N_("Process server commands from file."), NULL
  },
  {"write",	ALLOW_HACK,
   /* TRANS: translate text between <> only */
   N_("write <file-name>"),
   N_("Write current settings as server commands to file."), NULL
  },
  {"rfcstyle",	ALLOW_HACK,
   /* no translatable parameters */
   "rfcstyle",
   N_("Switch server output between 'RFC-style' and normal style."), NULL
  },
  {"serverid",	ALLOW_INFO,
   /* no translatable parameters */
   "serverid",
   N_("Simply returns the id of the server."),
  }
};


/**************************************************************************
  ...
**************************************************************************/
const struct command *command_by_number(int i)
{
  assert(i >= 0 && i < CMD_NUM);
  return &commands[i];
}

/**************************************************************************
  ...
**************************************************************************/
const char *command_name(const struct command *pcommand)
{
  return pcommand->name;
}

/**************************************************************************
  ...
**************************************************************************/
const char *command_name_by_number(int i)
{
  return command_by_number(i)->name;
}

/**************************************************************************
  ...
**************************************************************************/
const char *command_synopsis(const struct command *pcommand)
{
  return pcommand->synopsis;
}

/**************************************************************************
  ...
**************************************************************************/
const char *command_short_help(const struct command *pcommand)
{
  return pcommand->short_help;
}

/**************************************************************************
  ...
**************************************************************************/
const char *command_extra_help(const struct command *pcommand)
{
  return pcommand->extra_help;
}

/**************************************************************************
  ...
**************************************************************************/
enum cmdlevel_id command_level(const struct command *pcommand)
{
  return pcommand->level;
}
