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

#ifndef FC__SETTINGS_H
#define FC__SETTINGS_H

#include "shared.h"

#include "game.h"

struct sset_val_name {
  const char *support;          /* Untranslated long support name, used 
                                 * for saving. */
  const char *pretty;           /* Translated, used to display to the
                                 * users. */
};

/* Whether settings are sent to the client when the client lists
 * server options; also determines whether clients can set them in principle.
 * Eg, not sent: seeds, saveturns, etc.
 */
#define SSET_TO_CLIENT TRUE
#define SSET_SERVER_ONLY FALSE

/* Categories allow options to be usefully organized when presented to the
 * user */
#define SPECENUM_NAME sset_category
#define SPECENUM_VALUE0     SSET_GEOLOGY
#define SPECENUM_VALUE0NAME N_("Geological")
#define SPECENUM_VALUE1     SSET_SOCIOLOGY
#define SPECENUM_VALUE1NAME N_("Sociological")
#define SPECENUM_VALUE2     SSET_ECONOMICS
#define SPECENUM_VALUE2NAME N_("Economic")
#define SPECENUM_VALUE3     SSET_MILITARY
#define SPECENUM_VALUE3NAME N_("Military")
#define SPECENUM_VALUE4     SSET_SCIENCE
#define SPECENUM_VALUE4NAME N_("Scientific")
#define SPECENUM_VALUE5     SSET_INTERNAL
#define SPECENUM_VALUE5NAME N_("Internal")
#define SPECENUM_VALUE6     SSET_NETWORK
#define SPECENUM_VALUE6NAME N_("Networking")
/* keep this last */
#define SPECENUM_COUNT      SSET_NUM_CATEGORIES
#include "specenum_gen.h"

/* Levels allow options to be subdivided and thus easier to navigate */
#define SPECENUM_NAME sset_level
#define SPECENUM_VALUE0     SSET_NONE
#define SPECENUM_VALUE0NAME N_("None")
#define SPECENUM_VALUE1     SSET_ALL
#define SPECENUM_VALUE1NAME N_("All")
#define SPECENUM_VALUE2     SSET_VITAL
#define SPECENUM_VALUE2NAME N_("Vital")
#define SPECENUM_VALUE3     SSET_SITUATIONAL
#define SPECENUM_VALUE3NAME N_("Situational")
#define SPECENUM_VALUE4     SSET_RARE
#define SPECENUM_VALUE4NAME N_("Rare")
#define SPECENUM_VALUE5     SSET_CHANGED
#define SPECENUM_VALUE5NAME N_("Changed")
#define SPECENUM_VALUE6     SSET_LOCKED
#define SPECENUM_VALUE6NAME N_("Locked")
/* keep this last */
#define SPECENUM_COUNT      OLEVELS_NUM
#include "specenum_gen.h"

/* Server setting types. */
#define SPECENUM_NAME sset_type
#define SPECENUM_VALUE0 SSET_BOOL
#define SPECENUM_VALUE1 SSET_INT
#define SPECENUM_VALUE2 SSET_STRING
#define SPECENUM_VALUE3 SSET_ENUM
#define SPECENUM_VALUE4 SSET_BITWISE
#include "specenum_gen.h"

/* forward declaration */
struct setting;

struct setting *setting_by_number(int id);
struct setting *setting_by_name(const char *name);
int setting_number(const struct setting *pset);

const char *setting_name(const struct setting *pset);
const char *setting_short_help(const struct setting *pset);
const char *setting_extra_help(const struct setting *pset);
enum sset_type setting_type(const struct setting *pset);
enum sset_level setting_level(const struct setting *pset);

bool setting_is_changeable(const struct setting *pset,
                           struct connection *caller, char *reject_msg,
                           size_t reject_msg_len);
bool setting_is_visible(const struct setting *pset,
                        struct connection *caller);

const char *setting_value_name(const struct setting *pset, bool pretty,
                               char *buf, size_t buf_len);
const char *setting_default_name(const struct setting *pset, bool pretty,
                                 char *buf, size_t buf_len);

/* Type SSET_BOOL setting functions. */
bool setting_bool_set(struct setting *pset, const char *val,
                      struct connection *caller, char *reject_msg,
                      size_t reject_msg_len);
bool setting_bool_validate(const struct setting *pset, const char *val,
                           struct connection *caller, char *reject_msg,
                           size_t reject_msg_len);

/* Type SSET_INT setting functions. */
int setting_int_min(const struct setting *pset);
int setting_int_max(const struct setting *pset);
bool setting_int_set(struct setting *pset, int val,
                     struct connection *caller, char *reject_msg,
                     size_t reject_msg_len);
bool setting_int_validate(const struct setting *pset, int val,
                          struct connection *caller, char *reject_msg,
                          size_t reject_msg_len);

/* Type SSET_STRING setting functions. */
bool setting_str_set(struct setting *pset, const char *val,
                     struct connection *caller, char *reject_msg,
                     size_t reject_msg_len);
bool setting_str_validate(const struct setting *pset, const char *val,
                          struct connection *caller, char *reject_msg,
                          size_t reject_msg_len);

/* Type SSET_ENUM setting functions. */
const char *setting_enum_val(const struct setting *pset, int val,
                             bool pretty);
bool setting_enum_set(struct setting *pset, const char *val,
                      struct connection *caller, char *reject_msg,
                      size_t reject_msg_len);
bool setting_enum_validate(const struct setting *pset, const char *val,
                           struct connection *caller, char *reject_msg,
                           size_t reject_msg_len);

/* Type SSET_BITWISE setting functions. */
const char *setting_bitwise_bit(const struct setting *pset,
                                int bit, bool pretty);
bool setting_bitwise_set(struct setting *pset, const char *val,
                         struct connection *caller, char *reject_msg,
                         size_t reject_msg_len);
bool setting_bitwise_validate(const struct setting *pset, const char *val,
                              struct connection *caller, char *reject_msg,
                              size_t reject_msg_len);

void setting_action(const struct setting *pset);

bool setting_changed(const struct setting *pset);
bool setting_locked(const struct setting *pset);
void setting_lock_set(struct setting *pset, bool lock);

/* iterate over all settings */
#define settings_iterate(_pset)                                             \
{                                                                           \
  struct setting *_pset;                                                    \
  int _pset_id;                                                             \
  for (_pset_id = 0; (_pset = setting_by_number(_pset_id)); _pset_id++) {

#define settings_iterate_end                                               \
  }                                                                        \
}

void settings_game_start(void);
void settings_game_save(struct section_file *file, const char *section);
void settings_game_load(struct section_file *file, const char *section);
bool settings_game_reset(void);

void settings_init(void);
void settings_reset(void);
void settings_turn(void);
void settings_free(void);
int settings_number(void);

bool settings_ruleset(struct section_file *file, const char *section);

void send_server_setting(struct conn_list *dest, const struct setting *pset);
void send_server_settings(struct conn_list *dest);
void send_server_hack_level_settings(struct conn_list *dest);
void send_server_setting_control(struct connection *pconn);

#endif				/* FC__SETTINGS_H */
