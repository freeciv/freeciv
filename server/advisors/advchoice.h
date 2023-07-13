/***********************************************************************
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
#ifndef FC__ADVCHOICE_H
#define FC__ADVCHOICE_H

/*
 * Uncomment ADV_CHOICE_TRACK to have choice information tracking enabled.
 *
 * To get the tracking information logged from specific place,
 * then add there one of
 *  - ADV_CHOICE_LOG()
 *  - adv_choice_log_info()
 *  - adv_choice_log_int()
 *  - adv_choice_get_use().
 *
 * To get logging when specific choice ends as the winning one,
 * mark it with adv_choice_mark()
 */

/* #define ADV_CHOICE_TRACK */

#ifdef ADV_CHOICE_TRACK
#define ADV_CHOICE_LOG_LEVEL LOG_NORMAL
#endif

enum choice_type {
  CT_NONE = 0,
  CT_BUILDING = 1,
  CT_CIVILIAN,
  CT_ATTACKER,
  CT_DEFENDER,
  CT_LAST
};

struct adv_choice {
  enum choice_type type;
  universals_u value;   /* What the advisor wants */
  adv_want want;        /* How much it wants it */
  bool need_boat;       /* Unit being built wants a boat */
#ifdef ADV_CHOICE_TRACK
  char *use;
  bool log_if_chosen;   /* If this choice ends as the winning one, log it */
#endif /* ADV_CHOICE_TRACK */
};

void adv_init_choice(struct adv_choice *choice);
void adv_deinit_choice(struct adv_choice *choice);

struct adv_choice *adv_new_choice(void);
void adv_free_choice(struct adv_choice *choice);

struct adv_choice *adv_better_choice(struct adv_choice *first,
                                     struct adv_choice *second);
struct adv_choice *adv_better_choice_free(struct adv_choice *first,
                                          struct adv_choice *second);

bool is_unit_choice_type(enum choice_type type);

const char *adv_choice_rule_name(const struct adv_choice *choice);

#ifdef ADV_CHOICE_TRACK
void adv_choice_copy(struct adv_choice *dest, struct adv_choice *src);
void adv_choice_set_use(struct adv_choice *choice, const char *use);
#define adv_choice_mark(_choice) (_choice)->log_if_chosen = TRUE
void adv_choice_log_info(struct adv_choice *choice, const char *loc1, const char *loc2);
void adv_choice_log_int(struct adv_choice *choice, const char *loc1, int loc2);
const char *adv_choice_get_use(const struct adv_choice *choice);
#else  /* ADV_CHOICE_TRACK */
static inline void adv_choice_copy(struct adv_choice *dest, struct adv_choice *src)
{
  if (dest != src) {
    *dest = *src;
  }
}
#define adv_choice_set_use(_choice, _use)
#define adv_choice_mark(_choice)
#define adv_choice_log_info(_choice, _loc1, _loc2)
#define adv_choice_log_int(_choice, _loc1, _loc2)
static inline const char *adv_choice_get_use(const struct adv_choice *choice)
{
  return "(unknown)";
}
#endif /* ADV_CHOICE_TRACK */

#define ADV_CHOICE_LOG(_choice) adv_choice_log_int((_choice), __FILE__, __FC_LINE__)

#ifdef FREECIV_NDEBUG
#define ADV_CHOICE_ASSERT(c) /* Do nothing. */
#else  /* FREECIV_NDEBUG */
#define ADV_CHOICE_ASSERT(c)                                             \
  do {                                                                   \
    if ((c).want > 0) {                                                  \
      fc_assert((c).type > CT_NONE && (c).type < CT_LAST);               \
      if (!is_unit_choice_type((c).type)) {                              \
        int _iindex = improvement_index((c).value.building);             \
        fc_assert(_iindex >= 0 && _iindex < improvement_count());        \
      } else {                                                           \
        int _uindex = utype_index((c).value.utype);                      \
        fc_assert(_uindex >= 0 && _uindex < utype_count());              \
      }                                                                  \
    }                                                                    \
  } while (FALSE);
#endif /* FREECIV_NDEBUG */

#endif /* FC__ADVCHOICE_H */
