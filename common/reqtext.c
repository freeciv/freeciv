/****************************************************************************
 Freeciv - Copyright (C) 2004 - The Freeciv Team
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* utility */
#include "astring.h"
#include "fcintl.h"

/* common */
#include "achievements.h"
#include "actions.h"
#include "calendar.h"
#include "counters.h"
#include "extras.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "nation.h"
#include "player.h"
#include "requirements.h"
#include "server_settings.h"
#include "specialist.h"
#include "terrain.h"
#include "tiledef.h"

#include "reqtext.h"

/*************************************************************//**
  Append text for the requirement. Something like

    "Requires knowledge of the technology Communism."

  pplayer may be nullptr. Note that it must be updated every time
  a new requirement type or range is defined.
*****************************************************************/
bool req_text_insert(char *buf, size_t bufsz, struct player *pplayer,
                     const struct requirement *preq,
                     enum rt_verbosity verb, const char *prefix)
{
  if (preq->quiet && verb != VERB_ACTUAL) {
    return FALSE;
  }

  switch (preq->source.kind) {
  case VUT_NONE:
    return FALSE;

  case VUT_COUNTER:
    if (preq->present) {

      fc_strlcat(buf, prefix, bufsz);
      cat_snprintf(buf, bufsz,
                 _("Requires counter %s to achieve at minimum %d value."),
                 counter_rule_name(preq->source.value.counter),
                 preq->source.value.counter->checkpoint);
    } else {

      fc_strlcat(buf, prefix, bufsz);
      cat_snprintf(buf, bufsz,
                   _("Requires counter %s to be less than %d value."),
                   counter_rule_name(preq->source.value.counter),
                   preq->source.value.counter->checkpoint - 1);
    }
    break;
  case VUT_ADVANCE:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires knowledge of the technology %s."),
                     advance_name_translation(preq->source.value.advance));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented by knowledge of the technology %s."),
                     advance_name_translation(preq->source.value.advance));
      }
      return TRUE;
    case REQ_RANGE_TEAM:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that a player on your team knows the "
                       "technology %s."),
                     advance_name_translation(preq->source.value.advance));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented if any player on your team knows the "
                       "technology %s."),
                     advance_name_translation(preq->source.value.advance));
      }
      return TRUE;
    case REQ_RANGE_ALLIANCE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that a player allied to you knows the "
                       "technology %s."),
                     advance_name_translation(preq->source.value.advance));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented if any player allied to you knows the "
                       "technology %s."),
                     advance_name_translation(preq->source.value.advance));
      }
      return TRUE;
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->survives) {
        if (preq->present) {
          cat_snprintf(buf, bufsz,
                       _("Requires that someone has discovered the "
                         "technology %s."),
                       advance_name_translation(preq->source.value.advance));
        } else {
          cat_snprintf(buf, bufsz,
                       _("Requires that no-one has yet discovered the "
                        "technology %s."),
                       advance_name_translation(preq->source.value.advance));
        }
      } else {
        if (preq->present) {
          cat_snprintf(buf, bufsz,
                       _("Requires that some player knows the "
                         "technology %s."),
                       advance_name_translation(preq->source.value.advance));
        } else {
          cat_snprintf(buf, bufsz,
                       _("Requires that no player knows the "
                         "technology %s."),
                       advance_name_translation(preq->source.value.advance));
        }
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_TECHFLAG:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) tech flag. */
                     _("Requires knowledge of a technology with the "
                       "\"%s\" flag."),
                     tech_flag_id_translated_name(preq->source.value.techflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) tech flag. */
                     _("Prevented by knowledge of any technology with the "
                       "\"%s\" flag."),
                     tech_flag_id_translated_name(preq->source.value.techflag));
      }
      return TRUE;
    case REQ_RANGE_TEAM:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) tech flag. */
                     _("Requires that a player on your team knows "
                       "a technology with the \"%s\" flag."),
                     tech_flag_id_translated_name(preq->source.value.techflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) tech flag. */
                     _("Prevented if any player on your team knows "
                       "any technology with the \"%s\" flag."),
                     tech_flag_id_translated_name(preq->source.value.techflag));
      }
      return TRUE;
    case REQ_RANGE_ALLIANCE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) tech flag. */
                     _("Requires that a player allied to you knows "
                       "a technology with the \"%s\" flag."),
                     tech_flag_id_translated_name(preq->source.value.techflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) tech flag. */
                     _("Prevented if any player allied to you knows "
                       "any technology with the \"%s\" flag."),
                     tech_flag_id_translated_name(preq->source.value.techflag));
      }
      return TRUE;
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) tech flag. */
                     _("Requires that some player knows a technology "
                       "with the \"%s\" flag."),
                     tech_flag_id_translated_name(preq->source.value.techflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) tech flag. */
                     _("Requires that no player knows any technology with "
                       "the \"%s\" flag."),
                     tech_flag_id_translated_name(preq->source.value.techflag));
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_GOVERNMENT:
    if (preq->range != REQ_RANGE_PLAYER) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz, _("Requires the %s government."),
                   government_name_translation(preq->source.value.govern));
    } else {
      cat_snprintf(buf, bufsz, _("Not available under the %s government."),
                   government_name_translation(preq->source.value.govern));
    }
    return TRUE;

  case VUT_GOVFLAG:
    if (preq->range != REQ_RANGE_PLAYER) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz, _("Requires a %s government."),
                   gov_flag_id_translated_name(preq->source.value.govflag));
    } else {
      cat_snprintf(buf, bufsz, _("Not available under a %s government."),
                   gov_flag_id_translated_name(preq->source.value.govflag));
    }
    return TRUE;

  case VUT_ACHIEVEMENT:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, _("Requires you to have achieved \"%s\"."),
                     achievement_name_translation(preq->source.value.achievement));
      } else {
        cat_snprintf(buf, bufsz, _("Not available once you have achieved "
                                   "\"%s\"."),
                     achievement_name_translation(preq->source.value.achievement));
      }
      return TRUE;
    case REQ_RANGE_TEAM:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, _("Requires that at least one of your "
                                   "team-mates has achieved \"%s\"."),
                     achievement_name_translation(preq->source.value.achievement));
      } else {
        cat_snprintf(buf, bufsz, _("Not available if any of your team-mates "
                                   "has achieved \"%s\"."),
                     achievement_name_translation(preq->source.value.achievement));
      }
      return TRUE;
    case REQ_RANGE_ALLIANCE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, _("Requires that at least one of your allies "
                                   "has achieved \"%s\"."),
                     achievement_name_translation(preq->source.value.achievement));
      } else {
        cat_snprintf(buf, bufsz, _("Not available if any of your allies has "
                                   "achieved \"%s\"."),
                     achievement_name_translation(preq->source.value.achievement));
      }
      return TRUE;
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, _("Requires that at least one player "
                                   "has achieved \"%s\"."),
                     achievement_name_translation(preq->source.value.achievement));
      } else {
        cat_snprintf(buf, bufsz, _("Not available if any player has "
                                   "achieved \"%s\"."),
                     achievement_name_translation(preq->source.value.achievement));
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_ACTION:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, _("Applies to the \"%s\" action."),
                     action_name_translation(preq->source.value.action));
      } else {
        cat_snprintf(buf, bufsz, _("Doesn't apply to the \"%s\""
                                   " action."),
                     action_name_translation(preq->source.value.action));
      }
      return TRUE;
    default:
      /* Not supported. */
      break;
    }
    break;

  case VUT_IMPR_GENUS:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, _("Applies to \"%s\" buildings."),
                     impr_genus_id_translated_name(
                       preq->source.value.impr_genus));
      } else {
        cat_snprintf(buf, bufsz, _("Doesn't apply to \"%s\" buildings."),
                     impr_genus_id_translated_name(
                       preq->source.value.impr_genus));
      }
      return TRUE;
    default:
      /* Not supported. */
      break;
    }
    break;

  case VUT_IMPR_FLAG:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, _("Applies to \"%s\" buildings."),
                     impr_flag_id_translated_name(
                       preq->source.value.impr_flag));
      } else {
        cat_snprintf(buf, bufsz, _("Doesn't apply to \"%s\" buildings."),
                     impr_flag_id_translated_name(
                       preq->source.value.impr_flag));
      }
      return TRUE;
    default:
      /* Not supported. */
      break;
    }
    break;

  case VUT_PLAYER_FLAG:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, _("Applies to players with \"%s\" flag."),
                     plr_flag_id_translated_name(
                       preq->source.value.plr_flag));
      } else {
        cat_snprintf(buf, bufsz, _("Doesn't apply to players with \"%s\" flag."),
                     plr_flag_id_translated_name(
                       preq->source.value.plr_flag));
      }
      return TRUE;
    default:
      /* Not supported. */
      break;
    }
    break;

  case VUT_PLAYER_STATE:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, _("Applies to \"%s\" players."),
                     plrstate_type_translated_name(preq->source.value.plrstate));
      } else {
        cat_snprintf(buf, bufsz, _("Doesn't apply to \"%s\" players."),
                     plrstate_type_translated_name(preq->source.value.plrstate));
      }
      return TRUE;
    default:
      /* Not supported. */
      break;
    }
    break;

  case VUT_IMPROVEMENT:
  case VUT_SITE:
    switch (preq->range) {
    case REQ_RANGE_WORLD:
      if (is_great_wonder(preq->source.value.building)) {
        fc_strlcat(buf, prefix, bufsz);
        if (preq->survives) {
          if (preq->present) {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires that %s was built at some point, "
                             "and that it has not yet been rendered "
                             "obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires that %s was built at some point."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          } else {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if %s has ever been built, "
                             "unless it would be obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if %s has ever been built."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          }
        } else {
          /* Non-surviving requirement */
          if (preq->present) {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires %s to be owned by any player "
                             "and not yet obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires %s to be owned by any player."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          } else {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if %s is currently owned by "
                             "any player, unless it is obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if %s is currently owned by "
                             "any player."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          }
        }
        return TRUE;
      }
      /* Non-great-wonder world-ranged requirements not supported */
      break;
    case REQ_RANGE_ALLIANCE:
      if (is_wonder(preq->source.value.building)) {
        fc_strlcat(buf, prefix, bufsz);
        if (preq->survives) {
          if (preq->present) {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires someone who is currently allied to "
                             "you to have built %s at some point, and for "
                             "it not to have been rendered obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires someone who is currently allied to "
                             "you to have built %s at some point."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          } else {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if someone currently allied to you "
                             "has ever built %s, unless it would be "
                             "obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if someone currently allied to you "
                             "has ever built %s."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          }
        } else {
          /* Non-surviving requirement */
          if (preq->present) {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires someone allied to you to own %s, "
                             "and for it not to have been rendered "
                             "obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires someone allied to you to own %s."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          } else {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if someone allied to you owns %s, "
                             "unless it is obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if someone allied to you owns %s."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          }
        }
        return TRUE;
      }
      /* Non-wonder alliance-ranged requirements not supported */
      break;
    case REQ_RANGE_TEAM:
      if (is_wonder(preq->source.value.building)) {
        fc_strlcat(buf, prefix, bufsz);
        if (preq->survives) {
          if (preq->present) {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires someone on your team to have "
                             "built %s at some point, and for it not "
                             "to have been rendered obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires someone on your team to have "
                             "built %s at some point."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          } else {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if someone on your team has ever "
                             "built %s, unless it would be obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if someone on your team has ever "
                             "built %s."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          }
        } else {
          /* Non-surviving requirement */
          if (preq->present) {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires someone on your team to own %s, "
                             "and for it not to have been rendered "
                             "obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires someone on your team to own %s."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          } else {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if someone on your team owns %s, "
                             "unless it is obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if someone on your team owns %s."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          }
        }
        return TRUE;
      }
      /* Non-wonder team-ranged requirements not supported */
      break;
    case REQ_RANGE_PLAYER:
      if (is_wonder(preq->source.value.building)) {
        fc_strlcat(buf, prefix, bufsz);
        if (preq->survives) {
          if (preq->present) {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires you to have built %s at some point, "
                             "and for it not to have been rendered "
                             "obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires you to have built %s at some point."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          } else {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if you have ever built %s, "
                             "unless it would be obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if you have ever built %s."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          }
        } else {
          /* Non-surviving requirement */
          if (preq->present) {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires you to own %s, which must not "
                             "be obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Requires you to own %s."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          } else {
            if (preq->source.kind == VUT_IMPROVEMENT
                && can_improvement_go_obsolete(preq->source.value.building)) {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if you own %s, unless it is "
                             "obsolete."),
                           improvement_name_translation
                           (preq->source.value.building));
            } else {
              cat_snprintf(buf, bufsz,
                           /* TRANS: %s is a wonder */
                           _("Prevented if you own %s."),
                           improvement_name_translation
                           (preq->source.value.building));
            }
          }
        }
        return TRUE;
      }
      /* Non-wonder player-ranged requirements not supported */
      break;
    case REQ_RANGE_CONTINENT:
      if (is_wonder(preq->source.value.building)) {
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          if (preq->source.kind == VUT_IMPROVEMENT
              && can_improvement_go_obsolete(preq->source.value.building)) {
            cat_snprintf(buf, bufsz,
                         /* TRANS: %s is a wonder */
                         _("Requires %s in one of your cities on the same "
                           "continent, and not yet obsolete."),
                         improvement_name_translation
                         (preq->source.value.building));
          } else {
            cat_snprintf(buf, bufsz,
                         /* TRANS: %s is a wonder */
                         _("Requires %s in one of your cities on the same "
                           "continent."),
                         improvement_name_translation
                         (preq->source.value.building));
          }
        } else {
          if (preq->source.kind == VUT_IMPROVEMENT
              && can_improvement_go_obsolete(preq->source.value.building)) {
            cat_snprintf(buf, bufsz,
                         /* TRANS: %s is a wonder */
                         _("Prevented if %s is in one of your cities on the "
                           "same continent, unless it is obsolete."),
                         improvement_name_translation
                         (preq->source.value.building));
          } else {
            cat_snprintf(buf, bufsz,
                         /* TRANS: %s is a wonder */
                         _("Prevented if %s is in one of your cities on the "
                           "same continent."),
                         improvement_name_translation
                         (preq->source.value.building));
          }
        }
        return TRUE;
      }
      /* Surviving or non-wonder continent-ranged requirements not supported */
      break;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        if (preq->source.kind == VUT_IMPROVEMENT
            && can_improvement_go_obsolete(preq->source.value.building)) {
          /* Should only apply to wonders */
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is a building or wonder */
                       _("Requires %s in the city or a trade partner "
                         "(and not yet obsolete)."),
                       improvement_name_translation
                       (preq->source.value.building));
        } else {
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is a building or wonder */
                       _("Requires %s in the city or a trade partner."),
                       improvement_name_translation
                       (preq->source.value.building));
        }
      } else {
        if (preq->source.kind == VUT_IMPROVEMENT
            && can_improvement_go_obsolete(preq->source.value.building)) {
          /* Should only apply to wonders */
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is a building or wonder */
                       _("Prevented by %s in the city or a trade partner "
                         "(unless it is obsolete)."),
                       improvement_name_translation
                       (preq->source.value.building));
        } else {
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is a building or wonder */
                       _("Prevented by %s in the city or a trade partner."),
                       improvement_name_translation
                       (preq->source.value.building));
        }
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        if (preq->source.kind == VUT_IMPROVEMENT
            && can_improvement_go_obsolete(preq->source.value.building)) {
          /* Should only apply to wonders */
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is a building or wonder */
                       _("Requires %s in the city (and not yet obsolete)."),
                       improvement_name_translation
                       (preq->source.value.building));
        } else {
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is a building or wonder */
                       _("Requires %s in the city."),
                       improvement_name_translation
                       (preq->source.value.building));
        }
      } else {
        if (preq->source.kind == VUT_IMPROVEMENT
            && can_improvement_go_obsolete(preq->source.value.building)) {
          /* Should only apply to wonders */
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is a building or wonder */
                       _("Prevented by %s in the city (unless it is "
                         "obsolete)."),
                       improvement_name_translation
                       (preq->source.value.building));
        } else {
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is a building or wonder */
                       _("Prevented by %s in the city."),
                       improvement_name_translation
                       (preq->source.value.building));
        }
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Only applies to \"%s\" buildings."),
                     improvement_name_translation
                     (preq->source.value.building));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Does not apply to \"%s\" buildings."),
                     improvement_name_translation
                     (preq->source.value.building));
      }
      return TRUE;
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires \"%s\" building in a city on the tile."),
                     improvement_name_translation
                     (preq->source.value.building));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented by \"%s\" building in a city on the tile."),
                     improvement_name_translation
                     (preq->source.value.building));
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_EXTRA:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Only applies to \"%s\" extras."),
                     extra_name_translation(preq->source.value.extra));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Does not apply to \"%s\" extras."),
                     extra_name_translation(preq->source.value.extra));
      }
      return TRUE;
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Requires %s on the tile."),
                     extra_name_translation(preq->source.value.extra));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Prevented by %s on the tile."),
                     extra_name_translation(preq->source.value.extra));
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Requires %s on the tile or a cardinally "
                        "adjacent tile."),
                     extra_name_translation(preq->source.value.extra));
        } else {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Prevented by %s on the tile or any cardinally "
                        "adjacent tile."),
                     extra_name_translation(preq->source.value.extra));
        }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Requires %s on the tile or an adjacent "
                        "tile."),
                     extra_name_translation(preq->source.value.extra));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Prevented by %s on the tile or any adjacent "
                        "tile."),
                     extra_name_translation(preq->source.value.extra));
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Requires %s on a tile within the city "
                        "radius."),
                     extra_name_translation(preq->source.value.extra));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Prevented by %s on any tile within the city "
                        "radius."),
                     extra_name_translation(preq->source.value.extra));
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Requires %s on a tile within the city "
                        "radius, or the city radius of a trade partner."),
                     extra_name_translation(preq->source.value.extra));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?extra:Prevented by %s on any tile within the city "
                        "radius or the city radius of a trade partner."),
                     extra_name_translation(preq->source.value.extra));
      }
      return TRUE;
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_TILEDEF:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Only applies to \"%s\" tiledef."),
                     tiledef_name_translation(preq->source.value.tiledef));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Does not apply to \"%s\" tiledef."),
                     tiledef_name_translation(preq->source.value.tiledef));
      }
      return TRUE;
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Requires %s on the tile."),
                     tiledef_name_translation(preq->source.value.tiledef));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Prevented by %s on the tile."),
                     tiledef_name_translation(preq->source.value.tiledef));
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Requires %s on the tile or a cardinally "
                        "adjacent tile."),
                     tiledef_name_translation(preq->source.value.tiledef));
        } else {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Prevented by %s on the tile or any cardinally "
                        "adjacent tile."),
                     tiledef_name_translation(preq->source.value.tiledef));
        }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Requires %s on the tile or an adjacent "
                        "tile."),
                     tiledef_name_translation(preq->source.value.tiledef));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Prevented by %s on the tile or any adjacent "
                        "tile."),
                     tiledef_name_translation(preq->source.value.tiledef));
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Requires %s on a tile within the city "
                        "radius."),
                     tiledef_name_translation(preq->source.value.tiledef));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Prevented by %s on any tile within the city "
                        "radius."),
                     tiledef_name_translation(preq->source.value.tiledef));
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Requires %s on a tile within the city "
                        "radius, or the city radius of a trade partner."),
                     tiledef_name_translation(preq->source.value.tiledef));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?tiledef:Prevented by %s on any tile within the city "
                        "radius or the city radius of a trade partner."),
                     tiledef_name_translation(preq->source.value.tiledef));
      }
      return TRUE;
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_GOOD:
    switch (preq->range) {
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, Q_("?good:Requires import of %s ."),
                     goods_name_translation(preq->source.value.good));
      } else {
        cat_snprintf(buf, bufsz, Q_("?goods:Prevented by import of %s."),
                     goods_name_translation(preq->source.value.good));
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_TERRAIN:
    switch (preq->range) {
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz, Q_("?terrain:Requires %s on the tile."),
                     terrain_name_translation(preq->source.value.terrain));
      } else {
        cat_snprintf(buf, bufsz, Q_("?terrain:Prevented by %s on the tile."),
                     terrain_name_translation(preq->source.value.terrain));
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?terrain:Requires %s on the tile or a cardinally "
                        "adjacent tile."),
                     terrain_name_translation(preq->source.value.terrain));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?terrain:Prevented by %s on the tile or any "
                        "cardinally adjacent tile."),
                     terrain_name_translation(preq->source.value.terrain));
      }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?terrain:Requires %s on the tile or an adjacent "
                        "tile."),
                     terrain_name_translation(preq->source.value.terrain));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?terrain:Prevented by %s on the tile or any "
                        "adjacent tile."),
                     terrain_name_translation(preq->source.value.terrain));
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?terrain:Requires %s on a tile within the city "
                        "radius."),
                     terrain_name_translation(preq->source.value.terrain));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?terrain:Prevented by %s on any tile within the city "
                        "radius."),
                     terrain_name_translation(preq->source.value.terrain));
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     Q_("?terrain:Requires %s on a tile within the city "
                        "radius, or the city radius of a trade partner."),
                     terrain_name_translation(preq->source.value.terrain));
      } else {
        cat_snprintf(buf, bufsz,
                     Q_("?terrain:Prevented by %s on any tile within the city "
                        "radius or the city radius of a trade partner."),
                     terrain_name_translation(preq->source.value.terrain));
      }
      return TRUE;
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_NATION:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "... playing as the Swedes." */
                     _("Requires that you are playing as the %s."),
                     nation_plural_translation(preq->source.value.nation));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "... playing as the Turks." */
                     _("Requires that you are not playing as the %s."),
                     nation_plural_translation(preq->source.value.nation));
      }
      return TRUE;
    case REQ_RANGE_TEAM:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "... same team as the Indonesians." */
                     _("Requires that you are on the same team as "
                       "the %s."),
                     nation_plural_translation(preq->source.value.nation));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "... same team as the Greeks." */
                     _("Requires that you are not on the same team as "
                       "the %s."),
                     nation_plural_translation(preq->source.value.nation));
      }
      return TRUE;
    case REQ_RANGE_ALLIANCE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "... allied with the Koreans." */
                     _("Requires that you are allied with the %s."),
                     nation_plural_translation(preq->source.value.nation));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "... allied with the Danes." */
                     _("Requires that you are not allied with the %s."),
                     nation_plural_translation(preq->source.value.nation));
      }
      return TRUE;
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->survives) {
        if (preq->present) {
          cat_snprintf(buf, bufsz,
                       /* TRANS: "Requires the Apaches to have ..." */
                       _("Requires the %s to have been in the game."),
                       nation_plural_translation(preq->source.value.nation));
        } else {
          cat_snprintf(buf, bufsz,
                       /* TRANS: "Requires the Celts never to have ..." */
                       _("Requires the %s never to have been in the "
                         "game."),
                       nation_plural_translation(preq->source.value.nation));
        }
      } else {
        if (preq->present) {
          cat_snprintf(buf, bufsz,
                       /* TRANS: "Requires the Belgians in the game." */
                       _("Requires the %s in the game."),
                       nation_plural_translation(preq->source.value.nation));
        } else {
          cat_snprintf(buf, bufsz,
                       /* TRANS: "Requires that the Russians are not ... */
                       _("Requires that the %s are not in the game."),
                       nation_plural_translation(preq->source.value.nation));
        }
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_NATIONGROUP:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: nation group: "... playing African nation." */
                     _("Requires that you are playing %s nation."),
                     nation_group_name_translation(preq->source.value.nationgroup));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: nation group: "... playing Imaginary nation." */
                     _("Prevented if you are playing %s nation."),
                     nation_group_name_translation(preq->source.value.nationgroup));
      }
      return TRUE;
    case REQ_RANGE_TEAM:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: nation group: "Requires Medieval nation ..." */
                     _("Requires %s nation on your team."),
                     nation_group_name_translation(preq->source.value.nationgroup));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: nation group: "Prevented by Medieval nation ..." */
                     _("Prevented by %s nation on your team."),
                     nation_group_name_translation(preq->source.value.nationgroup));
      }
      return TRUE;
    case REQ_RANGE_ALLIANCE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: nation group: "Requires Modern nation ..." */
                     _("Requires %s nation in alliance with you."),
                     nation_group_name_translation(preq->source.value.nationgroup));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: nation group: "Prevented by Modern nation ..." */
                     _("Prevented if %s nation is in alliance with you."),
                     nation_group_name_translation(preq->source.value.nationgroup));
      }
      return TRUE;
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: nation group: "Requires Asian nation ..." */
                     _("Requires %s nation in the game."),
                     nation_group_name_translation(preq->source.value.nationgroup));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: nation group: "Prevented by Asian nation ..." */
                     _("Prevented by %s nation in the game."),
                     nation_group_name_translation(preq->source.value.nationgroup));
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_STYLE:
    if (preq->range != REQ_RANGE_PLAYER) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz,
                   /* TRANS: "Requires that you are playing Asian style
                    * nation." */
                   _("Requires that you are playing %s style nation."),
                   style_name_translation(preq->source.value.style));
    } else {
      cat_snprintf(buf, bufsz,
                   /* TRANS: "Requires that you are not playing Classical
                    * style nation." */
                   _("Requires that you are not playing %s style nation."),
                   style_name_translation(preq->source.value.style));
    }
    return TRUE;

  case VUT_NATIONALITY:
    switch (preq->range) {
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "Requires at least one Barbarian citizen ..." */
                     _("Requires at least one %s citizen in the city or a "
                       "trade partner."),
                     nation_adjective_translation(preq->source.value.nationality));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "... no Pirate citizens ..." */
                     _("Requires that there are no %s citizens in "
                       "the city or any trade partners."),
                     nation_adjective_translation(preq->source.value.nationality));
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "Requires at least one Barbarian citizen ..." */
                     _("Requires at least one %s citizen in the city."),
                     nation_adjective_translation(preq->source.value.nationality));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: "... no Pirate citizens ..." */
                     _("Requires that there are no %s citizens in "
                       "the city."),
                     nation_adjective_translation(preq->source.value.nationality));
      }
      return TRUE;
    case REQ_RANGE_WORLD:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_ORIGINAL_OWNER:
    switch (preq->range) {
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires city to be founded by %s."),
                     nation_adjective_translation(preq->source.value.origowner));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that city was not originally owned by %s."),
                     nation_adjective_translation(preq->source.value.origowner));
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_DIPLREL:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: in this and following strings, '%s' can be one
                      * of a wide range of relationships; e.g., 'Peace',
                      * 'Never met', 'Foreign', 'Hosts embassy',
                      * 'Provided Casus Belli' */
                     _("Requires that you have the relationship '%s' with at "
                       "least one other living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that you do not have the relationship '%s' "
                       "with any living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_TEAM:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that somebody on your team has the "
                       "relationship '%s' with at least one other living "
                       "player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that nobody on your team has the "
                       "relationship '%s' with any living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_ALLIANCE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that somebody in your alliance has the "
                       "relationship '%s' with at least one other living "
                       "player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that nobody in your alliance has the "
                       "relationship '%s' with any living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires the relationship '%s' between two living "
                       "players."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that no two living players have the "
                       "relationship '%s'."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that you have the relationship '%s' with the "
                       "other player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that you do not have the relationship '%s' "
                       "with the other player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_DIPLREL_TILE:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: in this and following strings, '%s' can be one
                      * of a wide range of relationships; e.g., 'Peace',
                      * 'Never met', 'Foreign', 'Hosts embassy',
                      * 'Provided Casus Belli' */
                     _("Requires that the tile owner has the relationship"
                       " '%s' with at least one other living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that the tile owner does not have the"
                       " relationship '%s' with any living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_TEAM:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that somebody on the tile owner's team"
                       " has the relationship '%s' with at least one other"
                       " living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that nobody on the tile owner's team has"
                       " the relationship '%s' with any living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_ALLIANCE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that somebody in the tile owner's alliance"
                       " has the relationship '%s' with at least one other "
                       "living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that nobody in the tile owner's alliance "
                       "has the relationship '%s' with any living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that you have the relationship '%s' with"
                       " the tile owner."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that you do not have the relationship '%s'"
                       " with the tile owner."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_DIPLREL_TILE_O:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that the tile owner has the relationship"
                       " '%s' with the other player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that the tile owner does not have the"
                       " relationship '%s' with the other player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_DIPLREL_UNITANY:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: in this and following strings, '%s' can be one
                      * of a wide range of relationships; e.g., 'Peace',
                      * 'Never met', 'Foreign', 'Hosts embassy',
                      * 'Provided Casus Belli' */
                     _("Requires that the tile has at least one unit whose "
                       "owner has the relationship "
                       "'%s' with at least one other living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that no unit at the tile is owned by a player "
                       "who has the relationship '%s' with any living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_TEAM:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that the tile has at least one unit whose "
                       "owner is on a team where a member "
                       "has the relationship '%s' with at least one other "
                       "living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that no unit at the tile is owned by a player "
                       "who is on a team where a member "
                       "has the relationship '%s' with at least one other "
                       "living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_ALLIANCE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that the tile has at least one unit whose "
                       "owner is allied to someone that "
                       "has the relationship '%s' with at least one other "
                       "living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that no unit at the tile is owned by a player "
                       "allied to someone that "
                       "has the relationship '%s' with any living player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that you have the relationship '%s' with "
                       "the owner of at least one unit at the tile."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that you do not have the relationship '%s' "
                       "with the owner of any unit at the tile."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_DIPLREL_UNITANY_O:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires that the tile has at least one unit whose "
                       "owner has the relationship '%s' with the other player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Requires that no unit at the tile is owned by a player "
                       "who has the relationship '%s' with the other player."),
                     diplrel_name_translation(preq->source.value.diplrel));
      }
      return TRUE;
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_UTYPE:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        /* TRANS: %s is a single kind of unit (e.g., "Settlers"). */
        cat_snprintf(buf, bufsz, Q_("?unit:Requires %s."),
                     utype_name_translation(preq->source.value.utype));
      } else {
        /* TRANS: %s is a single kind of unit (e.g., "Settlers"). */
        cat_snprintf(buf, bufsz, Q_("?unit:Does not apply to %s."),
                     utype_name_translation(preq->source.value.utype));
      }
      return TRUE;
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_UTFLAG:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      {
        struct astring astr = ASTRING_INIT;

         /* Unit type flags mean nothing to users. Explicitly list the unit
         * types with those flags. */
        if (role_units_translations(&astr, preq->source.value.unitflag,
                                    TRUE)) {
          fc_strlcat(buf, prefix, bufsz);
          if (preq->present) {
            /* TRANS: %s is a list of unit types separated by "or". */
            cat_snprintf(buf, bufsz, Q_("?ulist:Requires %s."),
                         astr_str(&astr));
          } else {
            /* TRANS: %s is a list of unit types separated by "or". */
            cat_snprintf(buf, bufsz, Q_("?ulist:Does not apply to %s."),
                         astr_str(&astr));
          }
          astr_free(&astr);
          return TRUE;
        }
      }
      break;
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_UCLASS:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        /* TRANS: %s is a single unit class (e.g., "Air"). */
        cat_snprintf(buf, bufsz, Q_("?uclass:Requires %s units."),
                     uclass_name_translation(preq->source.value.uclass));
      } else {
        /* TRANS: %s is a single unit class (e.g., "Air"). */
        cat_snprintf(buf, bufsz, Q_("?uclass:Does not apply to %s units."),
                     uclass_name_translation(preq->source.value.uclass));
      }
      return TRUE;
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_UCFLAG:
    {
      const char *classes[uclass_count()];
      int i = 0;
      bool done = FALSE;
      struct astring list = ASTRING_INIT;

      unit_class_iterate(uclass) {
        if (uclass_has_flag(uclass, preq->source.value.unitclassflag)) {
          classes[i++] = uclass_name_translation(uclass);
        }
      } unit_class_iterate_end;
      astr_build_or_list(&list, classes, i);

      switch (preq->range) {
      case REQ_RANGE_LOCAL:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          /* TRANS: %s is a list of unit classes separated by "or". */
          cat_snprintf(buf, bufsz, Q_("?uclasslist:Requires %s units."),
                       astr_str(&list));
        } else {
          /* TRANS: %s is a list of unit classes separated by "or". */
          cat_snprintf(buf, bufsz, Q_("?uclasslist:Does not apply to "
                                      "%s units."),
                       astr_str(&list));
        }
        done = TRUE;
        break;
      case REQ_RANGE_TILE:
      case REQ_RANGE_CADJACENT:
      case REQ_RANGE_ADJACENT:
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }
      astr_free(&list);
      if (done) {
        return TRUE;
      }
    }
    break;

  case VUT_UNITSTATE:
    {
      switch (preq->range) {
      case REQ_RANGE_LOCAL:
        switch (preq->source.value.unit_state) {
        case USP_TRANSPORTED:
          fc_strlcat(buf, prefix, bufsz);
          if (preq->present) {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit is transported."));
          } else {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit isn't transported."));
          }
          return TRUE;
        case USP_LIVABLE_TILE:
          fc_strlcat(buf, prefix, bufsz);
          if (preq->present) {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit is on livable tile."));
          } else {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit isn't on livable tile."));
          }
          return TRUE;
        case USP_TRANSPORTING:
          fc_strlcat(buf, prefix, bufsz);
          if (preq->present) {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit does transport one or "
                           "more cargo units."));
          } else {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit doesn't transport "
                           "any cargo units."));
          }
          return TRUE;
        case USP_HAS_HOME_CITY:
          fc_strlcat(buf, prefix, bufsz);
          if (preq->present) {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit has a home city."));
          } else {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit is homeless."));
          }
          return TRUE;
        case USP_NATIVE_TILE:
          fc_strlcat(buf, prefix, bufsz);
          if (preq->present) {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit is on native tile."));
          } else {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit isn't on native tile."));
          }
          return TRUE;
        case USP_NATIVE_EXTRA:
          fc_strlcat(buf, prefix, bufsz);
          if (preq->present) {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit is in a native extra."));
          } else {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit isn't in a native extra."));
          }
          return TRUE;
        case USP_MOVED_THIS_TURN:
          fc_strlcat(buf, prefix, bufsz);
          if (preq->present) {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit has moved this turn."));
          } else {
            cat_snprintf(buf, bufsz,
                         _("Requires that the unit hasn't moved this turn."));
          }
          return TRUE;
        case USP_COUNT:
          fc_assert_msg(preq->source.value.unit_state != USP_COUNT,
                        "Invalid unit state property.");
        }
        break;
      case REQ_RANGE_TILE:
      case REQ_RANGE_CADJACENT:
      case REQ_RANGE_ADJACENT:
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }
    }
    break;

  case VUT_ACTIVITY:
    {
      switch (preq->range) {
      case REQ_RANGE_LOCAL:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          cat_snprintf(buf, bufsz,
                       _("Requires that the unit is performing activity %s."),
                       Q_(unit_activity_name(preq->source.value.activity)));
        } else {
          cat_snprintf(buf, bufsz,
                       _("Requires that the unit is not performing activity %s."),
                       Q_(unit_activity_name(preq->source.value.activity)));
        }
        return TRUE;
      case REQ_RANGE_TILE:
      case REQ_RANGE_CADJACENT:
      case REQ_RANGE_ADJACENT:
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }
    }
    break;

  case VUT_MINMOVES:
    {
      switch (preq->range) {
      case REQ_RANGE_LOCAL:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is numeric move points; it may have a
                        * fractional part ("1 1/3 MP"). */
                       _("Requires that the unit has at least %s MP left."),
                       move_points_text(preq->source.value.minmoves, TRUE));
        } else {
          cat_snprintf(buf, bufsz,
                       /* TRANS: %s is numeric move points; it may have a
                        * fractional part ("1 1/3 MP"). */
                       _("Requires that the unit has less than %s MP left."),
                       move_points_text(preq->source.value.minmoves, TRUE));
        }
        return TRUE;
      case REQ_RANGE_TILE:
      case REQ_RANGE_CADJACENT:
      case REQ_RANGE_ADJACENT:
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }
    }
    break;

  case VUT_MINVETERAN:
    if (preq->range != REQ_RANGE_LOCAL) {
      break;
    }
    /* FIXME: this would be better with veteran level names, but that's
     * potentially unit type dependent. */
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz,
                   PL_("Requires a unit with at least %d veteran level.",
                       "Requires a unit with at least %d veteran levels.",
                       preq->source.value.minveteran),
                   preq->source.value.minveteran);
    } else {
      cat_snprintf(buf, bufsz,
                   PL_("Requires a unit with fewer than %d veteran level.",
                       "Requires a unit with fewer than %d veteran levels.",
                       preq->source.value.minveteran),
                   preq->source.value.minveteran);
    }
    return TRUE;

    case VUT_MINHP:
      if (preq->range != REQ_RANGE_LOCAL) {
        break;
      }

      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("Requires a unit with at least %d hit point left.",
                         "Requires a unit with at least %d hit points left.",
                         preq->source.value.min_hit_points),
                     preq->source.value.min_hit_points);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("Requires a unit with fewer than %d hit point "
                         "left.",
                         "Requires a unit with fewer than %d hit points "
                         "left.",
                         preq->source.value.min_hit_points),
                     preq->source.value.min_hit_points);
      }
      return TRUE;

  case VUT_OTYPE:
    if (preq->range != REQ_RANGE_LOCAL) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      /* TRANS: "Applies only to Food." */
      cat_snprintf(buf, bufsz, Q_("?output:Applies only to %s."),
                   get_output_name(preq->source.value.outputtype));
    } else {
      /* TRANS: "Does not apply to Food." */
      cat_snprintf(buf, bufsz, Q_("?output:Does not apply to %s."),
                   get_output_name(preq->source.value.outputtype));
    }
    return TRUE;

  case VUT_SPECIALIST:
    if (preq->range != REQ_RANGE_LOCAL) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      /* TRANS: "Applies only to Scientists." */
      cat_snprintf(buf, bufsz, Q_("?specialist:Applies only to %s."),
                   specialist_plural_translation(preq->source.value.specialist));
    } else {
      /* TRANS: "Does not apply to Scientists." */
      cat_snprintf(buf, bufsz, Q_("?specialist:Does not apply to %s."),
                   specialist_plural_translation(preq->source.value.specialist));
    }
    return TRUE;

  case VUT_MINSIZE:
    switch (preq->range) {
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("Requires a minimum city size of %d for this "
                         "city or a trade partner.",
                         "Requires a minimum city size of %d for this "
                         "city or a trade partner.",
                         preq->source.value.minsize),
                     preq->source.value.minsize);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("Requires the city size to be less than %d "
                         "for this city and all trade partners.",
                         "Requires the city size to be less than %d "
                         "for this city and all trade partners.",
                         preq->source.value.minsize),
                     preq->source.value.minsize);
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("Requires a minimum city size of %d.",
                         "Requires a minimum city size of %d.",
                         preq->source.value.minsize),
                     preq->source.value.minsize);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("Requires the city size to be less than %d.",
                         "Requires the city size to be less than %d.",
                         preq->source.value.minsize),
                   preq->source.value.minsize);
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_MINCULTURE:
    switch (preq->range) {
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("Requires a minimum culture of %d in the city.",
                         "Requires a minimum culture of %d in the city.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("Requires the culture in the city to be less "
                         "than %d.",
                         "Requires the culture in the city to be less "
                         "than %d.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("Requires a minimum culture of %d in this city or "
                         "a trade partner.",
                         "Requires a minimum culture of %d in this city or "
                         "a trade partner.",
                         preq->source.value.minculture),
                      preq->source.value.minculture);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("Requires the culture in this city and all trade "
                         "partners to be less than %d.",
                         "Requires the culture in this city and all trade "
                         "partners to be less than %d.",
                         preq->source.value.minculture),
                      preq->source.value.minculture);
      }
      return TRUE;
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("Requires your nation to have culture "
                         "of at least %d.",
                         "Requires your nation to have culture "
                         "of at least %d.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("Prevented if your nation has culture of "
                         "%d or more.",
                         "Prevented if your nation has culture of "
                         "%d or more.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      }
      return TRUE;
    case REQ_RANGE_TEAM:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("Requires someone on your team to have culture of "
                         "at least %d.",
                         "Requires someone on your team to have culture of "
                         "at least %d.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("Prevented if anyone on your team has culture of "
                         "%d or more.",
                         "Prevented if anyone on your team has culture of "
                         "%d or more.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      }
      return TRUE;
    case REQ_RANGE_ALLIANCE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("Requires someone in your current alliance to "
                         "have culture of at least %d.",
                         "Requires someone in your current alliance to "
                         "have culture of at least %d.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("Prevented if anyone in your current alliance has "
                         "culture of %d or more.",
                         "Prevented if anyone in your current alliance has "
                         "culture of %d or more.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      }
      return TRUE;
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("Requires that some player has culture of at "
                         "least %d.",
                         "Requires that some player has culture of at "
                         "least %d.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("Requires that no player has culture of %d "
                         "or more.",
                         "Requires that no player has culture of %d "
                         "or more.",
                         preq->source.value.minculture),
                     preq->source.value.minculture);
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      break;
    }
    break;

  case VUT_MINFOREIGNPCT:
    switch (preq->range) {
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("At least %d%% of the citizens of the city "
                       "must be foreign."),
                     preq->source.value.minforeignpct);
      } else {
        cat_snprintf(buf, bufsz,
                     _("Less than %d%% of the citizens of the city "
                       "must be foreign."),
                     preq->source.value.minforeignpct);
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("At least %d%% of the citizens of the city "
                       "or some trade partner must be foreign."),
                     preq->source.value.minforeignpct);
      } else {
        cat_snprintf(buf, bufsz,
                     _("Less than %d%% of the citizens of the city "
                       "and each trade partner must be foreign."),
                      preq->source.value.minforeignpct);
      }
      return TRUE;
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_COUNT:
      break;
    }
    break;

  case VUT_MAXTILETOTALUNITS:
    switch (preq->range) {
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("At most %d total unit may be present on the tile.",
                         "At most %d total units may be present on the tile.",
                         preq->source.value.max_tile_total_units),
                     preq->source.value.max_tile_total_units);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("There must be more than %d total unit present on "
                         "the tile.",
                         "There must be more than %d total units present on "
                         "the tile.",
                         preq->source.value.max_tile_total_units),
                     preq->source.value.max_tile_total_units);
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("The tile or at least one cardinally adjacent tile "
                         "must have %d total unit or fewer.",
                         "The tile or at least one cardinally adjacent tile "
                         "must have %d total units or fewer.",
                         preq->source.value.max_tile_total_units),
                     preq->source.value.max_tile_total_units);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("The tile and all cardinally adjacent tiles must "
                         "have more than %d total unit each.",
                         "The tile and all cardinally adjacent tiles must "
                         "have more than %d total units each.",
                         preq->source.value.max_tile_total_units),
                     preq->source.value.max_tile_total_units);
      }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("The tile or at least one adjacent tile must have "
                         "%d total unit or fewer.",
                         "The tile or at least one adjacent tile must have "
                         "%d total units or fewer.",
                         preq->source.value.max_tile_total_units),
                     preq->source.value.max_tile_total_units);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("The tile and all adjacent tiles must have more "
                         "than %d total unit each.",
                         "The tile and all adjacent tiles must have more "
                         "than %d total units each.",
                         preq->source.value.max_tile_total_units),
                     preq->source.value.max_tile_total_units);
      }
      return TRUE;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_MAXTILETOPUNITS:
    switch (preq->range) {
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("At most %d untransported unit may be present on the tile.",
                         "At most %d untransported units may be present on the tile.",
                         preq->source.value.max_tile_top_units),
                     preq->source.value.max_tile_top_units);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("There must be more than %d untransported unit present on "
                         "the tile.",
                         "There must be more than %d untransported units present on "
                         "the tile.",
                         preq->source.value.max_tile_top_units),
                     preq->source.value.max_tile_top_units);
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("The tile or at least one cardinally adjacent tile "
                         "must have %d untransporte unit or fewer.",
                         "The tile or at least one cardinally adjacent tile "
                         "must have %d untransporte units or fewer.",
                         preq->source.value.max_tile_top_units),
                     preq->source.value.max_tile_top_units);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("The tile and all cardinally adjacent tiles must "
                         "have more than %d untransported unit each.",
                         "The tile and all cardinally adjacent tiles must "
                         "have more than %d untransported units each.",
                         preq->source.value.max_tile_top_units),
                     preq->source.value.max_tile_top_units);
      }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     PL_("The tile or at least one adjacent tile must have "
                         "%d untransported unit or fewer.",
                         "The tile or at least one adjacent tile must have "
                         "%d untransported units or fewer.",
                         preq->source.value.max_tile_top_units),
                     preq->source.value.max_tile_top_units);
      } else {
        cat_snprintf(buf, bufsz,
                     PL_("The tile and all adjacent tiles must have "
                         "more than %d untransported unit each.",
                         "The tile and all adjacent tiles must have "
                         "more than %d untransported units each.",
                         preq->source.value.max_tile_top_units),
                     preq->source.value.max_tile_top_units);
      }
      return TRUE;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_AI_LEVEL:
    if (preq->range != REQ_RANGE_PLAYER) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz,
                   /* TRANS: AI level (e.g., "Handicapped") */
                   _("Applies to %s AI players."),
                   ai_level_translated_name(preq->source.value.ai_level));
    } else {
      cat_snprintf(buf, bufsz,
                   /* TRANS: AI level (e.g., "Cheating") */
                   _("Does not apply to %s AI players."),
                   ai_level_translated_name(preq->source.value.ai_level));
    }
    return TRUE;

  case VUT_TERRAINCLASS:
    switch (preq->range) {
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Requires %s terrain on the tile."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Prevented by %s terrain on the tile."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Requires %s terrain on the tile or a "
                        "cardinally adjacent tile."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Prevented by %s terrain on the tile or "
                        "any cardinally adjacent tile."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Requires %s terrain on the tile or an "
                        "adjacent tile."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Prevented by %s terrain on the tile or "
                        "any adjacent tile."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Requires %s terrain on a tile within "
                        "the city radius."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Prevented by %s terrain on any tile "
                        "within the city radius."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Requires %s terrain on a tile within "
                        "the city radius or the city radius of a trade "
                        "partner."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a terrain class */
                     Q_("?terrainclass:Prevented by %s terrain on any tile "
                        "within the city radius or the city radius of a trade "
                        "partner."),
                     terrain_class_name_translation
                     (preq->source.value.terrainclass));
      }
      return TRUE;
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_TERRFLAG:
    switch (preq->range) {
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Requires terrain with the \"%s\" flag on the tile."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Prevented by terrain with the \"%s\" flag on the "
                       "tile."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Requires terrain with the \"%s\" flag on the "
                       "tile or a cardinally adjacent tile."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Prevented by terrain with the \"%s\" flag on "
                       "the tile or any cardinally adjacent tile."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Requires terrain with the \"%s\" flag on the "
                       "tile or an adjacent tile."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Prevented by terrain with the \"%s\" flag on "
                       "the tile or any adjacent tile."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Requires terrain with the \"%s\" flag on a tile "
                       "within the city radius."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Prevented by terrain with the \"%s\" flag on any tile "
                       "within the city radius."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Requires terrain with the \"%s\" flag on a tile "
                       "within the city radius or the city radius of "
                       "a trade partner."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) terrain flag. */
                     _("Prevented by terrain with the \"%s\" flag on any tile "
                       "within the city radius or the city radius of "
                       "a trade partner."),
                     terrain_flag_id_translated_name(preq->source.value.terrainflag));
      }
      return TRUE;
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_ROADFLAG:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Only applies to roads with the \"%s\" flag."),
                     road_flag_id_translated_name
                     (preq->source.value.roadflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Does not apply to roads with the \"%s\" flag."),
                     road_flag_id_translated_name
                     (preq->source.value.roadflag));
      }
      return TRUE;
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Requires a road with the \"%s\" flag on the tile."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Prevented by a road with the \"%s\" flag on the "
                       "tile."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Requires a road with the \"%s\" flag on the "
                       "tile or a cardinally adjacent tile."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Prevented by a road with the \"%s\" flag on "
                       "the tile or any cardinally adjacent tile."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Requires a road with the \"%s\" flag on the "
                       "tile or an adjacent tile."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Prevented by a road with the \"%s\" flag on "
                       "the tile or any adjacent tile."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Requires a road with the \"%s\" flag on a tile "
                       "within the city radius."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Prevented by a road with the \"%s\" flag on any tile "
                       "within the city radius."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Requires a road with the \"%s\" flag on a tile "
                       "within the city radius or the city radius of a "
                       "trade partner."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) road flag. */
                     _("Prevented by a road with the \"%s\" flag on any tile "
                       "within the city radius or the city radius of a "
                       "trade partner."),
                     road_flag_id_translated_name(preq->source.value.roadflag));
      }
      return TRUE;
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_EXTRAFLAG:
    switch (preq->range) {
    case REQ_RANGE_LOCAL:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Only applies to extras with the \"%s\" flag."),
                     extra_flag_id_translated_name
                     (preq->source.value.extraflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Does not apply to extras with the \"%s\" flag."),
                     extra_flag_id_translated_name
                     (preq->source.value.extraflag));
      }
      return TRUE;
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Requires an extra with the \"%s\" flag on the tile."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Prevented by an extra with the \"%s\" flag on the "
                       "tile."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Requires an extra with the \"%s\" flag on the "
                       "tile or a cardinally adjacent tile."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Prevented by an extra with the \"%s\" flag on "
                       "the tile or any cardinally adjacent tile."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Requires an extra with the \"%s\" flag on the "
                       "tile or an adjacent tile."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Prevented by an extra with the \"%s\" flag on "
                       "the tile or any adjacent tile."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      }
      return TRUE;
    case REQ_RANGE_CITY:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Requires an extra with the \"%s\" flag on a tile "
                       "within the city radius."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Prevented by an extra with the \"%s\" flag on any tile "
                       "within the city radius."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      }
      return TRUE;
    case REQ_RANGE_TRADE_ROUTE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Requires an extra with the \"%s\" flag on a tile "
                       "within the city radius or the city radius of a "
                       "trade partner."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      } else {
        cat_snprintf(buf, bufsz,
                     /* TRANS: %s is a (translatable) extra flag. */
                     _("Prevented by an extra with the \"%s\" flag on any tile "
                       "within the city radius or the city radius of a "
                       "trade partner."),
                     extra_flag_id_translated_name(preq->source.value.extraflag));
      }
      return TRUE;
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_MINYEAR:
    if (preq->range != REQ_RANGE_WORLD) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz,
                   _("Requires the game to have reached the year %s."),
                   textyear(preq->source.value.minyear));
    } else {
      cat_snprintf(buf, bufsz,
                   _("Requires that the game has not yet reached the "
                     "year %s."),
                   textyear(preq->source.value.minyear));
    }
    return TRUE;

  case VUT_MINCALFRAG:
    if (preq->range != REQ_RANGE_WORLD) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz,
                   /* TRANS: %s is a representation of a calendar fragment,
                    * from the ruleset. May be a bare number. */
                   _("Requires the game to have reached %s."),
                   textcalfrag(preq->source.value.mincalfrag));
    } else {
      cat_snprintf(buf, bufsz,
                   /* TRANS: %s is a representation of a calendar fragment,
                    * from the ruleset. May be a bare number. */
                   _("Requires that the game has not yet reached %s."),
                   textcalfrag(preq->source.value.mincalfrag));
    }
    return TRUE;

  case VUT_TOPO:
    if (preq->range != REQ_RANGE_WORLD) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz,
                   /* TRANS: topology flag name ("Hex", "ISO") */
                   _("Requires %s map."),
                   _(topo_flag_name(preq->source.value.topo_property)));
    } else {
      cat_snprintf(buf, bufsz,
                   /* TRANS: topology flag name ("Hex", "ISO") */
                   _("Prevented on %s map."),
                   _(topo_flag_name(preq->source.value.topo_property)));
    }
    return TRUE;

  case VUT_WRAP:
    if (preq->range != REQ_RANGE_WORLD) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz,
                   /* TRANS: wrap flag name ("WrapX", "WrapY") */
                   _("Requires %s map."),
                   _(wrap_flag_name(preq->source.value.wrap_property)));
    } else {
      cat_snprintf(buf, bufsz,
                   /* TRANS: wrap flag name ("WrapX", "WrapY") */
                   _("Prevented on %s map."),
                   _(wrap_flag_name(preq->source.value.wrap_property)));
    }
    return TRUE;

  case VUT_SERVERSETTING:
    if (preq->range != REQ_RANGE_WORLD) {
      break;
    }
    fc_strlcat(buf, prefix, bufsz);
    cat_snprintf(buf, bufsz,
                 /* TRANS: %s is a server setting, its value and if it is
                  * required to be present or absent. The string's format
                  * is specified in ssetv_human_readable().
                  * Example: "killstack is enabled". */
                 _("Requires that the server setting %s."),
                 ssetv_human_readable(preq->source.value.ssetval,
                                      preq->present));
    return TRUE;

  case VUT_AGE:
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz,
                   _("Requires age of %d turns."),
                   preq->source.value.age);
    } else {
      cat_snprintf(buf, bufsz,
                   _("Prevented if age is over %d turns."),
                   preq->source.value.age);
    }
    return TRUE;

  case VUT_FORM_AGE:
    fc_strlcat(buf, prefix, bufsz);
    if (preq->present) {
      cat_snprintf(buf, bufsz,
                   _("Requires form age of %d turns."),
                   preq->source.value.form_age);
    } else {
      cat_snprintf(buf, bufsz,
                   _("Prevented if form age is over %d turns."),
                   preq->source.value.form_age);
    }
    return TRUE;

  case VUT_MINTECHS:
    switch (preq->range) {
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires %d techs to be known in the world."),
                     preq->source.value.min_techs);
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented when %d techs are known in the world."),
                     preq->source.value.min_techs);
      }
      return TRUE;
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires player to know %d techs."),
                     preq->source.value.min_techs);
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented when player knows %d techs."),
                     preq->source.value.min_techs);
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_FUTURETECHS:
    switch (preq->range) {
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires %d future techs to be known in the world."),
                     preq->source.value.future_techs);
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented when %d future techs are known in the world."),
                     preq->source.value.future_techs);
      }
      return TRUE;
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires player to know %d future techs."),
                     preq->source.value.future_techs);
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented when player knows %d future techs."),
                     preq->source.value.future_techs);
      }
      return TRUE;
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_MINCITIES:
    switch (preq->range) {
    case REQ_RANGE_PLAYER:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires player to have at least %d cities."),
                     preq->source.value.min_cities);
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented when player has at least %d cities."),
                     preq->source.value.min_cities);
      }
      return TRUE;
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_TERRAINALTER:
    switch (preq->range) {
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Requires terrain on which alteration %s is "
                       "possible."),
                     Q_(terrain_alteration_name(preq->source.value.terrainalter)));
      } else {
        cat_snprintf(buf, bufsz,
                     _("Prevented by terrain on which alteration %s "
                       "can be made."),
                     Q_(terrain_alteration_name(preq->source.value.terrainalter)));
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_CITYTILE:
    if (preq->source.value.citytile == CITYT_LAST) {
      break;
    } else {
      static char *tile_property = nullptr;

      switch (preq->source.value.citytile) {
      case CITYT_CENTER:
        tile_property = _("city centers");
        break;
      case CITYT_CLAIMED:
        tile_property = _("claimed tiles");
        break;
      case CITYT_EXTRAS_OWNED:
        tile_property = _("owned extras");
        break;
      case CITYT_WORKED:
        tile_property = _("worked tiles");
        break;
      case CITYT_SAME_CONTINENT:
        /* TRANS: a specific city for each use case */
        tile_property = _("tiles on the same continent as the city");
        break;
      case CITYT_BORDERING_TCLASS_REGION:
        {
          bool oceanic_cities = FALSE; /* FIXME: maybe cache globally? */

          terrain_type_iterate(tt) {
            if (terrain_type_terrain_class(tt) == TC_OCEAN
                && !terrain_has_flag(tt, TER_NO_CITIES)) {
              oceanic_cities = TRUE;
              /* TRANS: a specific city for each use case */
              tile_property = _("tiles of a mass of a different "
                                "terrain class next to the city");
              break;
            }
          } terrain_type_iterate_end;
          if (oceanic_cities) {
            break;
          }
          /* TRANS: a specific city for each use case */
          tile_property = _("tiles of a body of water next to the city");
        }
        break;
      case CITYT_LAST:
        fc_assert(preq->source.value.citytile != CITYT_LAST);
        break;
      }

      switch (preq->range) {
      case REQ_RANGE_TILE:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          cat_snprintf(buf, bufsz,
                       /* TRANS: tile property ("city centers", etc) */
                       Q_("?tileprop:Applies only to %s."), tile_property);
        } else {
          cat_snprintf(buf, bufsz,
                       /* TRANS: tile property ("city centers", etc) */
                       Q_("?tileprop:Does not apply to %s."), tile_property);
        }
        return TRUE;
      case REQ_RANGE_CADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          /* TRANS: tile property ("city centers", etc) */
          cat_snprintf(buf, bufsz, Q_("?tileprop:Applies only to %s and "
                                      "cardinally adjacent tiles."),
                       tile_property);
        } else {
          /* TRANS: tile property ("city centers", etc) */
          cat_snprintf(buf, bufsz, Q_("?tileprop:Does not apply to %s or "
                                      "cardinally adjacent tiles."),
                       tile_property);
        }
        return TRUE;
      case REQ_RANGE_ADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          /* TRANS: tile property ("city centers", etc) */
          cat_snprintf(buf, bufsz, Q_("?tileprop:Applies only to %s and "
                                      "adjacent tiles."), tile_property);
        } else {
          /* TRANS: tile property ("city centers", etc) */
          cat_snprintf(buf, bufsz, Q_("?tileprop:Does not apply to %s or "
                                      "adjacent tiles."), tile_property);
        }
        return TRUE;
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_LOCAL:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }

      break;
    }

  case VUT_CITYSTATUS:
    if (preq->source.value.citystatus != CITYS_LAST) {
      static char *city_property = nullptr;

      switch (preq->source.value.citystatus) {
      case CITYS_OWNED_BY_ORIGINAL:
        city_property = _("owned by original");
        break;
      case CITYS_STARVED:
        city_property = _("starved");
        break;
      case CITYS_DISORDER:
        city_property = _("disorder");
        break;
      case CITYS_CELEBRATION:
        city_property = _("celebration");
        break;
      case CITYS_TRANSFERRED:
        city_property = _("transferred");
        break;
      case CITYS_CAPITALCONNECTED:
        city_property = _("capitalconnected");
        break;
      case CITYS_LAST:
        fc_assert(preq->source.value.citystatus != CITYS_LAST);
        break;
      }

      switch (preq->range) {
      case REQ_RANGE_CITY:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          /* TRANS: city property ("owned by original", etc) */
          cat_snprintf(buf, bufsz, Q_("?cityprop:Applies only to %s cities"),
                       city_property);
        } else {
          /* TRANS: city property ("owned by original", etc) */
          cat_snprintf(buf, bufsz, Q_("?cityprop:Does not apply to %s cities"),
                       city_property);
        }
        return TRUE;
      case REQ_RANGE_TRADE_ROUTE:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          /* TRANS: city property ("owned by original", etc) */
          cat_snprintf(buf, bufsz, Q_("?cityprop:Applies only to %s cities or "
                                      "their trade partners."), city_property);
        } else {
          /* TRANS: city property ("owned by original", etc) */
          cat_snprintf(buf, bufsz, Q_("?cityprop:Does not apply to %s cities or "
                                      "their trade partners."), city_property);
        }
        return TRUE;
      case REQ_RANGE_LOCAL:
      case REQ_RANGE_TILE:
      case REQ_RANGE_ADJACENT:
      case REQ_RANGE_CADJACENT:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }
    }
    break;

  case VUT_MINLATITUDE:
    switch (preq->range) {
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Some part of the world must be at latitude %d or "
                       "further north."),
                     preq->source.value.latitude);
      } else {
        cat_snprintf(buf, bufsz,
                     _("The entire world must be at latitude %d or "
                       "further south."),
                     preq->source.value.latitude - 1);
      }
      return TRUE;
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Tile must be at latitude %d or further north."),
                     preq->source.value.latitude);
      } else {
        cat_snprintf(buf, bufsz,
                     _("Tile must be at latitude %d or further south."),
                     preq->source.value.latitude - 1);
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("A cardinally adjacent tile must be at latitude "
                       "%d or further north."),
                     preq->source.value.latitude);
      } else {
        cat_snprintf(buf, bufsz,
                     _("All cardinally adjacent tiles must be at latitude "
                       "%d or further south."),
                     preq->source.value.latitude - 1);
      }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("An adjacent tile must be at latitude %d or "
                       "further north."),
                     preq->source.value.latitude);
      } else {
        cat_snprintf(buf, bufsz,
                     _("All adjacent tiles must be at latitude %d or "
                       "further south."),
                     preq->source.value.latitude - 1);
      }
      return TRUE;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_MAXLATITUDE:
    switch (preq->range) {
    case REQ_RANGE_WORLD:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Some part of the world must be at latitude %d or "
                       "further south."),
                     preq->source.value.latitude);
      } else {
        cat_snprintf(buf, bufsz,
                     _("The entire world must be at latitude %d or "
                       "further north."),
                     preq->source.value.latitude + 1);
      }
      return TRUE;
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("Tile must be at latitude %d or further south."),
                     preq->source.value.latitude);
      } else {
        cat_snprintf(buf, bufsz,
                     _("Tile must be at latitude %d or further north."),
                     preq->source.value.latitude + 1);
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("A cardinally adjacent tile must be at latitude "
                       "%d or further south."),
                     preq->source.value.latitude);
      } else {
        cat_snprintf(buf, bufsz,
                     _("All cardinally adjacent tiles must be at latitude "
                       "%d or further north."),
                     preq->source.value.latitude + 1);
      }
      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                     _("An adjacent tile must be at latitude %d or "
                       "further south."),
                     preq->source.value.latitude);
      } else {
        cat_snprintf(buf, bufsz,
                     _("All adjacent tiles must be at latitude %d or "
                       "further north."),
                     preq->source.value.latitude + 1);
      }
      return TRUE;
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_MAX_DISTANCE_SQ:
    switch (preq->range) {
    case REQ_RANGE_TILE:
      fc_strlcat(buf, prefix, bufsz);
      /* Test some special cases */
      switch (preq->source.value.distance_sq)
      {
      case 0:
        if (preq->present) {
          fc_strlcat(buf, _("Must be the same tile."), bufsz);
        } else {
          fc_strlcat(buf, _("Must not be the same tile."), bufsz);
        }
        break;
      case 1:
        if (preq->present) {
          fc_strlcat(buf, _("Must be cardinally adjacent."), bufsz);
        } else {
          fc_strlcat(buf, _("Must not be cardinally adjacent."), bufsz);
        }
        break;
      case 2:
      case 3:
        if (preq->present) {
          fc_strlcat(buf, _("Must be adjacent."), bufsz);
        } else {
          fc_strlcat(buf, _("Must not be adjacent."), bufsz);
        }
        break;

      default:
        if (preq->present) {
          cat_snprintf(buf, bufsz,
                      _("The squared distance between the tiles "
                        "must be at most %d."),
                      preq->source.value.distance_sq);
        } else {
          cat_snprintf(buf, bufsz,
                      _("The squared distance between the tiles "
                        "must be at least %d."),
                      preq->source.value.distance_sq + 1);
        }
        break;
      }
      return TRUE;
    case REQ_RANGE_CADJACENT:
    case REQ_RANGE_ADJACENT:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_CONTINENT:
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_MAX_REGION_TILES:
    switch (preq->range) {
    case REQ_RANGE_CADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      /* Off-by-one: The requirement counts the tile itself, we're phrasing
       * the helptext in terms of *other* tiles only. */
      if (preq->present) {
        if (preq->source.value.region_tiles == 1) {
          /* Special case for zero */
          fc_strlcat(buf,
                     _("No other cardinally adjacent tile may be part of "
                       "the same continent or ocean."),
                     bufsz);
        } else {
          cat_snprintf(buf, bufsz,
                      PL_("No more than %d other cardinally adjacent tile "
                          "may be part of the same continent or ocean.",
                          "No more than %d other cardinally adjacent tiles "
                          "may be part of the same continent or ocean.",
                          preq->source.value.region_tiles - 1),
                      preq->source.value.region_tiles - 1);
        }
      } else {
        cat_snprintf(buf, bufsz,
                    PL_("Requires at least %d other cardinally adjacent "
                        "tile of the same continent or ocean.",
                        "Requires at least %d other cardinally adjacent "
                        "tiles of the same continent or ocean.",
                        preq->source.value.region_tiles),
                    preq->source.value.region_tiles);
      }

      return TRUE;
    case REQ_RANGE_ADJACENT:
      fc_strlcat(buf, prefix, bufsz);
      /* Off-by-one: The requirement counts the tile itself, we're phrasing
       * the helptext in terms of *other* tiles only. */
      if (preq->present) {
        if (preq->source.value.region_tiles == 1) {
          /* Special case for zero */
          fc_strlcat(buf,
                     _("No other adjacent tile may be part of the same "
                       "continent or ocean."),
                     bufsz);
        } else {
          cat_snprintf(buf, bufsz,
                      PL_("No more than %d other adjacent tile may be "
                          "part of the same continent or ocean.",
                          "No more than %d other adjacent tiles may be "
                          "part of the same continent or ocean.",
                          preq->source.value.region_tiles - 1),
                      preq->source.value.region_tiles - 1);
        }
      } else {
        cat_snprintf(buf, bufsz,
                    PL_("Requires at least %d other adjacent tile of the "
                        "same continent or ocean.",
                        "Requires at least %d other adjacent tiles of the "
                        "same continent or ocean.",
                        preq->source.value.region_tiles),
                    preq->source.value.region_tiles);
      }

      return TRUE;
    case REQ_RANGE_CONTINENT:
      fc_strlcat(buf, prefix, bufsz);
      if (preq->present) {
        cat_snprintf(buf, bufsz,
                    _("Requires a continent or ocean size of at most %d."),
                    preq->source.value.region_tiles);
      } else {
        cat_snprintf(buf, bufsz,
                    _("Requires a continent or ocean size of at least %d."),
                    preq->source.value.region_tiles + 1);
      }

      return TRUE;
    case REQ_RANGE_PLAYER:
    case REQ_RANGE_TEAM:
    case REQ_RANGE_ALLIANCE:
    case REQ_RANGE_WORLD:
    case REQ_RANGE_LOCAL:
    case REQ_RANGE_TILE:
    case REQ_RANGE_CITY:
    case REQ_RANGE_TRADE_ROUTE:
    case REQ_RANGE_COUNT:
      /* Not supported. */
      break;
    }
    break;

  case VUT_TILE_REL:
    switch (preq->source.value.tilerel) {
    case TREL_SAME_TCLASS:
      switch (preq->range) {
      case REQ_RANGE_TILE:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("Must be on the same terrain class."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must be on a different terrain class."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_CADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("Must be cardinally adjacent to the same "
                            "terrain class."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must not be cardinally adjacent to the same "
                            "terrain class."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_ADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("Must be adjacent to the same terrain class."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must not be adjacent to the same terrain "
                            "class."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_LOCAL:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }
      break;
    case TREL_SAME_REGION:
      switch (preq->range) {
      case REQ_RANGE_TILE:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("Must be on the same continent or ocean."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must be on a different continent or ocean."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_CADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("Must be cardinally adjacent to the same "
                            "continent or ocean."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must not be cardinally adjacent to the same "
                            "continent or ocean."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_ADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("Must be adjacent to the same continent or "
                            "ocean."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must not be adjacent to the same continent "
                            "or ocean."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_LOCAL:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }
      break;
    case TREL_ONLY_OTHER_REGION:
      switch (preq->range) {
      case REQ_RANGE_CADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("May only be cardinally adjacent to this "
                            "other continent or ocean."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must be cardinally adjacent to more than "
                            "just this other continent or ocean."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_ADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("May only be adjacent to this other continent "
                            "or ocean."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must be adjacent to more than just this "
                            "other continent or ocean."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_LOCAL:
      case REQ_RANGE_TILE:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }
      break;
    case TREL_REGION_SURROUNDED:
      switch (preq->range) {
      case REQ_RANGE_TILE:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("Must be on a lake or island surrounded by "
                            "this continent or ocean."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must not be on a lake or island surrounded "
                            "by this continent or ocean."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_CADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("Must be on or cardinally adjacent to a lake "
                            "or island surrounded by this continent or "
                            "ocean."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must not be on nor cardinally adjacent to a "
                            "lake or island surrounded by this continent "
                            "or ocean."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_ADJACENT:
        fc_strlcat(buf, prefix, bufsz);
        if (preq->present) {
          fc_strlcat(buf, _("Must be on or adjacent to a lake or island "
                            "surrounded by this continent or ocean."),
                     bufsz);
        } else {
          fc_strlcat(buf, _("Must not be on nor adjacent to a lake or "
                            "island surrounded by this continent or "
                            "ocean."),
                     bufsz);
        }
        return TRUE;
      case REQ_RANGE_CITY:
      case REQ_RANGE_TRADE_ROUTE:
      case REQ_RANGE_CONTINENT:
      case REQ_RANGE_PLAYER:
      case REQ_RANGE_TEAM:
      case REQ_RANGE_ALLIANCE:
      case REQ_RANGE_WORLD:
      case REQ_RANGE_LOCAL:
      case REQ_RANGE_COUNT:
        /* Not supported. */
        break;
      }
      break;

    case TREL_COUNT:
      /* Invalid. */
      break;
    }
    break;

  case VUT_COUNT:
    break;
  }

  if (verb == VERB_DEFAULT) {
    char text[256];

    log_error("%s requirement %s in range %d is not supported in reqtext.c.",
              preq->present ? "Present" : "Absent",
              universal_name_translation(&preq->source, text, sizeof(text)),
              preq->range);
  }

  return FALSE;
}

/*************************************************************//**
  Append text for the requirement. Added line ends to a newline.
*****************************************************************/
bool req_text_insert_nl(char *buf, size_t bufsz, struct player *pplayer,
                        const struct requirement *preq,
                        enum rt_verbosity verb, const char *prefix)
{
  if (req_text_insert(buf, bufsz, pplayer, preq, verb, prefix)) {
    fc_strlcat(buf, "\n", bufsz);

    return TRUE;
  }

  return FALSE;
}
