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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdarg.h>

/* utility */
#include "astring.h"
#include "fcintl.h"
#include "log.h"
#include "support.h"

/* common */
#include "city.h"
#include "culture.h"
#include "game.h"
#include "specialist.h"
#include "unitlist.h"

/* client/include */
#include "citydlg_g.h"
#include "mapview_g.h"

/* client */
#include "citydlg_common.h"
#include "client_main.h"                /* For can_client_issue_orders() */
#include "climap.h"
#include "control.h"
#include "mapview_common.h"
#include "options.h"            /* For concise_city_production */
#include "tilespec.h"           /* For tileset_is_isometric(tileset) */

static int citydlg_map_width, citydlg_map_height;

/**********************************************************************//**
  Return the width of the city dialog canvas.
**************************************************************************/
int get_citydlg_canvas_width(void)
{
  return citydlg_map_width;
}

/**********************************************************************//**
  Return the height of the city dialog canvas.
**************************************************************************/
int get_citydlg_canvas_height(void)
{
  return citydlg_map_height;
}

/**********************************************************************//**
  Calculate the citydlg width and height.
**************************************************************************/
void generate_citydlg_dimensions(void)
{
  int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
  int max_rad = rs_max_city_radius_sq();

  /* Use maximum possible squared city radius. */
  city_map_iterate_without_index(max_rad, city_x, city_y) {
    float canvas_x, canvas_y;

    map_to_gui_vector(get_tileset(), 1.0, &canvas_x, &canvas_y,
                      CITY_ABS2REL(city_x), CITY_ABS2REL(city_y));

    min_x = MIN(canvas_x, min_x);
    max_x = MAX(canvas_x, max_x);
    min_y = MIN(canvas_y, min_y);
    max_y = MAX(canvas_y, max_y);
  } city_map_iterate_without_index_end;

  citydlg_map_width = max_x - min_x + tileset_tile_width(get_tileset());
  citydlg_map_height = max_y - min_y + tileset_tile_height(get_tileset());
}

/**********************************************************************//**
  Converts a (cartesian) city position to citymap canvas coordinates.
  Returns TRUE if the city position is valid.
**************************************************************************/
bool city_to_canvas_pos(float *canvas_x, float *canvas_y, int city_x,
                        int city_y, int city_radius_sq)
{
  const int width = get_citydlg_canvas_width();
  const int height = get_citydlg_canvas_height();

  /* The citymap is centered over the center of the citydlg canvas. */
  map_to_gui_vector(tileset, 1.0, canvas_x, canvas_y, CITY_ABS2REL(city_x),
                    CITY_ABS2REL(city_y));
  *canvas_x += (width - tileset_tile_width(tileset)) / 2;
  *canvas_y += (height - tileset_tile_height(tileset)) / 2;

  fc_assert_ret_val(is_valid_city_coords(city_radius_sq, city_x, city_y),
                    FALSE);

  return TRUE;
}

/**********************************************************************//**
  Converts a citymap canvas position to a (cartesian) city coordinate
  position. Returns TRUE iff the city position is valid.
**************************************************************************/
bool canvas_to_city_pos(int *city_x, int *city_y, int city_radius_sq,
                        int canvas_x, int canvas_y)
{
#ifdef FREECIV_DEBUG
  int orig_canvas_x = canvas_x, orig_canvas_y = canvas_y;
#endif
  const int width = get_citydlg_canvas_width();
  const int height = get_citydlg_canvas_height();

  /* The citymap is centered over the center of the citydlg canvas. */
  canvas_x -= (width - tileset_tile_width(get_tileset())) / 2;
  canvas_y -= (height - tileset_tile_height(get_tileset())) / 2;

  if (tileset_is_isometric(get_tileset())) {
    const int W = tileset_tile_width(get_tileset()),
              H = tileset_tile_height(get_tileset());

    /* Shift the tile left so the top corner of the origin tile is at
       canvas position (0,0). */
    canvas_x -= W / 2;

    /* Perform a pi / 4 rotation, with scaling. See gui_to_map_pos()
       for a full explanation. */
    *city_x = DIVIDE(canvas_x * H + canvas_y * W, W * H);
    *city_y = DIVIDE(canvas_y * W - canvas_x * H, W * H);
  } else {
    *city_x = DIVIDE(canvas_x, tileset_tile_width(get_tileset()));
    *city_y = DIVIDE(canvas_y, tileset_tile_height(get_tileset()));
  }

  /* Add on the offset of the top-left corner to get the final
   * coordinates (like in gui_to_map_pos()). */
  *city_x = CITY_REL2ABS(*city_x);
  *city_y = CITY_REL2ABS(*city_y);

  log_debug("canvas_to_city_pos(pos=(%d,%d))=(%d,%d)@radius=%d",
            orig_canvas_x, orig_canvas_y, *city_x, *city_y, city_radius_sq);

  return is_valid_city_coords(city_radius_sq, *city_x, *city_y);
}

/* Iterate over all known tiles in the city. This iteration follows the
 * painter's algorithm and can be used for drawing. */
