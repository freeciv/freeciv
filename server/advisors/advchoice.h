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
#ifndef FC__ADVCHOICE_H
#define FC__ADVCHOICE_H

/* Uncomment to have choice infomation tracked */
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
  universals_u value; /* what the advisor wants */
  adv_want want;      /* how much it wants it */
  bool need_boat;     /* unit being built wants a boat */
#ifdef ADV_CHOICE_TRACK
  char *use;
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

#ifdef ADV_CHOICE_TRACK
void adv_choice_copy(struct adv_choice *dest, struct adv_choice *src);
void adv_choice_set_use(struct adv_choice *choice, const char *use);
void adv_choice_log_info(struct adv_choice *choice, const char *loc1, const char *loc2);
#else  /* ADV_CHOICE_TRACK */
static inline void adv_choice_copy(struct adv_choice *dest, struct adv_choice *src)
{
  if (dest != src) {
    *dest = *src;
  }
}
#define adv_choice_set_use(_choice, _use)
#define adv_choice_log_info(_choice, _loc1, _loc2)
#endif /* ADV_CHOICE_TRACK */

#endif   /* FC__ADVCHOICE_H */