#define citydlg_iterate(pcity, ptile, pedge, pcorner, _x, _y)           \
{                                                                       \
  float _x##_0, _y##_0;                                                 \
  int _tile_x, _tile_y;                                                 \
  const int _x##_w = get_citydlg_canvas_width();                        \
  const int _y##_h = get_citydlg_canvas_height();                       \
  index_to_map_pos(&_tile_x, &_tile_y, tile_index((pcity)->tile));      \
                                                                        \
  map_to_gui_vector(tileset, 1.0, &_x##_0, &_y##_0, _tile_x, _tile_y);  \
  _x##_0 -= (_x##_w - tileset_tile_width(tileset)) / 2;                 \
  _y##_0 -= (_y##_h - tileset_tile_height(tileset)) / 2;                \
  log_debug("citydlg: %f,%f + %dx%d",                                   \
            _x##_0, _y##_0, _x##_w, _y##_h);                            \
                                                                        \
  gui_rect_iterate_coord(_x##_0, _y##_0, _x##_w, _y##_h,                \
                         ptile, pedge, pcorner, _x##_g, _y##_g, 1.0) {  \
    const int _x = _x##_g - _x##_0;                                     \
    const int _y = _y##_g - _y##_0;                                     \
    {

#define citydlg_iterate_end                                             \
    }                                                                   \
  } gui_rect_iterate_coord_end;                                         \
}

/**********************************************************************//**
  Draw the full city map onto the canvas store. Works for both isometric
  and orthogonal views.
**************************************************************************/
void city_dialog_redraw_map(struct city *pcity,
                            struct canvas *pcanvas)
{
  struct tileset *tmp;

  tmp = NULL;
  if (unscaled_tileset) {
    tmp = tileset;
    tileset = unscaled_tileset;
  }

  /* First make it all black. */
  canvas_put_rectangle(pcanvas, get_color(tileset, COLOR_MAPVIEW_UNKNOWN),
                       0, 0,
                       get_citydlg_canvas_width(),
                       get_citydlg_canvas_height());

  mapview_layer_iterate(layer) {
    citydlg_iterate(pcity, ptile, pedge, pcorner, canvas_x, canvas_y) {
      struct unit *punit
        = ptile ? get_drawable_unit(tileset, ptile, pcity) : NULL;
      struct city *pcity_draw = ptile ? tile_city(ptile) : NULL;

      put_one_element(pcanvas, 1.0, layer, ptile, pedge, pcorner,
                      punit, pcity_draw, canvas_x, canvas_y, pcity, NULL);
    } citydlg_iterate_end;
  } mapview_layer_iterate_end;

  if (tmp != NULL) {
    tileset = tmp;
  }
}

/**********************************************************************//**
  Return a string describing the cost for the production of the city
  considering several build slots for units.
**************************************************************************/
char *city_production_cost_str(const struct city *pcity)
{
  static char cost_str[50];
  int cost = city_production_build_shield_cost(pcity);
  int build_slots = city_build_slots(pcity);
  int num_units;

  if (build_slots > 1
      && city_production_build_units(pcity, TRUE, &num_units)) {
    /* The city could build more than one unit of the selected type */
    if (num_units == 0) {
      /* No unit will be finished this turn but one is build */
      num_units++;
    }

    if (build_slots > num_units) {
      /* Some build slots for units will be unused */
      fc_snprintf(cost_str, sizeof(cost_str), "{%d*%d}", num_units, cost);
    } else {
      /* Maximal number of units will be build */
      fc_snprintf(cost_str, sizeof(cost_str), "[%d*%d]", num_units, cost);
    }
  } else {
    /* Nothing special */
    fc_snprintf(cost_str, sizeof(cost_str), "%3d", cost);
  }

  return cost_str;
}

/**********************************************************************//**
  Find the city dialog city production text for the given city, and
  place it into the buffer. This will check the
  concise_city_production option. pcity may be NULL; in this case a
  filler string is returned.
**************************************************************************/
void get_city_dialog_production(struct city *pcity,
                                char *buffer, size_t buffer_len)
{
  char time_str[50], *cost_str;
  int turns, stock;

  if (pcity == NULL) {
    /*
     * Some GUIs use this to build a "filler string" so that they can
     * properly size the widget to hold the string. This has some
     * obvious problems; the big one is that we have two forms of time
     * information: "XXX turns" and "never". Later this may need to
     * be extended to return the longer of the two; in the meantime
     * translators can fudge it by changing this "filler" string.
     */
    /* TRANS: Use longer of "XXX turns" and "never" */
    fc_strlcpy(buffer, Q_("?filler:XXX/XXX XXX turns"), buffer_len);

    return;
  }

  if (city_production_is_genus(pcity, IG_CONVERT)) {
    int output = MAX(0, pcity->surplus[O_SHIELD]);

    if (city_production_has_flag(pcity, IF_GOLD)) {
      fc_snprintf(buffer, buffer_len,
                  PL_("%3d gold per turn", "%3d gold per turn", output),
                  output);
    } else if (city_production_has_flag(pcity, IF_INFRA)) {
      fc_snprintf(buffer, buffer_len,
                  PL_("%3d infrapoint per turn", "%3d infrapoints per turn", output),
                  output);
    } else {
      fc_strlcpy(buffer, "---", buffer_len);
    }
    return;
  }

  turns = city_production_turns_to_build(pcity, TRUE);
  stock = pcity->shield_stock;
  cost_str = city_production_cost_str(pcity);

  if (turns < FC_INFINITY) {
    if (gui_options.concise_city_production) {
      fc_snprintf(time_str, sizeof(time_str), "%3d", turns);
    } else {
      fc_snprintf(time_str, sizeof(time_str),
                  PL_("%3d turn", "%3d turns", turns), turns);
    }
  } else {
    fc_snprintf(time_str, sizeof(time_str), "%s",
                gui_options.concise_city_production ? "-" : _("never"));
  }

  if (gui_options.concise_city_production) {
    fc_snprintf(buffer, buffer_len, _("%3d/%s:%s"), stock, cost_str,
                time_str);
  } else {
    fc_snprintf(buffer, buffer_len, _("%3d/%s %s"), stock, cost_str,
                time_str);
  }
}

/**********************************************************************//**
  Pretty sprints the info about a production (name, info, cost, turns
  to build) into a single text string.

  This is very similar to get_city_dialog_production_row(); the
  difference is that instead of placing the data into an array of
  strings it all goes into one long string. This means it can be used
  by frontends that do not use a tabled structure, but it also gives
  less flexibility.
**************************************************************************/
void get_city_dialog_production_full(char *buffer, size_t buffer_len,
                                     struct universal *target,
                                     struct city *pcity)
{
  int turns = city_turns_to_build(pcity, target, TRUE);
  int cost = universal_build_shield_cost(pcity, target);

  switch (target->kind) {
  case VUT_IMPROVEMENT:
    fc_strlcpy(buffer,
               city_improvement_name_translation(pcity, target->value.building),
               buffer_len);

    if (is_convert_improvement(target->value.building)) {
      fc_strlcat(buffer, " (--) ", buffer_len);
      if (improvement_has_flag(target->value.building, IF_GOLD)
          || improvement_has_flag(target->value.building, IF_INFRA)) {
        cat_snprintf(buffer, buffer_len, _("%d/turn"),
                     MAX(0, pcity->surplus[O_SHIELD]));
      } else {
        fc_strlcat(buffer, "---", buffer_len);
      }
      return;
    }
    break;
  default:
    universal_name_translation(target, buffer, buffer_len);
    break;
  };
  cat_snprintf(buffer, buffer_len, " (%d) ", cost);

  if (turns < FC_INFINITY) {
    cat_snprintf(buffer, buffer_len,
                 PL_("%d turn", "%d turns", turns),
                 turns);
  } else {
    cat_snprintf(buffer, buffer_len, _("never"));
  }
}

/**********************************************************************//**
  Pretty sprints the info about a production in 4 columns (name, info,
  cost, turns to build). The columns must each have a size of
  column_size bytes. City may be NULL.
**************************************************************************/
void get_city_dialog_production_row(char *buf[], size_t column_size,
                                    struct universal *target,
                                    struct city *pcity)
{
  struct player *pplayer = pcity ? city_owner(pcity) : client_player();

  universal_name_translation(target, buf[0], column_size);

  switch (target->kind) {
  case VUT_UTYPE:
  {
    const struct unit_type *ptype = target->value.utype;

    fc_strlcpy(buf[1], utype_values_string(ptype), column_size);
    fc_snprintf(buf[2], column_size, "(%d)",
                utype_build_shield_cost(pcity, pplayer, ptype));
    break;
  }
  case VUT_IMPROVEMENT:
  {
    const struct impr_type *pimprove = target->value.building;

    /* Total & turns left meaningless on coinage-like improvements */
    if (is_convert_improvement(pimprove)) {
      buf[1][0] = '\0';
      fc_snprintf(buf[2], column_size, "---");
    } else {
      int upkeep = pcity ? city_improvement_upkeep(pcity, pimprove)
                         : pimprove->upkeep;
      /* From city.c city_improvement_name_translation() */
      if (pcity && is_improvement_redundant(pcity, pimprove)) {
        fc_snprintf(buf[1], column_size, "%d*", upkeep);
      } else {
        const char *state = NULL;

        if (is_great_wonder(pimprove)) {
          if (improvement_obsolete(pplayer, pimprove, pcity)) {
            state = _("Obsolete");
          } else if (great_wonder_is_built(pimprove)) {
            state = _("Built");
          } else if (great_wonder_is_destroyed(pimprove)) {
            state = _("Destroyed");
          } else {
            state = _("Great Wonder");
          }
        } else if (is_small_wonder(pimprove)) {
          if (improvement_obsolete(pplayer, pimprove, pcity)) {
            state = _("Obsolete");
          } else if (wonder_is_built(pplayer, target->value.building)) {
            state = _("Built");
          } else {
            state = _("Small Wonder");
          }
        }
        if (state) {
          fc_strlcpy(buf[1], state, column_size);
        } else {
          fc_snprintf(buf[1], column_size, "%d", upkeep);
        }
      }

      if (pcity != NULL) {
        fc_snprintf(buf[2], column_size, "%d",
                    impr_build_shield_cost(pcity, pimprove));
      } else {
        fc_snprintf(buf[2], column_size, "%d",
                    impr_estimate_build_shield_cost(pplayer, NULL, pimprove));
      }
    }
    break;
  }
  default:
    buf[1][0] = '\0';
    buf[2][0] = '\0';
    break;
  };

  /* Add the turns-to-build entry in the 4th position */
  if (pcity) {
    if (VUT_IMPROVEMENT == target->kind
        && is_convert_improvement(target->value.building)) {
      /* Coinage-like improvements: print yield-per-turn instead */
      if (improvement_has_flag(target->value.building, IF_GOLD)
          || improvement_has_flag(target->value.building, IF_INFRA)) {
        fc_snprintf(buf[3], column_size, _("%d/turn"),
                    MAX(0, pcity->surplus[O_SHIELD]));
      } else {
        buf[3][0] = '\0';
      }
    } else {
      int turns = city_turns_to_build(pcity, target, FALSE);

      if (turns < FC_INFINITY) {
        fc_snprintf(buf[3], column_size, "%d", turns);
      } else {
        fc_snprintf(buf[3], column_size, _("never"));
      }
    }
  } else {
    fc_snprintf(buf[3], column_size, "---");
  }
}

/**********************************************************************//**
  Helper structure to accumulate a breakdown of the constributions
  to some numeric city property. Contributions are returned in order,
  with duplicates merged.
**************************************************************************/
struct city_sum {
  const char *format;
  size_t n;
  struct {
    /* The net value that is accumulated. */
    double value;
    /* Description; compared for duplicate-merging.
     * Both of these are maintained/compared until the net 'value' is known;
     * then posdesc is used if value>=0, negdesc if value<0 */
    char *posdesc, *negdesc;
    /* Whether posdesc is printed for total==0 */
    bool suppress_if_zero;
    /* An auxiliary value that is also accumulated, but not tested */
    double aux;
    /* ...and the format string for the net aux value (appended to *desc) */
    const char *auxfmt;
  } *sums;
};

/**********************************************************************//**
  Create a new city_sum.
  'format' is how to print each contribution, of the form "%+4.0f : %s"
  (the first item is the numeric value and must have a 'f' conversion
  spec; the second is the description).
**************************************************************************/
static struct city_sum *city_sum_new(const char *format)
{
  struct city_sum *sum = fc_malloc(sizeof(struct city_sum));

  sum->format = format;
  sum->n = 0;
  sum->sums = NULL;

  return sum;
}

/**********************************************************************//**
  Helper: add a new contribution to the city_sum.
  If 'posdesc'/'negdesc' and other properties match an existing entry,
  'value' is added to the existing entry, else a new one is appended.
**************************************************************************/
static void city_sum_add_real(struct city_sum *sum, double value,
                              bool suppress_if_zero,
                              const char *auxfmt,
                              double aux,
                              char *posdesc,
                              char *negdesc)
{
  size_t i;

  /* Likely to lead to quadratic behavior, but who cares: */
  for (i = 0; i < sum->n; i++) {
    fc_assert(sum->sums != NULL);
    if ((strcmp(sum->sums[i].posdesc, posdesc) == 0)
        && (strcmp(sum->sums[i].negdesc, negdesc) == 0)
        && ((sum->sums[i].auxfmt == auxfmt)
            || (auxfmt != NULL && !strcmp(sum->sums[i].auxfmt, auxfmt)))
        && sum->sums[i].suppress_if_zero == suppress_if_zero) {
      /* Looks like we already have an entry like this. Accumulate values. */
      sum->sums[i].value += value;
      sum->sums[i].aux += aux;
      FC_FREE(posdesc);
      FC_FREE(negdesc);
      return;
    }
  }

  /* Didn't find description already, so add it to the end. */
  sum->sums = fc_realloc(sum->sums, (sum->n + 1) * sizeof(sum->sums[0]));
  sum->sums[sum->n].value   = value;
  sum->sums[sum->n].posdesc = posdesc;
  sum->sums[sum->n].negdesc = negdesc;
  sum->sums[sum->n].suppress_if_zero = suppress_if_zero;
  sum->sums[sum->n].aux     = aux;
  sum->sums[sum->n].auxfmt  = auxfmt;
  sum->n++;
}

/**********************************************************************//**
  Add a new contribution to the city_sum (complex).
   - Allows different descriptions for net positive and negative
     contributions (posfmt/negfmt);
   - Allows specifying another auxiliary number which isn't significant
     for comparisons but will also be accumulated, and appended to the
     description when rendered (e.g. the 50% in "+11: Bonus from
     Marketplace+Luxury (+50%)")
   - Allows control over whether the item will be discarded if its
     net value is zero (suppress_if_zero).
**************************************************************************/
static void
            fc__attribute((__format__(__printf__, 6, 8)))
            fc__attribute((__format__(__printf__, 7, 8)))
            fc__attribute((nonnull (1, 6, 7)))
            city_sum_add_full(struct city_sum *sum, double value,
                              bool suppress_if_zero,
                              const char *auxfmt,
                              double aux,
                              const char *posfmt,
                              const char *negfmt,
                              ...)
{
  va_list args;
  struct astring astr = ASTRING_INIT;
  char *posdesc, *negdesc;

  /* Format both descriptions */
  va_start(args, negfmt);  /* sic -- arguments follow negfmt */
  astr_vadd(&astr, posfmt, args);
  posdesc = astr_to_str(&astr);
  va_end(args);
  va_start(args, negfmt);
  astr_vadd(&astr, negfmt, args);
  negdesc = astr_to_str(&astr);
  va_end(args);

  city_sum_add_real(sum, value, suppress_if_zero, auxfmt, aux,
                    posdesc, negdesc);
}

/**********************************************************************//**
  Add a new contribution to the city_sum (simple).
  Compared to city_sum_add_full():
   - description does not depend on net value
   - not suppressed if net value is zero (compare city_sum_add_nonzero())
   - no auxiliary number
**************************************************************************/
static void
            fc__attribute((__format__(__printf__, 3, 4)))
            fc__attribute((nonnull (1, 3)))
            city_sum_add(struct city_sum *sum, double value,
                         const char *descfmt, ...)
{
  va_list args;
  struct astring astr = ASTRING_INIT;
  char *desc;

  /* Format description (same used for positive or negative net value) */
  va_start(args, descfmt);
  astr_vadd(&astr, descfmt, args);
  desc = astr_to_str(&astr);
  va_end(args);

  /* Descriptions will be freed individually, so need to fc_strdup() */
  city_sum_add_real(sum, value, FALSE, NULL, 0, desc, fc_strdup(desc));
}

/**********************************************************************//**
  Add a new contribution to the city_sum (simple).
  Compared to city_sum_add_full():
   - description does not depend on net value
   - suppressed if net value is zero (compare city_sum_add())
   - no auxiliary number
**************************************************************************/
static void
            fc__attribute((__format__(__printf__, 3, 4)))
            fc__attribute((nonnull (1, 3)))
            city_sum_add_if_nonzero(struct city_sum *sum, double value,
                                    const char *descfmt, ...)
{
  va_list args;
  struct astring astr = ASTRING_INIT;
  char *desc;

  /* Format description (same used for positive or negative net value) */
  va_start(args, descfmt);
  astr_vadd(&astr, descfmt, args);
  desc = astr_to_str(&astr);
  va_end(args);

  /* Descriptions will be freed individually, so need to fc_strdup() */
  city_sum_add_real(sum, value, TRUE, NULL, 0, desc, fc_strdup(desc));
}

/**********************************************************************//**
  Return the net total accumulated in the sum so far.
**************************************************************************/
static double city_sum_total(struct city_sum *sum)
{
  size_t i;
  double total = 0;

  for (i = 0; i < sum->n; i++) {
    total += sum->sums[i].value;
  }

  return total;
}

/**********************************************************************//**
  Compare two values, taking care of floating point comparison issues.
    -1: val1 <  val2
     0: val1 == val2 (approximately)
    +1: val1 >  val2
**************************************************************************/
static inline int city_sum_compare(double val1, double val2)
{
  /* Fudgey epsilon -- probably the numbers we're dealing with have at
   * most 1% or 0.1% real difference */
  if (fabs(val1-val2) < 0.0000001) {
    return 0;
  }

  return (val1 > val2 ? +1 : -1);
}

/**********************************************************************//**
  Print out the sum, including total, and free the city_sum.
  totalfmt's first format string must be some kind of %f, and first
  argument must be a double (if account_for_unknown).
  account_for_unknown is optional, as not every sum wants it (consider
  pollution's clipping).
**************************************************************************/
static void
            fc__attribute((__format__(__printf__, 5, 6)))
            fc__attribute((nonnull (1, 2, 5)))
            city_sum_print(struct city_sum *sum, char *buf, size_t bufsz,
                           bool account_for_unknown,
                           const char *totalfmt, ...)
{
  va_list args;
  size_t i;

  /* This probably ought not to happen in well-designed rulesets, but it's
   * possible for incomplete client knowledge to give an inaccurate
   * breakdown. If it does happen, at least acknowledge to the user that
   * we are confused, rather than displaying an incorrect sum. */
  if (account_for_unknown) {
    double total = city_sum_total(sum);
    double actual_total;

    va_start(args, totalfmt);
    actual_total = va_arg(args, double);
    va_end(args);

    if (city_sum_compare(total, actual_total) != 0) {
      city_sum_add(sum, actual_total - total,
                   /* TRANS: Client cannot explain some aspect of city
                    * output. Should never happen. */
                   Q_("?city_sum:(unknown)"));
    }
  }

  for (i = 0; i < sum->n; i++) {
    if (!sum->sums[i].suppress_if_zero
        || city_sum_compare(sum->sums[i].value, 0) != 0) {
      cat_snprintf(buf, bufsz,
                   sum->format, sum->sums[i].value,
                   (sum->sums[i].value < 0) ? sum->sums[i].negdesc
                                            : sum->sums[i].posdesc);
      if (sum->sums[i].auxfmt) {
        cat_snprintf(buf, bufsz, sum->sums[i].auxfmt, sum->sums[i].aux);
      }
      cat_snprintf(buf, bufsz, "\n");
    }
    FC_FREE(sum->sums[i].posdesc);
    FC_FREE(sum->sums[i].negdesc);
  }

  va_start(args, totalfmt);
  fc_vsnprintf(buf + strlen(buf), bufsz - strlen(buf), totalfmt, args);
  va_end(args);

  FC_FREE(sum->sums);
  FC_FREE(sum);
}

/**********************************************************************//**
  Return text describing the production output.
**************************************************************************/
void get_city_dialog_output_text(const struct city *pcity,
                                 Output_type_id otype,
                                 char *buf, size_t bufsz)
{
  int priority;
  int tax[O_LAST];
  struct output_type *output = &output_types[otype];
  /* TRANS: format string for a row of the city output sum that adds up
   * to "Total surplus" */
  struct city_sum *sum = city_sum_new(Q_("?city_surplus:%+4.0f : %s"));

  buf[0] = '\0';

  city_sum_add(sum, pcity->citizen_base[otype],
               Q_("?city_surplus:Citizens"));

  /* Hack to get around the ugliness of add_tax_income(). */
  memset(tax, 0, O_LAST * sizeof(*tax));
  add_tax_income(city_owner(pcity), pcity->prod[O_TRADE], tax);
  city_sum_add_if_nonzero(sum, tax[otype],
                          Q_("?city_surplus:Taxed from trade"));

  /* Special cases for "bonus" production. See set_city_production() in
   * city.c. */
  if (otype == O_TRADE) {
    trade_routes_iterate(pcity, proute) {
      /* NB: (proute->value == 0) is valid case. The trade route
       * is established but doesn't give trade surplus. */
      struct city *trade_city = game_city_by_number(proute->partner);
      /* TRANS: Trade partner unknown to client */
      const char *name
        = trade_city ? city_name_get(trade_city) : _("(unknown)");
      int value = proute->value
        * (100 + get_city_bonus(pcity, EFT_TRADE_ROUTE_PCT)) / 100;

      switch (proute->dir) {
      case RDIR_BIDIRECTIONAL:
        city_sum_add(sum, value, Q_("?city_surplus:Trading %s with %s"),
                     goods_name_translation(proute->goods),
                     name);
        break;
      case RDIR_FROM:
        city_sum_add(sum, value, Q_("?city_surplus:Trading %s to %s"),
                     goods_name_translation(proute->goods),
                     name);
        break;
      case RDIR_TO:
        city_sum_add(sum, value, Q_("?city_surplus:Trading %s from %s"),
                     goods_name_translation(proute->goods),
                     name);
        break;
      case RDIR_NONE:
        fc_assert(proute->dir != RDIR_NONE);
        break;
      }
    } trade_routes_iterate_end;
  } else if (otype == O_GOLD) {
    int tithes = get_city_tithes_bonus(pcity);

    city_sum_add_if_nonzero(sum, tithes,
                            Q_("?city_surplus:Building tithes"));
  }

  for (priority = 0; priority < 3; priority++) {
    enum effect_type eft[] = {EFT_OUTPUT_BONUS, EFT_OUTPUT_BONUS_2,
                              EFT_OUTPUT_BONUS_ABSOLUTE};

    {
      int base = city_sum_total(sum), bonus = 100;
      struct effect_list *plist = effect_list_new();

      (void) get_city_bonus_effects(plist, pcity, output, eft[priority]);

      effect_list_iterate(plist, peffect) {
        char buf2[512];
        int delta;
        int new_total;

        get_effect_req_text(peffect, buf2, sizeof(buf2));

        if (peffect->multiplier) {
          int mul = player_multiplier_effect_value(city_owner(pcity),
                                                   peffect->multiplier);

          if (mul == 0) {
            /* Suppress text when multiplier setting suppresses effect
             * (this will also suppress it when the city owner's policy
             * settings are not known to us) */
            continue;
          }
          delta = (peffect->value * mul) / 100;
        } else {
          delta = peffect->value;
        }

        if (eft[priority] == EFT_OUTPUT_BONUS_ABSOLUTE) {
          new_total = bonus * base / 100 + delta;
          city_sum_add_full(sum, new_total - city_sum_total(sum), TRUE,
                            NULL, 0,
                            Q_("?city_surplus:Bonus from %s"),
                            Q_("?city_surplus:Loss from %s"), buf2);
        } else {
          bonus += delta;
          new_total = bonus * base / 100;
          city_sum_add_full(sum, new_total - city_sum_total(sum), TRUE,
                            /* TRANS: percentage city output bonus/loss from
                             * some source; preserve leading space */
                            Q_("?city_surplus: (%+.0f%%)"), delta,
                            Q_("?city_surplus:Bonus from %s"),
                            Q_("?city_surplus:Loss from %s"), buf2);
        }
      } effect_list_iterate_end;
      effect_list_destroy(plist);
    }
  }

  if (pcity->waste[otype] != 0) {
    int wastetypes[OLOSS_LAST];
    bool breakdown_ok;
    int regular_waste;
    /* FIXME: this will give the wrong answer in rulesets with waste on
     * taxed outputs, such as 'science waste', as our total so far includes
     * contributions taxed from trade, whereas the equivalent bit in
     * set_city_production() does not */
    if (city_waste(pcity, otype, city_sum_total(sum), wastetypes)
        == pcity->waste[otype]) {
      /* Our calculation matches the server's, so we trust our breakdown. */
      city_sum_add_if_nonzero(sum, -wastetypes[OLOSS_SIZE],
                              Q_("?city_surplus:Size penalty"));
      regular_waste = wastetypes[OLOSS_WASTE];
      breakdown_ok = TRUE;
    } else {
      /* Our calculation doesn't match what the server sent. Account it all
       * to corruption/waste. */
      regular_waste = pcity->waste[otype];
      breakdown_ok = FALSE;
    }
    if (regular_waste > 0) {
      const char *fmt;
      switch (otype) {
        case O_SHIELD:
        default: /* FIXME other output types? */
          /* TRANS: %s is normally empty, but becomes '?' if client is
           * uncertain about its accounting (should never happen) */
          fmt = Q_("?city_surplus:Waste%s");
          break;
        case O_TRADE:
          /* TRANS: %s is normally empty, but becomes '?' if client is
           * uncertain about its accounting (should never happen) */
          fmt = Q_("?city_surplus:Corruption%s");
          break;
      }
      city_sum_add(sum, -regular_waste, fmt, breakdown_ok ? "" : "?");
    }
  }

  city_sum_add_if_nonzero(sum, -pcity->unhappy_penalty[otype],
                          Q_("?city_surplus:Disorder"));

  if (pcity->usage[otype] > 0) {
    city_sum_add(sum, -pcity->usage[otype],
                 Q_("?city_surplus:Used"));
  }

  city_sum_print(sum, buf, bufsz, TRUE,
                 Q_("?city_surplus:"
                    "==== : Adds up to\n"
                    "%4.0f : Total surplus"), (double) pcity->surplus[otype]);
}

/**********************************************************************//**
  Return text describing the chance for a plague.
**************************************************************************/
void get_city_dialog_illness_text(const struct city *pcity,
                                  char *buf, size_t bufsz)
{
  int illness, ill_base, ill_size, ill_trade, ill_pollution;
  struct effect_list *plist;
  struct city_sum *sum;

  buf[0] = '\0';

  if (!game.info.illness_on) {
    cat_snprintf(buf, bufsz, _("Illness deactivated in ruleset."));
    return;
  }

  sum = city_sum_new(Q_("?city_plague:%+5.1f%% : %s"));

  illness = city_illness_calc(pcity, &ill_base, &ill_size, &ill_trade,
                              &ill_pollution);

  city_sum_add(sum, (float)(ill_size) / 10.0,
               Q_("?city_plague:Risk from overcrowding"));
  city_sum_add(sum, (float)(ill_trade) / 10.0,
               Q_("?city_plague:Risk from trade"));
  city_sum_add(sum, (float)(ill_pollution) / 10.0,
               Q_("?city_plague:Risk from pollution"));

  plist = effect_list_new();

  (void) get_city_bonus_effects(plist, pcity, NULL, EFT_HEALTH_PCT);

  effect_list_iterate(plist, peffect) {
    char buf2[512];
    int delta;

    get_effect_req_text(peffect, buf2, sizeof(buf2));

    if (peffect->multiplier) {
      int mul = player_multiplier_effect_value(city_owner(pcity),
                                               peffect->multiplier);

      if (mul == 0) {
        /* Suppress text when multiplier setting suppresses effect
         * (this will also suppress it when the city owner's policy
         * settings are not known to us) */
        continue;
      }
      delta = (peffect->value * mul) / 100;
    } else {
      delta = peffect->value;
    }

    city_sum_add_full(sum, -(0.1 * ill_base * delta / 100), TRUE,
                      Q_("?city_plague: (%+.0f%%)"), -delta,
                      Q_("?city_plague:Risk from %s"),
                      Q_("?city_plague:Bonus from %s"), buf2);
  } effect_list_iterate_end;
  effect_list_destroy(plist);

  /* XXX: account_for_unknown==FALSE: the displayed sum can fail to
   * add up due to rounding. Making it always add up probably requires
   * arbitrary assignment of 0.1% rounding figures to particular
   * effects with something like distribute(). */
  city_sum_print(sum, buf, bufsz, FALSE,
                 Q_("?city_plague:"
                    "====== : Adds up to\n"
                    "%5.1f%% : Plague chance per turn"),
                 ((double)(illness) / 10.0));
}

/**********************************************************************//**
  Return text describing the pollution output.
**************************************************************************/
void get_city_dialog_pollution_text(const struct city *pcity,
                                    char *buf, size_t bufsz)
{
  int pollu, prod, pop, mod;
  struct city_sum *sum = city_sum_new(Q_("?city_pollution:%+4.0f : %s"));

  /* On the server, pollution is calculated before production is deducted
   * for disorder; we need to compensate for that */
  pollu = city_pollution_types(pcity,
                               pcity->prod[O_SHIELD]
                               + pcity->unhappy_penalty[O_SHIELD],
                               &prod, &pop, &mod);
  buf[0] = '\0';

  city_sum_add(sum, prod, Q_("?city_pollution:Pollution from shields"));
  city_sum_add(sum, pop,  Q_("?city_pollution:Pollution from citizens"));
  city_sum_add(sum, mod,  Q_("?city_pollution:Pollution modifier"));
  city_sum_print(sum, buf, bufsz, FALSE,
                 Q_("?city_pollution:"
                    "==== : Adds up to\n"
                    "%4.0f : Total surplus"), (double)pollu);
}

/**********************************************************************//**
  Return text describing the culture output.
**************************************************************************/
void get_city_dialog_culture_text(const struct city *pcity,
                                  char *buf, size_t bufsz)
{
  struct effect_list *plist;
  struct city_sum *sum = city_sum_new(Q_("?city_culture:%4.0f : %s"));

  buf[0] = '\0';

  /* XXX: no way to check whether client's idea of gain/turn is accurate */
  city_sum_add(sum, pcity->history, Q_("?city_culture:History (%+d/turn)"),
               city_history_gain(pcity));

  plist = effect_list_new();

  (void) get_city_bonus_effects(plist, pcity, NULL, EFT_PERFORMANCE);

  effect_list_iterate(plist, peffect) {
    char buf2[512];
    int mul = 100;
    int value;

    get_effect_req_text(peffect, buf2, sizeof(buf2));

    if (peffect->multiplier) {
      mul = player_multiplier_effect_value(city_owner(pcity),
                                           peffect->multiplier);

      if (mul == 0) {
        /* Suppress text when multiplier setting suppresses effect
         * (this will also suppress it when the city owner's policy
         * settings are not known to us) */
        continue;
      }
    }

    value = (peffect->value * mul) / 100;
    /* TRANS: Text describing source of culture bonus ("Library+Republic") */
    city_sum_add_if_nonzero(sum, value, Q_("?city_culture:%s"), buf2);
  } effect_list_iterate_end;
  effect_list_destroy(plist);

  city_sum_print(sum, buf, bufsz, TRUE,
                 Q_("?city_culture:"
                    "==== : Adds up to\n"
                    "%4.0f : Total culture"), (double)pcity->client.culture);
}

/**********************************************************************//**
  Return text describing airlift capacity.
**************************************************************************/
void get_city_dialog_airlift_text(const struct city *pcity,
                                  char *buf, size_t bufsz)
{
  char src[512];
  char dest[512];
  int unlimited = 0;

  if ((game.info.airlifting_style & AIRLIFTING_UNLIMITED_SRC)
      && (pcity->airlift >= 1 || game.info.airlift_from_always_enabled)) {

    unlimited++;

    /* TRANS: airlift. Possible take offs text. String is a
     * proviso that take offs can't occur if landings spend all the
     * remaining airlift when landings are limited and empty when they
     * aren't limited. */
    fc_snprintf(src, sizeof(src), _("unlimited take offs%s"),
                game.info.airlifting_style & AIRLIFTING_UNLIMITED_DEST
                /* TRANS: Airlift unlimited take offs proviso used above.
                 * Plural based on remaining airlift capacity. */
                ? "" : PL_(" (until the landing has been spent)",
                           " (until all landings have been spent)",
                           pcity->airlift));
  } else {
    fc_snprintf(src, sizeof(src),
                /* TRANS: Airlift. Possible take offs text. Number is
                 * airlift capacity. */
                PL_("%d take off", "%d take offs", pcity->airlift),
                pcity->airlift);
  }

  if ((game.info.airlifting_style & AIRLIFTING_UNLIMITED_DEST)
      && (pcity->airlift >= 1 || game.info.airlift_to_always_enabled)){

    unlimited++;

    /* TRANS: Airlift. Possible landings text. */
    fc_snprintf(dest, sizeof(dest), _("unlimited landings"));
  } else {
    fc_snprintf(dest, sizeof(dest),
                /* TRANS: Airlift. Possible landings text.
                 * Number is airlift capacity. */
                PL_("%d landing", "%d landings", pcity->airlift),
                pcity->airlift);
  }

  switch (unlimited) {
  case 2:
    /* TRANS: Airlift take offs and landings */
    fc_snprintf(buf, bufsz, _("unlimited take offs and landings"));
    break;
  case 1:
    /* TRANS: Airlift take offs and landings. One is unlimited. The first
     * string is the take offs text. The 2nd string is the landings text. */
    fc_snprintf(buf, bufsz, _("%s and %s"), src, dest);
    break;
  default:
    fc_snprintf(buf, bufsz,
                /* TRANS: Airlift take offs or landings, no unlimited.
                 * Number is airlift capacity. */
                PL_("%d take off or landing", "%d take offs or landings",
                    pcity->airlift),
                pcity->airlift);
    break;
  }
}

/**********************************************************************//**
  Return airlift capacity.
**************************************************************************/
void get_city_dialog_airlift_value(const struct city *pcity,
                                   char *buf, size_t bufsz)
{
  char src[512];
  char dest[512];
  int unlimited = 0;

  if ((game.info.airlifting_style & AIRLIFTING_UNLIMITED_SRC)
      && (pcity->airlift >= 1 || game.info.airlift_from_always_enabled)) {

    unlimited++;

    /* TRANS: Airlift. Possible take offs text. String is a symbol that
     * indicates that terms and conditions apply when landings are limited
     * and empty when they aren't limited. */
    fc_snprintf(src, sizeof(src), _("   ∞%s"),
                game.info.airlifting_style & AIRLIFTING_UNLIMITED_DEST
                /* TRANS: airlift unlimited take offs may be spent symbol
                 * used above. */
                ? "" : Q_("?landings:*"));
  } else {
    /* TRANS: Airlift. Possible take offs text. Number is
     * airlift capacity. */
    fc_snprintf(src, sizeof(src), Q_("?takeoffs:%4d"), pcity->airlift);
  }

  if ((game.info.airlifting_style & AIRLIFTING_UNLIMITED_DEST)
      && (pcity->airlift >= 1 || game.info.airlift_to_always_enabled)){

    unlimited++;

    /* TRANS: Airlift. Possible landings text. */
    fc_snprintf(dest, sizeof(dest), _("   ∞"));
  } else {
    /* TRANS: Airlift. Possible landings text. */
    fc_snprintf(dest, sizeof(dest), Q_("?landings:%4d"), pcity->airlift);
  }

  switch (unlimited) {
  case 2:
    /* TRANS: Unlimited airlift take offs and landings */
    fc_snprintf(buf, bufsz, _("   ∞"));
    break;
  case 1:
    /* TRANS: Airlift take offs and landings. One is unlimited. The first
     * string is the take offs text. The 2nd string is the landings text.
     * For English, initials of d)epartures and a)rrivals were chosen. */
    fc_snprintf(buf, bufsz, _("d: %s a: %s"), src, dest);
    break;
  default:
    /* TRANS: Airlift take offs or landings, no unlimited */
    fc_snprintf(buf, bufsz, Q_("?airlifts:%s"), src);
    break;
  }
}

/**********************************************************************//**
  Provide a list of all citizens in the city, in order. "idx"
  should be the happiness index from FEELING_BASE to FEELING_FINAL.
  "categories" should be an array large enough to hold all citizens
  (use MAX_CITY_SIZE to be on the safe side).
  Note that superspecialists are not included. There can be theoretically
  times more superspecialists working than MAX_CITY_SIZE.
  Return number of categories filled (presumed equal to city size).
**************************************************************************/
int get_city_citizen_types(struct city *pcity, enum citizen_feeling idx,
                           enum citizen_category *categories)
{
  int i = 0, n;

  fc_assert(idx >= 0 && idx < FEELING_LAST);

  for (n = 0; n < pcity->feel[CITIZEN_HAPPY][idx]; n++, i++) {
    categories[i] = CITIZEN_HAPPY;
  }
  for (n = 0; n < pcity->feel[CITIZEN_CONTENT][idx]; n++, i++) {
    categories[i] = CITIZEN_CONTENT;
  }
  for (n = 0; n < pcity->feel[CITIZEN_UNHAPPY][idx]; n++, i++) {
    categories[i] = CITIZEN_UNHAPPY;
  }
  for (n = 0; n < pcity->feel[CITIZEN_ANGRY][idx]; n++, i++) {
    categories[i] = CITIZEN_ANGRY;
  }

  normal_specialist_type_iterate(sp) {
    for (n = 0; n < pcity->specialists[sp]; n++, i++) {
      categories[i] = CITIZEN_SPECIALIST + sp;
    }
  } normal_specialist_type_iterate_end;

  if (city_size_get(pcity) != i) {
    log_error("get_city_citizen_types() %d citizens "
              "not equal %d city size in \"%s\".",
              i, city_size_get(pcity), city_name_get(pcity));
  }

  return i;
}

/**********************************************************************//**
  Try to fill city superspecialists into categories[cat_len] array.
  If succeeds, return how many are filled.
  If there are too many of them, fill as many as fits
  and return negative value.
**************************************************************************/
int city_try_fill_superspecialists(struct city *pcity, int cat_len,
                                   enum citizen_category *categories)
{
  int i = 0, n;

  super_specialist_type_iterate(sp) {
    for (n = 0; n < pcity->specialists[sp]; n++, i++) {
      if (i >= cat_len) {
        return -1;
      }
      categories[i] = CITIZEN_SPECIALIST + sp;
    }
  } super_specialist_type_iterate_end;

  return i;
}

/**********************************************************************//**
  Rotate the given specialist citizen to the next type of citizen.
**************************************************************************/
void city_rotate_specialist(struct city *pcity, int citizen_index)
{
  enum citizen_category categories[MAX_CITY_SIZE];
  Specialist_type_id from, to;
  int num_citizens = get_city_citizen_types(pcity, FEELING_FINAL, categories);

  if (citizen_index < 0 || citizen_index >= num_citizens
      || categories[citizen_index] < CITIZEN_SPECIALIST) {
    return;
  }
  from = categories[citizen_index] - CITIZEN_SPECIALIST;

  /* Loop through all specialists in order until we find a usable one
   * (or run out of choices). */
  to = from;
  fc_assert(is_normal_specialist_id(to));
  do {
    to = (to + 1) % normal_specialist_count();
  } while (to != from && !city_can_use_specialist(pcity, to));

  if (from != to) {
    city_change_specialist(pcity, from, to);
  }
}

/**********************************************************************//**
  Activate all units on the given map tile.
**************************************************************************/
void activate_all_units(struct tile *ptile)
{
  struct unit_list *punit_list = ptile->units;
  struct unit *pmyunit = NULL;

  unit_list_iterate(punit_list, punit) {
    if (unit_owner(punit) == client.conn.playing) {
      /* Activate this unit. */
      pmyunit = punit;
      request_new_unit_activity(punit, ACTIVITY_IDLE);
    }
  } unit_list_iterate_end;
  if (pmyunit) {
    /* Put the focus on one of the activated units. */
    unit_focus_set(pmyunit);
  }
}

/**********************************************************************//**
  Change the production of a given city. Return the request ID.
**************************************************************************/
int city_change_production(struct city *pcity, struct universal *target)
{
  return dsend_packet_city_change(&client.conn, pcity->id,
                                  target->kind,
                                  universal_number(target));
}

/**********************************************************************//**
  Set the worklist for a given city. Return the request ID.

  Note that the worklist does NOT include the current production.
**************************************************************************/
int city_set_worklist(struct city *pcity, const struct worklist *pworklist)
{
  return dsend_packet_city_worklist(&client.conn, pcity->id, pworklist);
}

/**********************************************************************//**
  Commit the changes to the worklist for the city.
**************************************************************************/
void city_worklist_commit(struct city *pcity, struct worklist *pwl)
{
  int k;

  /* Update the worklist. Remember, though -- the current build
     target really isn't in the worklist; don't send it to the server
     as part of the worklist. Of course, we have to search through
     the current worklist to find the first _now_available_ build
     target (to cope with players who try mean things like adding a
     Battleship to a city worklist when the player doesn't even yet
     have the Map Making tech). */

  for (k = 0; k < MAX_LEN_WORKLIST; k++) {
    int same_as_current_build;
    struct universal target;

    if (!worklist_peek_ith(pwl, &target, k)) {
      break;
    }

    same_as_current_build = are_universals_equal(&pcity->production, &target);

    /* Very special case: If we are currently building a wonder we
       allow the construction to continue, even if we the wonder is
       finished elsewhere, i.e., unbuildable. */
    if (k == 0
        && VUT_IMPROVEMENT == target.kind
        && is_wonder(target.value.building)
        && same_as_current_build) {
      worklist_remove(pwl, k);
      break;
    }

    /* If it can be built... */
    if (can_city_build_now(&(wld.map), pcity, &target)) {
      /* ...but we're not yet building it, then switch. */
      if (!same_as_current_build) {
        /* Change the current target */
        city_change_production(pcity, &target);
      }

      /* This item is now (and may have always been) the current
         build target. Drop it out of the worklist. */
      worklist_remove(pwl, k);
      break;
    }
  }

  /* Send the rest of the worklist on its way. */
  city_set_worklist(pcity, pwl);
}

/**********************************************************************//**
  Insert an item into the city's queue. This function will send new
  production requests to the server but will NOT send the new worklist
  to the server - the caller should call city_set_worklist() if the
  function returns TRUE.

  Note that the queue DOES include the current production.
**************************************************************************/
static bool base_city_queue_insert(struct city *pcity, int position,
                                   struct universal *item)
{
  const struct civ_map *nmap = &(wld.map);

  if (position == 0) {
    struct universal old = pcity->production;

    /* Insert as current production. */
    if (!can_city_build_direct(nmap, pcity, item)) {
      return FALSE;
    }

    if (!worklist_insert(&pcity->worklist, &old, 0)) {
      return FALSE;
    }

    city_change_production(pcity, item);
  } else if (position >= 1
             && position <= worklist_length(&pcity->worklist)) {
    /* Insert into middle. */
    if (!can_city_build_later(nmap, pcity, item)) {
      return FALSE;
    }
    if (!worklist_insert(&pcity->worklist, item, position - 1)) {
      return FALSE;
    }
  } else {
    /* Insert at end. */
    if (!can_city_build_later(nmap, pcity, item)) {
      return FALSE;
    }
    if (!worklist_append(&pcity->worklist, item)) {
      return FALSE;
    }
  }
  return TRUE;
}

/**********************************************************************//**
  Insert an item into the city's queue.

  Note that the queue DOES include the current production.
**************************************************************************/
bool city_queue_insert(struct city *pcity, int position,
                       struct universal *item)
{
  if (base_city_queue_insert(pcity, position, item)) {
    city_set_worklist(pcity, &pcity->worklist);
    return TRUE;
  }
  return FALSE;
}

/**********************************************************************//**
  Clear the queue (all entries except the first one since that can't be
  cleared).

  Note that the queue DOES include the current production.
**************************************************************************/
bool city_queue_clear(struct city *pcity)
{
  worklist_init(&pcity->worklist);

  return TRUE;
}

/**********************************************************************//**
  Insert the worklist into the city's queue at the given position.

  Note that the queue DOES include the current production.
**************************************************************************/
bool city_queue_insert_worklist(struct city *pcity, int position,
                                const struct worklist *worklist)
{
  bool success = FALSE;

  if (worklist_length(worklist) == 0) {
    return TRUE;
  }

  worklist_iterate(worklist, target) {
    if (base_city_queue_insert(pcity, position, &target)) {
      if (position > 0) {
        /* Move to the next position (unless position == -1 in which case
         * we're appending. */
        position++;
      }
      success = TRUE;
    }
  } worklist_iterate_end;

  if (success) {
    city_set_worklist(pcity, &pcity->worklist);
  }

  return success;
}

/**********************************************************************//**
  Get the city current production and the worklist, like it should be.
**************************************************************************/
void city_get_queue(struct city *pcity, struct worklist *pqueue)
{
  worklist_copy(pqueue, &pcity->worklist);

  /* The GUI wants current production to be in the task list, but the
     worklist API wants it out for reasons unknown. Perhaps someone enjoyed
     making things more complicated than necessary? So I dance around it. */

  /* We want the current production to be in the queue. Always. */
  worklist_remove(pqueue, MAX_LEN_WORKLIST - 1);

  worklist_insert(pqueue, &pcity->production, 0);
}

/**********************************************************************//**
  Set the city current production and the worklist, like it should be.
**************************************************************************/
bool city_set_queue(struct city *pcity, const struct worklist *pqueue)
{
  struct worklist copy;
  struct universal target;

  worklist_copy(&copy, pqueue);

  /* The GUI wants current production to be in the task list, but the
     worklist API wants it out for reasons unknown. Perhaps someone enjoyed
     making things more complicated than necessary? So I dance around it. */
  if (worklist_peek(&copy, &target)) {

    if (!city_can_change_build(pcity)
        && !are_universals_equal(&pcity->production, &target)) {
      /* We cannot change production to one from worklist.
       * Do not replace old worklist with new one. */
      return FALSE;
    }

    worklist_advance(&copy);

    city_set_worklist(pcity, &copy);
    city_change_production(pcity, &target);
  } else {
    /* You naughty boy, you can't erase the current production. Nyah! */
    if (worklist_is_empty(&pcity->worklist)) {
      refresh_city_dialog(pcity);
    } else {
      city_set_worklist(pcity, &copy);
    }
  }

  return TRUE;
}

/**********************************************************************//**
  Return TRUE iff the city can buy.
**************************************************************************/
bool city_can_buy(const struct city *pcity)
{
  /* See really_handle_city_buy() in the server. However this function
   * doesn't allow for error messages. It doesn't check the cost of
   * buying; that's handled separately (and with an error message). */
  return (can_client_issue_orders()
          && NULL != pcity
          && city_owner(pcity) == client.conn.playing
          && pcity->turn_founded != game.info.turn
          && !pcity->did_buy
          && (VUT_UTYPE == pcity->production.kind
              || !is_convert_improvement(pcity->production.value.building))
          && !(VUT_UTYPE == pcity->production.kind
               && pcity->anarchy != 0)
          && pcity->client.buy_cost > 0);
}

/**********************************************************************//**
  Change the production of a given city. Return the request ID.
**************************************************************************/
int city_sell_improvement(struct city *pcity, Impr_type_id sell_id)
{
  return dsend_packet_city_sell(&client.conn, pcity->id, sell_id);
}

/**********************************************************************//**
  Buy the current production item in a given city. Return the request ID.
**************************************************************************/
int city_buy_production(struct city *pcity)
{
  return dsend_packet_city_buy(&client.conn, pcity->id);
}

/**********************************************************************//**
  Change a specialist in the given city. Return the request ID.
**************************************************************************/
int city_change_specialist(struct city *pcity, Specialist_type_id from,
                           Specialist_type_id to)
{
  return dsend_packet_city_change_specialist(&client.conn, pcity->id, from,
                                             to);
}

/**********************************************************************//**
  Toggle a worker<->specialist at the given city tile. Return the
  request ID.
**************************************************************************/
int city_toggle_worker(struct city *pcity, int city_x, int city_y)
{
  int city_radius_sq;
  struct tile *ptile;

  if (city_owner(pcity) != client_player()) {
    return 0;
  }

  city_radius_sq = city_map_radius_sq_get(pcity);
  fc_assert(is_valid_city_coords(city_radius_sq, city_x, city_y));
  ptile = city_map_to_tile(&(wld.map), city_tile(pcity), city_radius_sq,
                           city_x, city_y);
  if (NULL == ptile) {
    return 0;
  }

  if (NULL != tile_worked(ptile) && tile_worked(ptile) == pcity) {
    return dsend_packet_city_make_specialist(&client.conn,
                                             pcity->id, ptile->index);
  } else if (city_can_work_tile(pcity, ptile)) {
    return dsend_packet_city_make_worker(&client.conn,
                                         pcity->id, ptile->index);
  } else {
    return 0;
  }
}

/**********************************************************************//**
  Tell the server to rename the city. Return the request ID.
**************************************************************************/
int city_rename(struct city *pcity, const char *name)
{
  return dsend_packet_city_rename(&client.conn, pcity->id, name);
}
