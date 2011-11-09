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
#include <fc_config.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "registry.h"
#include "shared.h"
#include "string_vector.h"
#include "support.h"

/* common */
#include "ai.h"
#include "base.h"
#include "capability.h"
#include "city.h"
#include "effects.h"
#include "fc_types.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "movement.h"
#include "name_translation.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "requirements.h"
#include "rgbcolor.h"
#include "road.h"
#include "specialist.h"
#include "tech.h"
#include "unit.h"
#include "unittype.h"

/* server */
#include "citytools.h"
#include "plrhand.h"
#include "settings.h"
#include "srv_main.h"

/* server/advisors */
#include "advruleset.h"

/* server/scripting */
#include "script_server.h"

#include "ruleset.h"


#define RULESET_CAPABILITIES "+Freeciv-ruleset-Devel-2011.Nov.02"
/*
 * Ruleset capabilities acceptable to this program:
 *
 * +Freeciv-2.3-ruleset
 *    - basic ruleset format for Freeciv versions 2.3.x; required
 *
 * +Freeciv-tilespec-Devel-YYYY.MMM.DD
 *    - ruleset of the development version at the given data
 */

/* RULESET_SUFFIX already used, no leading dot here */
#define RULES_SUFFIX "ruleset"
#define SCRIPT_SUFFIX "lua"

#define ADVANCE_SECTION_PREFIX "advance_"
#define BUILDING_SECTION_PREFIX "building_"
#define CITYSTYLE_SECTION_PREFIX "citystyle_"
#define EFFECT_SECTION_PREFIX "effect_"
#define GOVERNMENT_SECTION_PREFIX "government_"
#define NATION_GROUP_SECTION_PREFIX "ngroup" /* without underscore? */
#define NATION_SECTION_PREFIX "nation" /* without underscore? */
#define RESOURCE_SECTION_PREFIX "resource_"
#define BASE_SECTION_PREFIX "base_"
#define ROAD_SECTION_PREFIX "road_"
#define SPECIALIST_SECTION_PREFIX "specialist_"
#define TERRAIN_SECTION_PREFIX "terrain_"
#define UNIT_CLASS_SECTION_PREFIX "unitclass_"
#define UNIT_SECTION_PREFIX "unit_"

#define check_name(name) (check_strlen(name, MAX_LEN_NAME, NULL))

/* avoid re-reading files */
static const char name_too_long[] = "Name \"%s\" too long; truncating.";
#define MAX_SECTION_LABEL 64
#define section_strlcpy(dst, src) \
	(void) loud_strlcpy(dst, src, MAX_SECTION_LABEL, name_too_long)
static char *resource_sections = NULL;
static char *terrain_sections = NULL;
static char *base_sections = NULL;
static char *road_sections = NULL;

static struct section_file *openload_ruleset_file(const char *whichset);
static const char *check_ruleset_capabilities(struct section_file *file,
                                              const char *us_capstr,
                                              const char *filename);

static void load_tech_names(struct section_file *file);
static void load_unit_names(struct section_file *file);
static void load_building_names(struct section_file *file);
static void load_government_names(struct section_file *file);
static void load_terrain_names(struct section_file *file);
static void load_citystyle_names(struct section_file *file);
static void load_nation_names(struct section_file *file);
static void load_city_name_list(struct section_file *file,
                                struct nation_type *pnation,
                                const char *secfile_str1,
                                const char *secfile_str2);

static void load_ruleset_techs(struct section_file *file);
static void load_ruleset_units(struct section_file *file);
static void load_ruleset_buildings(struct section_file *file);
static void load_ruleset_governments(struct section_file *file);
static void load_ruleset_terrain(struct section_file *file);
static void load_ruleset_cities(struct section_file *file);
static void load_ruleset_effects(struct section_file *file);

static void load_ruleset_game(void);

static void send_ruleset_techs(struct conn_list *dest);
static void send_ruleset_unit_classes(struct conn_list *dest);
static void send_ruleset_units(struct conn_list *dest);
static void send_ruleset_buildings(struct conn_list *dest);
static void send_ruleset_terrain(struct conn_list *dest);
static void send_ruleset_resources(struct conn_list *dest);
static void send_ruleset_bases(struct conn_list *dest);
static void send_ruleset_roads(struct conn_list *dest);
static void send_ruleset_governments(struct conn_list *dest);
static void send_ruleset_cities(struct conn_list *dest);
static void send_ruleset_game(struct conn_list *dest);
static void send_ruleset_team_names(struct conn_list *dest);

static bool load_ruleset_veteran(struct section_file *file,
                                 const char *path,
                                 struct veteran_system **vsystem, char *err,
                                 size_t err_len);

static bool nation_has_initial_tech(struct nation_type *pnation,
                                    struct advance *tech);
static bool sanity_check_ruleset_data(void);
static void ruleset_error_real(const char *file, const char *function,
                               int line, enum log_level level,
                               const char *format, ...)
                               fc__attribute((__format__ (__printf__, 5, 6)));
#define ruleset_error(level, format, ...)                                   \
  if (log_do_output_for_level(level)) {                                     \
    ruleset_error_real(__FILE__, __FUNCTION__, __FC_LINE__,                 \
                       level, format, ## __VA_ARGS__);                      \
  }

/**************************************************************************
  Notifications about ruleset errors to clients. Especially important in
  case of internal server crashing.
**************************************************************************/
static void ruleset_error_real(const char *file, const char *function,
                               int line, enum log_level level,
                               const char *format, ...)
{
  va_list args;

  va_start(args, format);
  vdo_log(file, function, line, FALSE, level, format, args);
  va_end(args);

  if (LOG_FATAL >= level) {
    exit(EXIT_FAILURE);
  }
}

/**************************************************************************
  datafilename() wrapper: tries to match in two ways.
  Returns NULL on failure, the (statically allocated) filename on success.
**************************************************************************/
static const char *valid_ruleset_filename(const char *subdir,
                                          const char *name,
                                          const char *extension)
{
  char filename[512];
  const char *dfilename;

  fc_assert_ret_val(subdir && name && extension, NULL);

  fc_snprintf(filename, sizeof(filename), "%s/%s.%s",
              subdir, name, extension);
  log_verbose("Trying \"%s\".", filename);
  dfilename = fileinfoname(get_data_dirs(), filename);
  if (dfilename) {
    return dfilename;
  }

  fc_snprintf(filename, sizeof(filename), "default/%s.%s", name, extension);
  log_verbose("Trying \"%s\": default ruleset directory.", filename);
  dfilename = fileinfoname(get_data_dirs(), filename);
  if (dfilename) {
    return dfilename;
  }

  fc_snprintf(filename, sizeof(filename), "%s_%s.%s",
              subdir, name, extension);
  log_verbose("Trying \"%s\": alternative ruleset filename syntax.",
              filename);
  dfilename = fileinfoname(get_data_dirs(), filename);
  if (dfilename) {
    return dfilename;
  } else {
    ruleset_error(LOG_FATAL,
                  /* TRANS: message about an installation error. */
                  _("Could not find a readable \"%s.%s\" ruleset file."),
                  name, extension);
  }

  return(NULL);
}

/**************************************************************************
  Do initial section_file_load on a ruleset file.
  "whichset" = "techs", "units", "buildings", "terrain", ...
**************************************************************************/
static struct section_file *openload_ruleset_file(const char *whichset)
{
  char sfilename[512];
  const char *dfilename = valid_ruleset_filename(game.server.rulesetdir,
                                                 whichset, RULES_SUFFIX);
  struct section_file *secfile;

  /* Need to save a copy of the filename for following message, since
     section_file_load() may call datafilename() for includes. */

  sz_strlcpy(sfilename, dfilename);

  if (!(secfile = secfile_load(sfilename, FALSE))) {
    ruleset_error(LOG_FATAL, "Could not load ruleset '%s':\n%s",
                  sfilename, secfile_error());
  }
  return secfile;
}

/**************************************************************************
  Parse script file.
**************************************************************************/
static void openload_script_file(const char *whichset)
{
  const char *dfilename = valid_ruleset_filename(game.server.rulesetdir,
                                                 whichset, SCRIPT_SUFFIX);

  if (!script_server_do_file(NULL, dfilename)) {
    ruleset_error(LOG_FATAL, "\"%s\": could not load ruleset script.",
                  dfilename);
  }
}

/**************************************************************************
  Ruleset files should have a capabilities string datafile.options
  This gets and returns that string, and checks that the required
  capabilities specified are satisified.
**************************************************************************/
static const char *check_ruleset_capabilities(struct section_file *file,
                                              const char *us_capstr,
                                              const char *filename)
{
  const char *datafile_options;

  if (!(datafile_options = secfile_lookup_str(file, "datafile.options"))) {
    log_fatal("\"%s\": ruleset capability problem:", filename);
    ruleset_error(LOG_FATAL, "%s", secfile_error());
  }
  if (!has_capabilities(us_capstr, datafile_options)) {
    log_fatal("\"%s\": ruleset datafile appears incompatible:", filename);
    log_fatal("  datafile options: %s", datafile_options);
    log_fatal("  supported options: %s", us_capstr);
    ruleset_error(LOG_FATAL, "Capability problem");
  }
  if (!has_capabilities(datafile_options, us_capstr)) {
    log_fatal("\"%s\": ruleset datafile claims required option(s)"
              " that we don't support:", filename);
    log_fatal("  datafile options: %s", datafile_options);
    log_fatal("  supported options: %s", us_capstr);
    ruleset_error(LOG_FATAL, "Capability problem");
  }
  return datafile_options;
}

/**************************************************************************
  Load a requirement list.  The list is returned as a static vector
  (callers need not worry about freeing anything).
**************************************************************************/
static struct requirement_vector *lookup_req_list(struct section_file *file,
						  const char *sec,
						  const char *sub,
                                                  const char *rfor)
{
  const char *type, *name;
  int j;
  const char *filename;
  static struct requirement_vector list;

  filename = secfile_name(file);

  requirement_vector_reserve(&list, 0);

  for (j = 0; (type = secfile_lookup_str_default(file, NULL, "%s.%s%d.type",
                                                 sec, sub, j)); j++) {
    char buf[MAX_LEN_NAME];
    const char *range;
    bool survives, negated;
    struct entry *pentry;
    struct requirement req;

    if (!(pentry = secfile_entry_lookup(file, "%s.%s%d.name",
                                        sec, sub, j))) {
      log_error("%s", secfile_error());
      continue;
    }
    name = NULL;
    switch (entry_type(pentry)) {
    case ENTRY_BOOL:
      {
        bool val;

        if (entry_bool_get(pentry, &val)) {
          fc_snprintf(buf, sizeof(buf), "%d", val);
          name = buf;
        }
      }
      break;
    case ENTRY_INT:
      {
        int val;

        if (entry_int_get(pentry, &val)) {
          fc_snprintf(buf, sizeof(buf), "%d", val);
          name = buf;
        }
      }
      break;
    case ENTRY_STR:
      (void) entry_str_get(pentry, &name);
      break;
    }
    if (NULL == name) {
      log_error("\"%s\": error in handling requirement name for '%s.%s%d'.",
                filename, sec, sub, j);
      continue;
    }

    if (!(range = secfile_lookup_str(file, "%s.%s%d.range", sec, sub, j))) {
      log_error("%s", secfile_error());
      continue;
    }

    survives = secfile_lookup_bool_default(file, FALSE,
	"%s.%s%d.survives", sec, sub, j);
    negated = secfile_lookup_bool_default(file, FALSE,
	"%s.%s%d.negated", sec, sub, j);

    req = req_from_str(type, range, survives, negated, name);
    if (req.source.kind == universals_n_invalid()) {
      /* Error.  Log it, clear the req and continue. */
      log_error("\"%s\" [%s] has unknown req: \"%s\" \"%s\".",
                filename, sec, type, name);
      req.source.kind = VUT_NONE;
    }

    requirement_vector_append(&list, req);
  }

  if (j > MAX_NUM_REQS) {
    ruleset_error(LOG_FATAL, "Too many (%d) requirements for %s. Max is %d",
                  j, rfor, MAX_NUM_REQS);
  }

  return &list;
}

/**************************************************************************
 Lookup a string prefix.entry in the file and return the corresponding
 advances pointer.  If (!required), return A_NEVER for match "Never" or
 can't match.  If (required), die when can't match.  Note the first tech
 should have name "None" so that will always match.
 If description is not NULL, it is used in the warning message
 instead of prefix (eg pass unit->name instead of prefix="units2.u27")
**************************************************************************/
static struct advance *lookup_tech(struct section_file *file,
				   const char *prefix, const char *entry,
				   int loglevel, const char *filename,
				   const char *description)
{
  const char *sval;
  struct advance *padvance;
  
  sval = secfile_lookup_str_default(file, NULL, "%s.%s", prefix, entry);
  if (!sval || (LOG_FATAL < loglevel && strcmp(sval, "Never") == 0)) {
    padvance = A_NEVER;
  } else {
    padvance = advance_by_rule_name(sval);

    if (A_NEVER == padvance) {
      ruleset_error(loglevel,
                    "\"%s\" %s %s: couldn't match \"%s\".",
                    filename, (description ? description : prefix), entry, sval);

      /* ruleset_error returned only if error was not fatal. */
    }
  }
  return padvance;
}

/**************************************************************************
 Lookup a string prefix.entry in the file and return the corresponding
 improvement pointer.  If (!required), return B_NEVER for match "None" or
 can't match.  If (required), die when can't match.
 If description is not NULL, it is used in the warning message
 instead of prefix (eg pass unit->name instead of prefix="units2.u27")
**************************************************************************/
static struct impr_type *lookup_building(struct section_file *file,
					 const char *prefix, const char *entry,
					 int loglevel, const char *filename,
					 const char *description)
{
  const char *sval;
  struct impr_type *pimprove;
  
  sval = secfile_lookup_str_default(file, NULL, "%s.%s", prefix, entry);
  if (!sval || (LOG_FATAL < loglevel && strcmp(sval, "None") == 0)) {
    pimprove = B_NEVER;
  } else {
    pimprove = improvement_by_rule_name(sval);

    if (B_NEVER == pimprove) {
      ruleset_error(loglevel,
                    "\"%s\" %s %s: couldn't match \"%s\".",
                    filename, (description ? description : prefix), entry, sval);

      /* ruleset_error() returned only if error was not fatal */
    }
  }
  return pimprove;
}

/**************************************************************************
 Lookup a prefix.entry string vector in the file and fill in the
 array, which should hold MAX_NUM_UNIT_LIST items. The output array is
 either U_LAST terminated or full (contains MAX_NUM_UNIT_LIST
 items). If the vector is not found and the required parameter is set,
 we report it as an error, otherwise we just punt.
**************************************************************************/
static void lookup_unit_list(struct section_file *file, const char *prefix,
                             const char *entry, int loglevel,
                             struct unit_type **output, 
                             const char *filename)
{
  const char **slist;
  size_t nval;
  int i;

  /* pre-fill with NULL: */
  for(i = 0; i < MAX_NUM_UNIT_LIST; i++) {
    output[i] = NULL;
  }
  slist = secfile_lookup_str_vec(file, &nval, "%s.%s", prefix, entry);
  if (nval == 0) {
    if (LOG_FATAL >= loglevel) {
      ruleset_error(LOG_FATAL, "\"%s\": missing string vector %s.%s",
                    filename, prefix, entry);
    }
    return;
  }
  if (nval > MAX_NUM_UNIT_LIST) {
    ruleset_error(LOG_FATAL,
                  "\"%s\": string vector %s.%s too long (%d, max %d)",
                  filename, prefix, entry, (int) nval, MAX_NUM_UNIT_LIST);
  }
  if (nval == 1 && strcmp(slist[0], "") == 0) {
    free(slist);
    return;
  }
  for (i = 0; i < nval; i++) {
    const char *sval = slist[i];
    struct unit_type *punittype = unit_type_by_rule_name(sval);

    if (!punittype) {
      ruleset_error(LOG_FATAL,
                    "\"%s\" %s.%s (%d): couldn't match \"%s\".",
                    filename, prefix, entry, i, sval);
    }
    output[i] = punittype;
    log_debug("\"%s\" %s.%s (%d): %s (%d)", filename, prefix, entry, i, sval,
              utype_number(punittype));
  }
  free(slist);
  return;
}

/**************************************************************************
 Lookup a prefix.entry string vector in the file and fill in the
 array, which should hold MAX_NUM_TECH_LIST items. The output array is
 either A_LAST terminated or full (contains MAX_NUM_TECH_LIST
 items). All valid entries of the output array are guaranteed to
 exist. There should be at least one value, but it may be "",
 meaning empty list.
**************************************************************************/
static void lookup_tech_list(struct section_file *file, const char *prefix,
                             const char *entry, int *output,
                             const char *filename)
{
  const char **slist;
  size_t nval;
  int i;

  /* pre-fill with A_LAST: */
  for(i=0; i<MAX_NUM_TECH_LIST; i++) {
    output[i] = A_LAST;
  }
  slist = secfile_lookup_str_vec(file, &nval, "%s.%s", prefix, entry);
  if (nval==0) {
    ruleset_error(LOG_FATAL, "\"%s\": missing string vector %s.%s",
                  filename, prefix, entry);
  }
  if (nval>MAX_NUM_TECH_LIST) {
    ruleset_error(LOG_FATAL,
                  "\"%s\": string vector %s.%s too long (%d, max %d)",
                  filename, prefix, entry, (int) nval, MAX_NUM_TECH_LIST);
  }
  if (nval==1 && strcmp(slist[0], "")==0) {
    free(slist);
    return;
  }
  for (i=0; i<nval; i++) {
    const char *sval = slist[i];
    struct advance *padvance = advance_by_rule_name(sval);

    if (NULL == padvance) {
      ruleset_error(LOG_FATAL,
                    "\"%s\" %s.%s (%d): couldn't match \"%s\".",
                    filename, prefix, entry, i, sval);
    }
    if (!valid_advance(padvance)) {
      ruleset_error(LOG_FATAL, "\"%s\" %s.%s (%d): \"%s\" is removed.",
                    filename, prefix, entry, i, sval);
    }
    output[i] = advance_number(padvance);
    log_debug("\"%s\" %s.%s (%d): %s (%d)", filename, prefix, entry, i, sval,
              advance_number(padvance));
  }
  free(slist);
  return;
}

/**************************************************************************
  Lookup a prefix.entry string vector in the file and fill in the
  array, which should hold MAX_NUM_BUILDING_LIST items. The output array is
  either B_LAST terminated or full (contains MAX_NUM_BUILDING_LIST
  items). [All valid entries of the output array are guaranteed to pass
  improvement_exist()?] There should be at least one value, but it may be
  "", meaning an empty list.
**************************************************************************/
static void lookup_building_list(struct section_file *file,
                                 const char *prefix, const char *entry,
                                 int *output, const char *filename)
{
  const char **slist;
  size_t nval;
  int i;

  /* pre-fill with B_LAST: */
  for (i = 0; i < MAX_NUM_BUILDING_LIST; i++) {
    output[i] = B_LAST;
  }
  slist = secfile_lookup_str_vec(file, &nval, "%s.%s", prefix, entry);
  if (nval == 0) {
    ruleset_error(LOG_FATAL, "\"%s\": missing string vector %s.%s",
                  filename, prefix, entry);
  }
  if (nval > MAX_NUM_BUILDING_LIST) {
    ruleset_error(LOG_FATAL,
                  "\"%s\": string vector %s.%s too long (%d, max %d)",
                  filename, prefix, entry, (int) nval, MAX_NUM_BUILDING_LIST);
  }
  if (nval == 1 && strcmp(slist[0], "") == 0) {
    free(slist);
    return;
  }
  for (i = 0; i < nval; i++) {
    const char *sval = slist[i];
    struct impr_type *pimprove = improvement_by_rule_name(sval);

    if (NULL == pimprove) {
      ruleset_error(LOG_FATAL,
                    "\"%s\" %s.%s (%d): couldn't match \"%s\".",
                    filename, prefix, entry, i, sval);
    }
    output[i] = improvement_number(pimprove);
    log_debug("%s.%s,%d %s %d", prefix, entry, i, sval, output[i]);
  }
  free(slist);
}

/**************************************************************************
 Lookup a string prefix.entry in the file and return the corresponding
 unit_type id.  If (!required), return NULL if match "None" or can't match.
 If (required), die if can't match.
 If description is not NULL, it is used in the warning message
 instead of prefix (eg pass unit->name instead of prefix="units2.u27")
**************************************************************************/
static struct unit_type *lookup_unit_type(struct section_file *file,
					  const char *prefix,
					  const char *entry,
					  int loglevel,
					  const char *filename,
					  const char *description)
{
  const char *sval;
  struct unit_type *punittype;
  
  if (LOG_FATAL >= loglevel) {
    sval = secfile_lookup_str(file, "%s.%s", prefix, entry);
  } else {
    sval = secfile_lookup_str_default(file, "None", "%s.%s", prefix, entry);
  }

  if (strcmp(sval, "None")==0) {
    punittype = NULL;
  } else {
    punittype = unit_type_by_rule_name(sval);
    if (!punittype) {
      ruleset_error(loglevel,
                    "\"%s\" %s %s: couldn't match \"%s\".",
                    filename, (description ? description : prefix), entry, sval);

      /* We continue if error was not fatal. */
      punittype = NULL;
    }
  }
  return punittype;
}

/**************************************************************************
  Lookup entry in the file and return the corresponding government index;
  dies if can't find/match.  filename is for error message.
**************************************************************************/
static struct government *lookup_government(struct section_file *file,
					    const char *entry,
					    const char *filename)
{
  const char *sval;
  struct government *gov;
  
  sval = secfile_lookup_str(file, "%s", entry);
  gov = government_by_rule_name(sval);
  if (!gov) {
    ruleset_error(LOG_FATAL,
                  "\"%s\" %s: couldn't match \"%s\".",
                  filename, entry, sval);
  }
  return gov;
}

/**************************************************************************
  Lookup entry in the file and return the corresponding move_type index;
  dies if can't find/match.  filename is for error message.
**************************************************************************/
static enum unit_move_type lookup_move_type(struct section_file *file,
					    const char *entry,
					    const char *filename)
{
  const char *sval;
  enum unit_move_type mt;
  
  sval = secfile_lookup_str_default(file, NULL, "%s", entry);
  if (sval == NULL) {
    return unit_move_type_invalid();
  }

  mt = unit_move_type_by_name(sval, fc_strcasecmp);
  if (!unit_move_type_is_valid(mt)) {
    ruleset_error(LOG_FATAL,
                  "\"%s\" %s: couldn't match \"%s\".",
                  filename, entry, sval);
  }
  return mt;
}

/****************************************************************************
  Lookup optional string, returning allocated memory or NULL.
****************************************************************************/
static char *lookup_string(struct section_file *file, const char *prefix,
                           const char *suffix)
{
  const char *sval = secfile_lookup_str(file, "%s.%s", prefix, suffix);

  if (NULL != sval) {
    char copy[strlen(sval) + 1];

    strcpy(copy, sval);
    remove_leading_trailing_spaces(copy);
    if (strlen(copy) > 0) {
      return fc_strdup(copy);
    }
  }
  return NULL;
}

/****************************************************************************
  Lookup optional string vector, returning allocated memory or NULL.
****************************************************************************/
static struct strvec *lookup_strvec(struct section_file *file,
                                    const char *prefix, const char *suffix)
{
  size_t dim;
  const char **vec = secfile_lookup_str_vec(file, &dim,
                                            "%s.%s", prefix, suffix);

  if (NULL != vec) {
    struct strvec *dest = strvec_new();

    strvec_store(dest, vec, dim);
    free(vec);
    return dest;
  }
  return NULL;
}

/**************************************************************************
  Look up the resource section name and return its pointer.
**************************************************************************/
static struct resource *lookup_resource(const char *filename,
					const char *name,
					const char *jsection)
{
  resource_type_iterate(presource) {
    const int i = resource_index(presource);
    const char *isection = &resource_sections[i * MAX_SECTION_LABEL];
    if (0 == fc_strcasecmp(isection, name)) {
      return presource;
    }
  } resource_type_iterate_end;

  ruleset_error(LOG_ERROR,
                "\"%s\" [%s] has unknown \"%s\".",
                filename,
                jsection,
                name);
  return NULL;
}

/**************************************************************************
  Look up the terrain section name and return its pointer.
**************************************************************************/
static struct terrain *lookup_terrain(struct section_file *file,
				      const char *item,
				      struct terrain *pthis)
{
  const int j = terrain_index(pthis);
  const char *jsection = &terrain_sections[j * MAX_SECTION_LABEL];
  const char *name = secfile_lookup_str(file, "%s.%s", jsection, item);

  if (NULL == name
      || *name == '\0'
      || (0 == strcmp(name, "none"))
      || (0 == strcmp(name, "no"))) {
    return T_NONE;
  }
  if (0 == strcmp(name, "yes")) {
    return pthis;
  }

  terrain_type_iterate(pterrain) {
    const int i = terrain_index(pterrain);
    const char *isection = &terrain_sections[i * MAX_SECTION_LABEL];
    if (0 == fc_strcasecmp(isection, name)) {
      return pterrain;
    }
  } terrain_type_iterate_end;

  ruleset_error(LOG_ERROR, "\"%s\" [%s] has unknown \"%s\".",
                secfile_name(file), jsection, name);
  return T_NONE;
}

/**************************************************************************
  Load "name" and (optionally) "rule_name" into a struct name_translation.
**************************************************************************/
static void ruleset_load_names(struct name_translation *pname,
                               struct section_file *file,
                               const char *sec_name)
{
  const char *name = secfile_lookup_str(file, "%s.name", sec_name);
  const char *rule_name = secfile_lookup_str(file, "%s.rule_name", sec_name);

  if (!name) {
    ruleset_error(LOG_FATAL,
                  "\"%s\" [%s]: no \"name\" specified.",
                  secfile_name(file), sec_name);
  }

  names_set(pname, name, rule_name);
}

/**************************************************************************
  Load trait values to array.
**************************************************************************/
static void ruleset_load_traits(int *traits, struct section_file *file,
                                const char *secname, const char *field_prefix)
{
  enum trait tr;

  /* FIXME: Use specenum trait names without duplicating them here.
   *        Just needs to take care of case. */
  const char *trait_names[] = {
    "placeholder",
    NULL
  };

  for (tr = trait_begin(); tr != trait_end() && trait_names[tr] != NULL; tr = trait_next(tr)) {
     traits[tr] = secfile_lookup_int_default(file, -1, "%s.%s%s",
                                             secname,
                                             field_prefix,
                                             trait_names[tr]);
  }

  fc_assert(tr == trait_end()); /* number of trait_names correct */
}

/**************************************************************************
  Load names of technologies so other rulesets can refer to techs with
  their name.
**************************************************************************/
static void load_tech_names(struct section_file *file)
{
  struct section_list *sec;
  /* Number of techs in the ruleset (means without A_NONE). */
  int num_techs = 0;
  int i;
  const char *filename = secfile_name(file);

  (void) secfile_entry_by_path(file, "datafile.description");   /* unused */

  /* The names: */
  sec = secfile_sections_by_name_prefix(file, ADVANCE_SECTION_PREFIX);
  if (NULL == sec || 0 == (num_techs = section_list_size(sec))) {
    ruleset_error(LOG_FATAL, "\"%s\": No Advances?!?", filename);
  }
  log_verbose("%d advances (including possibly unused)", num_techs);

  if(num_techs + A_FIRST > A_LAST_REAL) {
    ruleset_error(LOG_FATAL, "\"%s\": Too many advances (%d, max %d)",
                  filename, num_techs, A_LAST_REAL-A_FIRST);
  }

  game.control.num_tech_types = num_techs + A_FIRST; /* includes A_NONE */

  i = 0;
  advance_iterate(A_FIRST, a) {
    ruleset_load_names(&a->name, file, section_name(section_list_get(sec, i)));
    i++;
  } advance_iterate_end;
  section_list_destroy(sec);
}

/**************************************************************************
  Load technologies related ruleset data
**************************************************************************/
static void load_ruleset_techs(struct section_file *file)
{
  struct section_list *sec;
  int i;
  struct advance *a_none = advance_by_number(A_NONE);
  const char *filename = secfile_name(file);
  
  (void) check_ruleset_capabilities(file, RULESET_CAPABILITIES, filename);
  sec = secfile_sections_by_name_prefix(file, ADVANCE_SECTION_PREFIX);

  /* Initialize dummy tech A_NONE */
  a_none->require[AR_ONE] = a_none;
  a_none->require[AR_TWO] = a_none;
  a_none->require[AR_ROOT] = A_NEVER;
  BV_CLR_ALL(a_none->flags);

  i = 0;
  advance_iterate(A_FIRST, a) {
    const char *sec_name = section_name(section_list_get(sec, i));
    const char *sval, **slist;
    size_t nval;
    int j, ival;

    a->require[AR_ONE] = lookup_tech(file, sec_name, "req1", LOG_ERROR,
                                     filename, rule_name(&a->name));
    a->require[AR_TWO] = lookup_tech(file, sec_name, "req2", LOG_ERROR,
                                     filename, rule_name(&a->name));
    a->require[AR_ROOT] = lookup_tech(file, sec_name, "root_req", LOG_ERROR,
                                      filename, rule_name(&a->name));

    if ((A_NEVER == a->require[AR_ONE] && A_NEVER != a->require[AR_TWO])
     || (A_NEVER != a->require[AR_ONE] && A_NEVER == a->require[AR_TWO])) {
      log_error("\"%s\" [%s] \"%s\": \"Never\" with non-\"Never\".",
                filename, sec_name, rule_name(&a->name));
      a->require[AR_ONE] = a->require[AR_TWO] = A_NEVER;
    }
    if (a_none == a->require[AR_ONE] && a_none != a->require[AR_TWO]) {
      log_error("\"%s\" [%s] \"%s\": should have \"None\" second.",
                filename, sec_name, rule_name(&a->name));
      a->require[AR_ONE] = a->require[AR_TWO];
      a->require[AR_TWO] = a_none;
    }

    BV_CLR_ALL(a->flags);

    slist = secfile_lookup_str_vec(file, &nval, "%s.flags", sec_name);
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
        continue;
      }
      ival = tech_flag_id_by_name(sval, fc_strcasecmp);
      if (!tech_flag_id_is_valid(ival)) {
        log_error("\"%s\" [%s] \"%s\": bad flag name \"%s\".",
                  filename, sec_name, rule_name(&a->name), sval);
      } else {
        BV_SET(a->flags, ival);
      }
    }
    free(slist);

    sz_strlcpy(a->graphic_str,
               secfile_lookup_str_default(file, "-", "%s.graphic", sec_name));
    sz_strlcpy(a->graphic_alt,
               secfile_lookup_str_default(file, "-",
                                          "%s.graphic_alt", sec_name));

    a->helptext = lookup_strvec(file, sec_name, "helptext");
    a->bonus_message = lookup_string(file, sec_name, "bonus_message");
    a->preset_cost =
        secfile_lookup_int_default(file, -1, "%s.%s", sec_name, "cost");
    a->num_reqs = 0;
    
    i++;
  } advance_iterate_end;

  /* Propagate a root tech up into the tech tree.  Thus if a technology
   * X has Y has a root tech, then any technology requiring X also has
   * Y as a root tech. */
restart:
  advance_iterate(A_FIRST, a) {
    if (valid_advance(a)
     && A_NEVER != a->require[AR_ROOT]) {
      bool out_of_order = FALSE;

      /* Now find any tech depending on this technology and update its
       * root_req. */
      advance_iterate(A_FIRST, b) {
        if (valid_advance(b)
         && A_NEVER == b->require[AR_ROOT]
         && (a == b->require[AR_ONE] || a == b->require[AR_TWO])) {
          b->require[AR_ROOT] = a->require[AR_ROOT];
	  if (b < a) {
	    out_of_order = TRUE;
          }
        }
      } advance_iterate_end;

      if (out_of_order) {
	/* HACK: If we just changed the root_tech of a lower-numbered
	 * technology, we need to go back so that we can propagate the
	 * root_tech up to that technology's parents... */
	goto restart;   
      }
    }
  } advance_iterate_end;

  /* Now rename A_NEVER to A_NONE for consistency */
  advance_iterate(A_NONE, a) {
    if (A_NEVER == a->require[AR_ROOT]) {
      a->require[AR_ROOT] = a_none;
    }
  } advance_iterate_end;

  /* Some more consistency checking: 
     Non-removed techs depending on removed techs is too
     broken to fix by default, so die.
  */
  advance_iterate(A_FIRST, a) {
    if (valid_advance(a)) {
      /* We check for recursive tech loops later,
       * in build_required_techs_helper. */
      if (!valid_advance(a->require[AR_ONE])) {
        ruleset_error(LOG_FATAL,
                      "\"%s\" tech \"%s\": req1 leads to removed tech.",
                      filename,
                      advance_rule_name(a));
      } 
      if (!valid_advance(a->require[AR_TWO])) {
        ruleset_error(LOG_FATAL,
                      "\"%s\" tech \"%s\": req2 leads to removed tech.",
                      filename,
                      advance_rule_name(a));
      }
    }
  } advance_iterate_end;

  section_list_destroy(sec);
  secfile_check_unused(file);
  secfile_destroy(file);
}

/**************************************************************************
  Load names of units so other rulesets can refer to units with
  their name.
**************************************************************************/
static void load_unit_names(struct section_file *file)
{
  struct section_list *sec;
  int nval = 0;
  size_t user_flags;
  const char **flaglist;
  int i;
  const char *filename = secfile_name(file);

  (void) secfile_entry_by_path(file, "datafile.description");   /* unused */

  /* User unit flag names */
  flaglist = secfile_lookup_str_vec(file, &user_flags, "flags.names");

  if (user_flags > MAX_NUM_USER_UNIT_FLAGS) {
    ruleset_error(LOG_FATAL, "\"%s\": Too many user unit type flags!",
                  filename);
  }

  for (i = 0; i < user_flags; i++) {
    set_user_unit_flag_name(F_USER_FLAG_1 + i, flaglist[i]);
  }
  for (; i < MAX_NUM_USER_UNIT_FLAGS; i++) {
    set_user_unit_flag_name(F_USER_FLAG_1 + i, NULL);
  }
  if (flaglist) {
    free(flaglist);
  }

  /* Unit classes */
  sec = secfile_sections_by_name_prefix(file, UNIT_CLASS_SECTION_PREFIX);
  if (NULL == sec || 0 == (nval = section_list_size(sec))) {
    ruleset_error(LOG_FATAL, "\"%s\": No unit classes?!?", filename);
  }
  log_verbose("%d unit classes", nval);
  if(nval > UCL_LAST) {
    ruleset_error(LOG_FATAL, "\"%s\": Too many unit classes (%d, max %d)",
                  filename, nval, UCL_LAST);
  }

  game.control.num_unit_classes = nval;

  unit_class_iterate(punitclass) {
    const int i = uclass_index(punitclass);
    ruleset_load_names(&punitclass->name, file,
                       section_name(section_list_get(sec, i)));
  } unit_class_iterate_end;
  section_list_destroy(sec);

  /* The names: */
  sec = secfile_sections_by_name_prefix(file, UNIT_SECTION_PREFIX);
  if (NULL == sec || 0 == (nval = section_list_size(sec))) {
    ruleset_error(LOG_FATAL, "\"%s\": No unit types?!?", filename);
  }
  log_verbose("%d unit types (including possibly unused)", nval);
  if(nval > U_LAST) {
    ruleset_error(LOG_FATAL, "\"%s\": Too many unit types (%d, max %d)",
                  filename, nval, U_LAST);
  }

  game.control.num_unit_types = nval;

  unit_type_iterate(punittype) {
    const int i = utype_index(punittype);
    ruleset_load_names(&punittype->name, file,
                       section_name(section_list_get(sec, i)));
  } unit_type_iterate_end;
  section_list_destroy(sec);
}

/**************************************************************************
  Load veteran levels.
**************************************************************************/
static bool load_ruleset_veteran(struct section_file *file,
                                 const char *path,
                                 struct veteran_system **vsystem, char *err,
                                 size_t err_len)
{
  const char **vlist_name;
  int *vlist_power, *vlist_raise, *vlist_wraise, *vlist_move;
  size_t count_name, count_power, count_raise, count_wraise, count_move;
  int i;
  bool ret = TRUE;

  /* The pointer should be uninitialised. */
  if (*vsystem != NULL) {
    fc_snprintf(err, err_len, "Veteran system is defined?!");
    return FALSE;
  }

  /* Load data. */
  vlist_name = secfile_lookup_str_vec(file, &count_name,
                                      "%s.veteran_names", path);
  vlist_power = secfile_lookup_int_vec(file, &count_power,
                                      "%s.veteran_power_fact", path);
  vlist_raise = secfile_lookup_int_vec(file, &count_raise,
                                       "%s.veteran_raise_chance", path);
  vlist_wraise = secfile_lookup_int_vec(file, &count_wraise,
                                        "%s.veteran_work_raise_chance",
                                        path);
  vlist_move = secfile_lookup_int_vec(file, &count_move,
                                      "%s.veteran_move_bonus", path);

  if (count_name > MAX_VET_LEVELS) {
    ret = FALSE;
    fc_snprintf(err, err_len, "\"%s\": Too many veteran levels (section "
                              "'%s': %lu, max %d)", secfile_name(file), path,
                (long unsigned)count_name, MAX_VET_LEVELS);
  } else if (count_name != count_power
             || count_name != count_raise
             || count_name != count_wraise
             || count_name != count_move) {
    ret = FALSE;
    fc_snprintf(err, err_len, "\"%s\": Different lengths for the veteran "
                              "settings in section '%s'", secfile_name(file),
                path);
  } else if (count_name == 0) {
    /* Nothing defined. */
    *vsystem = NULL;
  } else {
    /* Generate the veteran system. */
    *vsystem = veteran_system_new((int)count_name);

#define rs_sanity_veteran(_path, _entry, _i, _condition, _action)            \
  if (_condition) {                                                          \
    log_error("Invalid veteran definition '%s.%s[%d]'!",                     \
              _path, _entry, _i);                                            \
    log_debug("Failed check: '%s'. Update value: '%s'.",                     \
              #_condition, #_action);                                        \
    _action;                                                                 \
  }
    for (i = 0; i < count_name; i++) {
      /* Some sanity checks. */
      rs_sanity_veteran(path, "veteran_power_fact", i,
                        (vlist_power[i] < 0), vlist_power[i] = 0);
      rs_sanity_veteran(path, "veteran_raise_chance", i,
                        (vlist_raise[i] < 0), vlist_raise[i] = 0);
      rs_sanity_veteran(path, "veteran_work_raise_chance", i,
                        (vlist_raise[i] < 0), vlist_wraise[i] = 0);
      rs_sanity_veteran(path, "veteran_move_bonus", i,
                        (vlist_move[i] < 0), vlist_move[i] = 0);
      if (i == 0) {
        /* First element.*/
        rs_sanity_veteran(path, "veteran_power_fact", i,
                          (vlist_power[i] != 100), vlist_power[i] = 100);
      } else if (i == count_name - 1) {
        /* Last element. */
        rs_sanity_veteran(path, "veteran_power_fact", i,
                          (vlist_power[i] < vlist_power[i - 1]),
                          vlist_power[i] = vlist_power[i - 1]);
        rs_sanity_veteran(path, "veteran_raise_chance", i,
                          (vlist_raise[i] != 0), vlist_raise[i] = 0);
        rs_sanity_veteran(path, "veteran_work_raise_chance", i,
                          (vlist_raise[i] != 0), vlist_wraise[i] = 0);
      } else {
        /* All elements inbetween. */
        rs_sanity_veteran(path, "veteran_power_fact", i,
                          (vlist_power[i] < vlist_power[i - 1]),
                          vlist_power[i] = vlist_power[i - 1]);
        rs_sanity_veteran(path, "veteran_raise_chance", i,
                          (vlist_raise[i] > 100), vlist_raise[i] = 100);
        rs_sanity_veteran(path, "veteran_work_raise_chance", i,
                          (vlist_raise[i] > 100), vlist_wraise[i] = 100);
      }

      veteran_system_definition(*vsystem, i, vlist_name[i], vlist_power[i],
                                vlist_move[i], vlist_raise[i],
                                vlist_wraise[i]);
    }
#undef rs_sanity_veteran
  }

  if (vlist_name) {
    free(vlist_name);
  }
  if (vlist_power) {
    free(vlist_power);
  }
  if (vlist_raise) {
    free(vlist_raise);
  }
  if (vlist_wraise) {
    free(vlist_wraise);
  }
  if (vlist_move) {
    free(vlist_move);
  }

  return ret;
}

/**************************************************************************
  Load units related ruleset data.
**************************************************************************/
static void load_ruleset_units(struct section_file *file)
{
  struct unit_type *u;
  int j, ival;
  size_t nval;
  struct section_list *sec, *csec;
  const char *sval, **slist;
  const char *filename = secfile_name(file);
  char msg[MAX_LEN_MSG];

  (void) check_ruleset_capabilities(file, RULESET_CAPABILITIES, filename);

  if (!load_ruleset_veteran(file, "veteran_system", &game.veteran, msg,
                            sizeof(msg)) || game.veteran == NULL) {
    ruleset_error(LOG_FATAL, "Error loading the default veteran system: %s",
                  msg);
  }

  sec = secfile_sections_by_name_prefix(file, UNIT_SECTION_PREFIX);
  nval = (NULL != sec ? section_list_size(sec) : 0);

  csec = secfile_sections_by_name_prefix(file, UNIT_CLASS_SECTION_PREFIX);
  nval = (NULL != csec ? section_list_size(csec) : 0);

  unit_class_iterate(uc) {
    int i = uclass_index(uc);
    char tmp[200] = "\0";
    const char *hut_str;
    const char *sec_name = section_name(section_list_get(csec, i));

    if (secfile_lookup_int(file, &uc->min_speed, "%s.min_speed", sec_name)) {
      uc->min_speed *= SINGLE_MOVE;
    } else {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }
    if (!secfile_lookup_int(file, &uc->hp_loss_pct,
                            "%s.hp_loss_pct", sec_name)) {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }

    hut_str = secfile_lookup_str_default(file, "Normal", "%s.hut_behavior", sec_name);
    if (fc_strcasecmp(hut_str, "Normal") == 0) {
      uc->hut_behavior = HUT_NORMAL;
    } else if (fc_strcasecmp(hut_str, "Nothing") == 0) {
      uc->hut_behavior = HUT_NOTHING;
    } else if (fc_strcasecmp(hut_str, "Frighten") == 0) {
      uc->hut_behavior = HUT_FRIGHTEN;
    } else {
      ruleset_error(LOG_FATAL,
                    "\"%s\" unit_class \"%s\":"
                    " Illegal hut behavior \"%s\".",
                    filename,
                    uclass_rule_name(uc),
                    hut_str);
    }

    BV_CLR_ALL(uc->flags);
    slist = secfile_lookup_str_vec(file, &nval, "%s.flags", sec_name);
    for(j = 0; j < nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"") == 0) {
        continue;
      }
      ival = unit_class_flag_id_by_name(sval, fc_strcasecmp);
      if (!unit_class_flag_id_is_valid(ival)) {
        log_error("\"%s\" unit_class \"%s\": bad flag name \"%s\".",
                  filename, uclass_rule_name(uc), sval);
        ival = unit_flag_by_rule_name(sval);
        if (ival != F_LAST) {
          log_error("\"%s\" unit_class \"%s\": unit_type flag!",
                    filename, uclass_rule_name(uc));
        }
      } else {
        BV_SET(uc->flags, ival);
      }
    }
    free(slist);

    fc_strlcat(tmp, sec_name, 200);
    fc_strlcat(tmp, ".move_type", 200);
    uc->move_type = lookup_move_type(file, tmp, filename);

    if (!unit_move_type_is_valid(uc->move_type)) {
      /* Not explicitly given, determine automatically */
      bool land_moving = FALSE;
      bool sea_moving = FALSE;

      if (uclass_has_flag(uc, UCF_RIVER_NATIVE)) {
        land_moving = TRUE;
      } else {
        road_type_iterate(proad) {
          /* Check all roads in case there's native one among them */
          if (is_native_road_to_uclass(proad, uc)) {
            land_moving = TRUE;
          }
        } road_type_iterate_end;
      }

      terrain_type_iterate(pterrain) {
        bv_special spe;
        bv_bases bases;

        BV_CLR_ALL(spe);
        BV_CLR_ALL(bases);

        if (is_native_to_class(uc, pterrain, spe, bases)) {
          if (is_ocean(pterrain)) {
            sea_moving = TRUE;
          } else {
            land_moving = TRUE;
          }
        }
      } terrain_type_iterate_end;

      if (land_moving && sea_moving) {
        uc->move_type = UMT_BOTH;
      } else if (sea_moving) {
        uc->move_type = UMT_SEA;
      } else {
        /* If unit has no native terrains, it is considered land moving */
        uc->move_type = UMT_LAND;
      }
    } else if (uc->move_type == UMT_SEA) {
      /* Explicitly given SEA_MOVING */
      if (uclass_has_flag(uc, UCF_RIVER_NATIVE)) {
        log_error("\"%s\" unit_class \"%s\": cannot give RiverNative "
                  "flag to sea moving unit",
                  filename, uclass_rule_name(uc));
        BV_CLR(uc->flags, UCF_RIVER_NATIVE);
      }
      road_type_iterate(proad) {
        if (is_native_road_to_uclass(proad, uc)) {
          ruleset_error(LOG_FATAL,
                        "\"%s\" unit_class \"%s\": cannot make road \"%s\" "
                        "native to sea moving unit.",
                        filename, uclass_rule_name(uc),
                        road_rule_name(proad));
        }
      } road_type_iterate_end;
    }
  } unit_class_iterate_end;

  /* Tech and Gov requirements; per unit veteran system */
  unit_type_iterate(u) {
    const int i = utype_index(u);
    const struct section *psection = section_list_get(sec, i);
    const char *sec_name = section_name(psection);

    u->require_advance = lookup_tech(file, sec_name,
                                     "tech_req", LOG_FATAL, filename,
                                     rule_name(&u->name));
    if (NULL != section_entry_by_name(psection, "gov_req")) {
      char tmp[200] = "\0";
      fc_strlcat(tmp, section_name(psection), sizeof(tmp));
      fc_strlcat(tmp, ".gov_req", sizeof(tmp));
      u->need_government = lookup_government(file, tmp, filename);
    } else {
      u->need_government = NULL; /* no requirement */
    }

    if (!load_ruleset_veteran(file, sec_name, &u->veteran,
                              msg, sizeof(msg))) {
      ruleset_error(LOG_NORMAL, "Error loading the veteran system: %s",
                    msg);
    }

    u->obsoleted_by = lookup_unit_type(file, sec_name, "obsolete_by",
                                       LOG_ERROR, filename,
                                       rule_name(&u->name));
    u->converted_to = lookup_unit_type(file, sec_name, "convert_to",
                                       LOG_ERROR, filename,
                                       rule_name(&u->name));
  } unit_type_iterate_end;

  /* main stats: */
  unit_type_iterate(u) {
    const int i = utype_index(u);
    struct unit_class *pclass;
    const char *sec_name = section_name(section_list_get(sec, i));
    const char *string;

    u->need_improvement = lookup_building(file, sec_name, "impr_req",
                                          LOG_ERROR, filename,
                                          rule_name(&u->name));

    sval = secfile_lookup_str(file, "%s.class", sec_name);
    pclass = unit_class_by_rule_name(sval);
    if (!pclass) {
      ruleset_error(LOG_FATAL,
                    "\"%s\" unit_type \"%s\":"
                    " bad class \"%s\".",
                    filename,
                    utype_rule_name(u),
                    sval);
    }
    u->uclass = pclass;
    
    sz_strlcpy(u->sound_move,
               secfile_lookup_str_default(file, "-", "%s.sound_move",
                                          sec_name));
    sz_strlcpy(u->sound_move_alt,
               secfile_lookup_str_default(file, "-", "%s.sound_move_alt",
                                          sec_name));
    sz_strlcpy(u->sound_fight,
               secfile_lookup_str_default(file, "-", "%s.sound_fight",
                                          sec_name));
    sz_strlcpy(u->sound_fight_alt,
               secfile_lookup_str_default(file, "-", "%s.sound_fight_alt",
                                          sec_name));

    if ((string = secfile_lookup_str(file, "%s.graphic", sec_name))) {
      sz_strlcpy(u->graphic_str, string);
    } else {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }
    sz_strlcpy(u->graphic_alt,
               secfile_lookup_str_default(file, "-", "%s.graphic_alt",
                                          sec_name));

    if (!secfile_lookup_int(file, &u->build_cost,
                            "%s.build_cost", sec_name)
        || !secfile_lookup_int(file, &u->pop_cost,
                               "%s.pop_cost", sec_name)
        || !secfile_lookup_int(file, &u->attack_strength,
                               "%s.attack", sec_name)
        || !secfile_lookup_int(file, &u->defense_strength,
                               "%s.defense", sec_name)
        || !secfile_lookup_int(file, &u->move_rate,
                               "%s.move_rate", sec_name)
        || !secfile_lookup_int(file, &u->vision_radius_sq,
                               "%s.vision_radius_sq", sec_name)
        || !secfile_lookup_int(file, &u->transport_capacity,
                               "%s.transport_cap", sec_name)
        || !secfile_lookup_int(file, &u->hp,
                               "%s.hitpoints", sec_name)
        || !secfile_lookup_int(file, &u->firepower,
                               "%s.firepower", sec_name)
        || !secfile_lookup_int(file, &u->fuel,
                               "%s.fuel", sec_name)
        || !secfile_lookup_int(file, &u->happy_cost,
                               "%s.uk_happy", sec_name)) {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }
    u->move_rate *= SINGLE_MOVE;

    if (u->firepower <= 0) {
      ruleset_error(LOG_FATAL,
                    "\"%s\" unit_type \"%s\":"
                    " firepower is %d,"
                    " but must be at least 1. "
                    "  If you want no attack ability,"
                    " set the unit's attack strength to 0.",
                    filename,
                    utype_rule_name(u),
                    u->firepower);
    }

    output_type_iterate(o) {
      u->upkeep[o] = secfile_lookup_int_default(file, 0, "%s.uk_%s",
                                                sec_name,
                                                get_output_identifier(o));
    } output_type_iterate_end;

    slist = secfile_lookup_str_vec(file, &nval, "%s.cargo", sec_name);
    BV_CLR_ALL(u->cargo);
    for (j = 0; j < nval; j++) {
      struct unit_class *uclass = unit_class_by_rule_name(slist[j]);

      if (!uclass) {
        ruleset_error(LOG_FATAL,
                      "\"%s\" unit_type \"%s\":"
                      "has unknown unit class %s as cargo.",
                      filename,
                      utype_rule_name(u),
                      slist[j]);
      }

      BV_SET(u->cargo, uclass_index(uclass));
    }
    free(slist);

    slist = secfile_lookup_str_vec(file, &nval, "%s.targets", sec_name);
    BV_CLR_ALL(u->targets);
    for (j = 0; j < nval; j++) {
      struct unit_class *uclass = unit_class_by_rule_name(slist[j]);

      if (!uclass) {
        ruleset_error(LOG_FATAL,
                      "\"%s\" unit_type \"%s\":"
                      "has unknown unit class %s as target.",
                      filename,
                      utype_rule_name(u),
                      slist[j]);
      }

      BV_SET(u->targets, uclass_index(uclass));
    }
    free(slist);

    /* Set also all classes that are never unreachable as targets. */
    unit_class_iterate(pclass) {
      if (!uclass_has_flag(pclass, UCF_UNREACHABLE)) {
        BV_SET(u->targets, uclass_index(pclass));
      }
    } unit_class_iterate_end;

    u->helptext = lookup_strvec(file, sec_name, "helptext");

    u->paratroopers_range = secfile_lookup_int_default(file,
        0, "%s.paratroopers_range", sec_name);
    u->paratroopers_mr_req = SINGLE_MOVE * secfile_lookup_int_default(file,
        0, "%s.paratroopers_mr_req", sec_name);
    u->paratroopers_mr_sub = SINGLE_MOVE * secfile_lookup_int_default(file,
        0, "%s.paratroopers_mr_sub", sec_name);
    u->bombard_rate = secfile_lookup_int_default(file,
        0, "%s.bombard_rate", sec_name);
    u->city_size = secfile_lookup_int_default(file,
        1, "%s.city_size", sec_name);
  } unit_type_iterate_end;
  
  /* flags */
  unit_type_iterate(u) {
    const int i = utype_index(u);

    BV_CLR_ALL(u->flags);
    fc_assert(!utype_has_flag(u, F_LAST - 1));

    slist = secfile_lookup_str_vec(file, &nval, "%s.flags",
                                   section_name(section_list_get(sec, i)));
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if (0 == strcmp(sval, "")) {
        continue;
      }
      ival = unit_flag_by_rule_name(sval);
      if (F_LAST == ival) {
        log_error("\"%s\" unit_type \"%s\": bad flag name \"%s\".",
                  filename, utype_rule_name(u),  sval);
        ival = unit_class_flag_id_by_name(sval, fc_strcasecmp);
        if (unit_class_flag_id_is_valid(ival)) {
          log_error("\"%s\" unit_type \"%s\": unit_class flag!",
                    filename, utype_rule_name(u));
        }
      } else {
        BV_SET(u->flags, ival);
      }
      fc_assert(utype_has_flag(u, ival));
    }
    free(slist);
  } unit_type_iterate_end;
    
  /* roles */
  unit_type_iterate(u) {
    const int i = utype_index(u);

    BV_CLR_ALL(u->roles);
    
    slist = secfile_lookup_str_vec(file, &nval, "%s.roles",
                                   section_name(section_list_get(sec, i)));
    for(j=0; j<nval; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
	continue;
      }
      ival = unit_role_by_rule_name(sval);
      if (ival==L_LAST) {
        log_error("\"%s\" unit_type \"%s\": bad role name \"%s\".",
                  filename, utype_rule_name(u), sval);
      } else if ((ival == L_FERRYBOAT || ival == L_BARBARIAN_BOAT)
                 && u->uclass->move_type == UMT_LAND) {
        log_error( "\"%s\" unit_type \"%s\": role \"%s\" "
                  "for land moving unit.",
                  filename, utype_rule_name(u), sval);
      } else {
        BV_SET(u->roles, ival - L_FIRST);
      }
      fc_assert(utype_has_role(u, ival));
    }
    free(slist);
  } unit_type_iterate_end;

  /* Some more consistency checking: */
  unit_type_iterate(u) {
    if (!valid_advance(u->require_advance)) {
      log_error("\"%s\" unit_type \"%s\": depends on removed tech \"%s\".",
                filename, utype_rule_name(u),
                advance_rule_name(u->require_advance));
      u->require_advance = A_NEVER;
    }

    if (utype_has_flag(u, F_SETTLERS)
        && u->city_size <= 0) {
      ruleset_error(LOG_ERROR, "\"%s\": Unit %s would build size %d cities",
                    filename, utype_rule_name(u), u->city_size);
      u->city_size = 1;
    }
  } unit_type_iterate_end;

  /* Setup roles and flags pre-calcs: */
  role_unit_precalcs();
     
  /* Check some required flags and roles etc: */
  if(num_role_units(F_CITIES)==0) {
    ruleset_error(LOG_FATAL, "\"%s\": No flag=cities units?", filename);
  }
  if(num_role_units(F_SETTLERS)==0) {
    ruleset_error(LOG_FATAL, "\"%s\": No flag=settler units?", filename);
  }
  if(num_role_units(L_EXPLORER)==0) {
    ruleset_error(LOG_FATAL, "\"%s\": No role=explorer units?", filename);
  }
  if(num_role_units(L_FERRYBOAT)==0) {
    ruleset_error(LOG_FATAL, "\"%s\": No role=ferryboat units?", filename);
  }
  if(num_role_units(L_FIRSTBUILD)==0) {
    ruleset_error(LOG_FATAL, "\"%s\": No role=firstbuild units?", filename);
  }
  if (0 == num_role_units(L_BARBARIAN)
      && BARBS_DISABLED != game.server.barbarianrate) {
    ruleset_error(LOG_FATAL, "\"%s\": No role=barbarian units?", filename);
  }
  if (0 == num_role_units(L_BARBARIAN_LEADER)
      && BARBS_DISABLED != game.server.barbarianrate) {
    ruleset_error(LOG_FATAL, "\"%s\": No role=barbarian leader units?", filename);
  }
  if (0 == num_role_units(L_BARBARIAN_BUILD)
      && BARBS_DISABLED != game.server.barbarianrate) {
    ruleset_error(LOG_FATAL, "\"%s\": No role=barbarian build units?", filename);
  }
  if (0 == num_role_units(L_BARBARIAN_BOAT)
      && BARBS_DISABLED != game.server.barbarianrate) {
    ruleset_error(LOG_FATAL, "\"%s\": No role=barbarian ship units?", filename);
  } else if (num_role_units(L_BARBARIAN_BOAT) > 0) {
    u = get_role_unit(L_BARBARIAN_BOAT,0);
    if(utype_move_type(u) != UMT_SEA) {
      ruleset_error(LOG_FATAL,
                    "\"%s\": Barbarian boat (%s) needs to be a sea unit.",
                    filename,
                    utype_rule_name(u));
    }
  }
  if (0 == num_role_units(L_BARBARIAN_SEA)
      && BARBS_DISABLED != game.server.barbarianrate) {
    ruleset_error(LOG_FATAL, "\"%s\": No role=sea raider barbarian units?",
                  filename);
  }

  section_list_destroy(csec);
  section_list_destroy(sec);
  secfile_check_unused(file);
  secfile_destroy(file);
}

/**************************************************************************
  Load names of buildings so other rulesets can refer to buildings with
  their name.
**************************************************************************/
static void load_building_names(struct section_file *file)
{
  struct section_list *sec;
  int i, nval = 0;
  const char *filename = secfile_name(file);

  (void) secfile_entry_by_path(file, "datafile.description");   /* unused */

  /* The names: */
  sec = secfile_sections_by_name_prefix(file, BUILDING_SECTION_PREFIX);
  if (NULL == sec || 0 == (nval = section_list_size(sec))) {
    ruleset_error(LOG_FATAL, "\"%s\": No improvements?!?", filename);
  }
  log_verbose("%d improvement types (including possibly unused)", nval);
  if (nval > B_LAST) {
    ruleset_error(LOG_FATAL, "\"%s\": Too many improvements (%d, max %d)",
                  filename, nval, B_LAST);
  }

  game.control.num_impr_types = nval;

  for (i = 0; i < nval; i++) {
    struct impr_type *b = improvement_by_number(i);
    ruleset_load_names(&b->name, file, section_name(section_list_get(sec, i)));
  }

  section_list_destroy(sec);
}

/**************************************************************************
  Load buildings related ruleset data
**************************************************************************/
static void load_ruleset_buildings(struct section_file *file)
{
  struct section_list *sec;
  const char *item;
  int i, nval;
  const char *filename = secfile_name(file);

  (void) check_ruleset_capabilities(file, RULESET_CAPABILITIES, filename);

  sec = secfile_sections_by_name_prefix(file, BUILDING_SECTION_PREFIX);
  nval = (NULL != sec ? section_list_size(sec) : 0);

  for (i = 0; i < nval; i++) {
    struct impr_type *b = improvement_by_number(i);
    const char *sec_name = section_name(section_list_get(sec, i));
    struct requirement_vector *reqs =
      lookup_req_list(file, sec_name, "reqs",
                      improvement_rule_name(b));
    const char *sval, **slist;
    int j, ival;
    size_t nflags;

    item = secfile_lookup_str(file, "%s.genus", sec_name);
    b->genus = impr_genus_id_by_name(item, fc_strcasecmp);
    if (!impr_genus_id_is_valid(b->genus)) {
      ruleset_error(LOG_FATAL, "\"%s\" improvement \"%s\": couldn't match "
                               "genus \"%s\".", filename,
                    improvement_rule_name(b), item);
    }

    slist = secfile_lookup_str_vec(file, &nflags, "%s.flags", sec_name);
    BV_CLR_ALL(b->flags);

    for(j=0; j<nflags; j++) {
      sval = slist[j];
      if(strcmp(sval,"")==0) {
	continue;
      }
      ival = impr_flag_id_by_name(sval, fc_strcasecmp);
      if (!impr_flag_id_is_valid(ival)) {
        log_error("\"%s\" improvement \"%s\": bad flag name \"%s\".",
                  filename, improvement_rule_name(b), sval);
      } else {
        BV_SET(b->flags, ival);
      }
    }
    free(slist);

    requirement_vector_copy(&b->reqs, reqs);

    b->obsolete_by = lookup_tech(file, sec_name, "obsolete_by", LOG_ERROR,
                                 filename, rule_name(&b->name));
    if (advance_by_number(A_NONE) == b->obsolete_by) {
      /* 
       * The ruleset can specify "None" for a never-obsoleted
       * improvement.  Currently this means A_NONE, which is an
       * unnecessary special-case.  We use A_NEVER to flag a
       * never-obsoleted improvement in the code instead.
       * (Test for valid_advance() later.)
       */
      b->obsolete_by = A_NEVER;
    }

    b->replaced_by = lookup_building(file, sec_name, "replaced_by",
                                     LOG_ERROR, filename,
                                     rule_name(&b->name));

    if (!secfile_lookup_int(file, &b->build_cost,
                            "%s.build_cost", sec_name)
        || !secfile_lookup_int(file, &b->upkeep,
                               "%s.upkeep", sec_name)
        || !secfile_lookup_int(file, &b->sabotage,
                               "%s.sabotage", sec_name)) {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }

    sz_strlcpy(b->graphic_str,
               secfile_lookup_str_default(file, "-",
                                          "%s.graphic", sec_name));
    sz_strlcpy(b->graphic_alt,
               secfile_lookup_str_default(file, "-",
                                          "%s.graphic_alt", sec_name));

    sz_strlcpy(b->soundtag,
               secfile_lookup_str_default(file, "-",
                                          "%s.sound", sec_name));
    sz_strlcpy(b->soundtag_alt,
               secfile_lookup_str_default(file, "-",
                                          "%s.sound_alt", sec_name));
    b->helptext = lookup_strvec(file, sec_name, "helptext");

    b->allows_units = FALSE;
    unit_type_iterate(ut) {
      if (ut->need_improvement == b) {
        b->allows_units = TRUE;
        break;
      }
    } unit_type_iterate_end;
  }

  /* Some more consistency checking: */
  improvement_iterate(b) {
    if (valid_improvement(b)) {
      if (A_NEVER != b->obsolete_by
          && !valid_advance(b->obsolete_by)) {
        log_error("\"%s\" improvement \"%s\": obsoleted by "
                  "removed tech \"%s\".",
                  filename, improvement_rule_name(b),
                  advance_rule_name(b->obsolete_by));
        b->obsolete_by = A_NEVER;
      }
    }
  } improvement_iterate_end;

  section_list_destroy(sec);
  secfile_check_unused(file);
  secfile_destroy(file);
}

/**************************************************************************
  Load names of terrain types so other rulesets can refer to terrains with
  their name.
**************************************************************************/
static void load_terrain_names(struct section_file *file)
{
  int nval = 0;
  struct section_list *sec;
  const char *filename = secfile_name(file);

  (void) secfile_entry_by_path(file, "datafile.description");   /* unused */

  /* terrain names */

  sec = secfile_sections_by_name_prefix(file, TERRAIN_SECTION_PREFIX);
  if (NULL == sec || 0 == (nval = section_list_size(sec))) {
    ruleset_error(LOG_FATAL, "\"%s\": ruleset doesn't have any terrains.",
                  filename);
  }
  if (nval > MAX_NUM_TERRAINS) {
    ruleset_error(LOG_FATAL, "\"%s\": Too many terrains (%d, max %d)",
                  filename, nval, MAX_NUM_TERRAINS);
  }
  game.control.terrain_count = nval;

  /* avoid re-reading files */
  if (terrain_sections) {
    free(terrain_sections);
  }
  terrain_sections = fc_calloc(nval, MAX_SECTION_LABEL);

  terrain_type_iterate(pterrain) {
    const int i = terrain_index(pterrain);
    const char *sec_name = section_name(section_list_get(sec, i));
    ruleset_load_names(&pterrain->name, file, sec_name);

    if (0 == strcmp(rule_name(&pterrain->name), "unused")) {
      name_set(&pterrain->name, "");
    }

    section_strlcpy(&terrain_sections[i * MAX_SECTION_LABEL], sec_name);
  } terrain_type_iterate_end;

  section_list_destroy(sec);

  /* resource names */

  sec = secfile_sections_by_name_prefix(file, RESOURCE_SECTION_PREFIX);
  nval = (NULL != sec ? section_list_size(sec) : 0);
  if (nval > MAX_NUM_RESOURCES) {
    ruleset_error(LOG_FATAL, "\"%s\": Too many resources (%d, max %d)",
                  filename, nval, MAX_NUM_RESOURCES);
  }
  game.control.resource_count = nval;

  /* avoid re-reading files */
  if (resource_sections) {
    free(resource_sections);
  }
  resource_sections = fc_calloc(nval, MAX_SECTION_LABEL);

  resource_type_iterate(presource) {
    const int i = resource_index(presource);
    const char *sec_name = section_name(section_list_get(sec, i));
    ruleset_load_names(&presource->name, file, sec_name);

    if (0 == strcmp(rule_name(&presource->name), "unused")) {
      name_set(&presource->name, "");
    }

    section_strlcpy(&resource_sections[i * MAX_SECTION_LABEL], sec_name);
  } resource_type_iterate_end;

  if (NULL != sec) {
    section_list_destroy(sec);
  }

  /* base names */

  sec = secfile_sections_by_name_prefix(file, BASE_SECTION_PREFIX);
  nval = (NULL != sec ? section_list_size(sec) : 0);
  if (nval > MAX_BASE_TYPES) {
    ruleset_error(LOG_FATAL, "\"%s\": Too many base types (%d, max %d)",
                  filename, nval, MAX_BASE_TYPES);
  }
  game.control.num_base_types = nval;

  if (base_sections) {
    free(base_sections);
  }
  base_sections = fc_calloc(nval, MAX_SECTION_LABEL);

  base_type_iterate(pbase) {
    const int i = base_index(pbase);
    const char *sec_name = section_name(section_list_get(sec, i));
    ruleset_load_names(&pbase->name, file, sec_name);
    section_strlcpy(&base_sections[i * MAX_SECTION_LABEL], sec_name);
  } base_type_iterate_end;

  /* road names */

  sec = secfile_sections_by_name_prefix(file, ROAD_SECTION_PREFIX);
  nval = (NULL != sec ? section_list_size(sec) : 0);
  if (nval != MAX_ROAD_TYPES) {
    ruleset_error(LOG_FATAL, "\"%s\": Number of road types not %d, but %d)",
                  filename, MAX_ROAD_TYPES, nval);
  }
  game.control.num_road_types = nval;

  if (road_sections) {
    free(road_sections);
  }
  road_sections = fc_calloc(nval, MAX_SECTION_LABEL);

  road_type_iterate(proad) {
    const int i = road_index(proad);
    const char *sec_name = section_name(section_list_get(sec, i));
    ruleset_load_names(&proad->name, file, sec_name);
    section_strlcpy(&road_sections[i * MAX_SECTION_LABEL], sec_name);
  } road_type_iterate_end;

  if (NULL != sec) {
    section_list_destroy(sec);
  }
}

/**************************************************************************
  Load terrain types related ruleset data
**************************************************************************/
static void load_ruleset_terrain(struct section_file *file)
{
  struct strvec *psv;
  size_t nval;
  int j;
  const char **res;
  const char *filename = secfile_name(file);
  /* char *datafile_options = */ (void)
    check_ruleset_capabilities(file, RULESET_CAPABILITIES, filename);

  /* options */
  terrain_control.may_road =
    secfile_lookup_bool_default(file, TRUE, "options.may_road");
  terrain_control.may_irrigate =
    secfile_lookup_bool_default(file, TRUE, "options.may_irrigate");
  terrain_control.may_mine =
    secfile_lookup_bool_default(file, TRUE, "options.may_mine");
  terrain_control.may_transform =
    secfile_lookup_bool_default(file, TRUE, "options.may_transform");

  /* parameters */

  terrain_control.ocean_reclaim_requirement_pct
    = secfile_lookup_int_default(file, 101,
				 "parameters.ocean_reclaim_requirement");
  terrain_control.land_channel_requirement_pct
    = secfile_lookup_int_default(file, 101,
				 "parameters.land_channel_requirement");
  terrain_control.lake_max_size
    = secfile_lookup_int_default(file, 0,
				 "parameters.lake_max_size");
  map.server.ocean_resources
    = secfile_lookup_bool_default(file, FALSE,
                                  "parameters.ocean_resources");
  terrain_control.river_move_mode =
    secfile_lookup_int_default(file, RMV_FAST_STRICT, "parameters.river_move_mode");
  terrain_control.river_defense_bonus =
    secfile_lookup_int_default(file, 50, "parameters.river_defense_bonus");
  terrain_control.river_trade_incr =
    secfile_lookup_int_default(file, 1, "parameters.river_trade_incr");

  psv = lookup_strvec(file, "parameters", "river_help_text");
  PACKET_STRVEC_COMPUTE(terrain_control.river_help_text, psv);
  if (NULL != psv) {
    strvec_destroy(psv);
  }

  terrain_control.road_superhighway_trade_bonus =
    secfile_lookup_int_default(file, 50, "parameters.road_superhighway_trade_bonus");
  output_type_iterate(o) {
    terrain_control.rail_tile_bonus[o] =
      secfile_lookup_int_default(file, 0, "parameters.rail_%s_bonus",
				 get_output_identifier(o));
    terrain_control.pollution_tile_penalty[o]
      = secfile_lookup_int_default(file, 50,
				   "parameters.pollution_%s_penalty",
				   get_output_identifier(o));
    terrain_control.fallout_tile_penalty[o]
      = secfile_lookup_int_default(file, 50,
				   "parameters.fallout_%s_penalty",
				   get_output_identifier(o));
  } output_type_iterate_end;

  /* terrain details */

  terrain_type_iterate(pterrain) {
    const char **slist;
    const int i = terrain_index(pterrain);
    const char *tsection = &terrain_sections[i * MAX_SECTION_LABEL];

    sz_strlcpy(pterrain->graphic_str,
	       secfile_lookup_str(file,"%s.graphic", tsection));
    sz_strlcpy(pterrain->graphic_alt,
	       secfile_lookup_str(file,"%s.graphic_alt", tsection));

    pterrain->identifier
      = secfile_lookup_str(file, "%s.identifier", tsection)[0];
    if ('\0' == pterrain->identifier) {
      ruleset_error(LOG_FATAL, "\"%s\" [%s] identifier missing value.",
                    filename, tsection);
    }
    if (TERRAIN_UNKNOWN_IDENTIFIER == pterrain->identifier) {
      ruleset_error(LOG_FATAL,
                    "\"%s\" [%s] cannot use '%c' as an identifier;"
                    " it is reserved for unknown terrain.",
                    filename, tsection, pterrain->identifier);
    }
    for (j = T_FIRST; j < i; j++) {
      if (pterrain->identifier == terrain_by_number(j)->identifier) {
        ruleset_error(LOG_FATAL,
                      "\"%s\" [%s] has the same identifier as [%s].",
                      filename,
                      tsection,
                      &terrain_sections[j * MAX_SECTION_LABEL]);
      }
    }

    if (!secfile_lookup_int(file, &pterrain->movement_cost,
                            "%s.movement_cost", tsection)
        || !secfile_lookup_int(file, &pterrain->defense_bonus,
                               "%s.defense_bonus", tsection)) {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }

    output_type_iterate(o) {
      pterrain->output[o]
	= secfile_lookup_int_default(file, 0, "%s.%s", tsection,
				     get_output_identifier(o));
    } output_type_iterate_end;

    res = secfile_lookup_str_vec(file, &nval, "%s.resources", tsection);
    pterrain->resources = fc_calloc(nval + 1, sizeof(*pterrain->resources));
    for (j = 0; j < nval; j++) {
      pterrain->resources[j] = lookup_resource(filename, res[j], tsection);
    }
    pterrain->resources[nval] = NULL;
    free(res);
    res = NULL;

    if (!secfile_lookup_int(file, &pterrain->road_trade_incr,
                            "%s.road_trade_incr", tsection)
        || !secfile_lookup_int(file, &pterrain->road_time,
                               "%s.road_time", tsection)) {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }

    pterrain->irrigation_result
      = lookup_terrain(file, "irrigation_result", pterrain);
    if (!secfile_lookup_int(file, &pterrain->irrigation_food_incr,
                            "%s.irrigation_food_incr", tsection)
        || !secfile_lookup_int(file, &pterrain->irrigation_time,
                               "%s.irrigation_time", tsection)) {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }

    pterrain->mining_result
      = lookup_terrain(file, "mining_result", pterrain);
    if (!secfile_lookup_int(file, &pterrain->mining_shield_incr,
                            "%s.mining_shield_incr", tsection)
        || !secfile_lookup_int(file, &pterrain->mining_time,
                               "%s.mining_time", tsection)) {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }

    pterrain->transform_result
      = lookup_terrain(file, "transform_result", pterrain);
    if (!secfile_lookup_int(file, &pterrain->transform_time,
                            "%s.transform_time", tsection)) {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }
    pterrain->rail_time
      = secfile_lookup_int_default(file, 3, "%s.rail_time", tsection);
    pterrain->clean_pollution_time
      = secfile_lookup_int_default(file, 3, "%s.clean_pollution_time", tsection);
    pterrain->clean_fallout_time
      = secfile_lookup_int_default(file, 3, "%s.clean_fallout_time", tsection);

    pterrain->warmer_wetter_result
      = lookup_terrain(file, "warmer_wetter_result", pterrain);
    pterrain->warmer_drier_result
      = lookup_terrain(file, "warmer_drier_result", pterrain);
    pterrain->cooler_wetter_result
      = lookup_terrain(file, "cooler_wetter_result", pterrain);
    pterrain->cooler_drier_result
      = lookup_terrain(file, "cooler_drier_result", pterrain);

    slist = secfile_lookup_str_vec(file, &nval, "%s.flags", tsection);
    BV_CLR_ALL(pterrain->flags);
    for (j = 0; j < nval; j++) {
      const char *sval = slist[j];
      enum terrain_flag_id flag
        = terrain_flag_id_by_name(sval, fc_strcasecmp);

      if (!terrain_flag_id_is_valid(flag)) {
        ruleset_error(LOG_FATAL, "\"%s\" [%s] has unknown flag \"%s\".",
                      filename, tsection, sval);
      } else {
	BV_SET(pterrain->flags, flag);
      }
    }
    free(slist);

    {
      enum mapgen_terrain_property mtp;
      for (mtp = mapgen_terrain_property_begin();
           mtp != mapgen_terrain_property_end();
           mtp = mapgen_terrain_property_next(mtp)) {
        pterrain->property[mtp]
          = secfile_lookup_int_default(file, 0, "%s.property_%s", tsection,
                                       mapgen_terrain_property_name(mtp));
      }
    }

    slist = secfile_lookup_str_vec(file, &nval, "%s.native_to", tsection);
    BV_CLR_ALL(pterrain->native_to);
    for (j = 0; j < nval; j++) {
      struct unit_class *class = unit_class_by_rule_name(slist[j]);

      if (!class) {
        ruleset_error(LOG_FATAL,
                      "\"%s\" [%s] is native to unknown unit class \"%s\".",
                      filename, tsection, slist[j]);
      } else {
        BV_SET(pterrain->native_to, uclass_index(class));
      }
    }
    free(slist);

    /* get terrain color */
    {
      fc_assert_ret(pterrain->rgb == NULL);
      if (!rgbcolor_load(file, &pterrain->rgb, "%s.color", tsection)) {
        ruleset_error(LOG_FATAL, "Missing terrain color definition: %s",
                      secfile_error());
      }
    }

    pterrain->helptext = lookup_strvec(file, tsection, "helptext");
  } terrain_type_iterate_end;

  /* resource details */

  resource_type_iterate(presource) {
    char identifier[MAX_LEN_NAME];
    const int i = resource_index(presource);
    const char *rsection = &resource_sections[i * MAX_SECTION_LABEL];

    output_type_iterate (o) {
      presource->output[o] =
	  secfile_lookup_int_default(file, 0, "%s.%s", rsection,
				       get_output_identifier(o));
    } output_type_iterate_end;
    sz_strlcpy(presource->graphic_str,
	       secfile_lookup_str(file,"%s.graphic", rsection));
    sz_strlcpy(presource->graphic_alt,
	       secfile_lookup_str(file,"%s.graphic_alt", rsection));

    sz_strlcpy(identifier,
	       secfile_lookup_str(file,"%s.identifier", rsection));
    presource->identifier = identifier[0];
    if (RESOURCE_NULL_IDENTIFIER == presource->identifier) {
      ruleset_error(LOG_FATAL, "\"%s\" [%s] identifier missing value.",
                    filename, rsection);
    }
    if (RESOURCE_NONE_IDENTIFIER == presource->identifier) {
      ruleset_error(LOG_FATAL,
                    "\"%s\" [%s] cannot use '%c' as an identifier;"
                    " it is reserved.",
                    filename, rsection, presource->identifier);
    }
    for (j = 0; j < i; j++) {
      if (presource->identifier == resource_by_number(j)->identifier) {
        ruleset_error(LOG_FATAL,
                      "\"%s\" [%s] has the same identifier as [%s].",
                      filename,
                      rsection,
                      &resource_sections[j * MAX_SECTION_LABEL]);
      }
    }

  } resource_type_iterate_end;

  /* base details */
  base_type_iterate(pbase) {
    BV_CLR_ALL(pbase->conflicts);
  } base_type_iterate_end;

  base_type_iterate(pbase) {
    const char *section = &base_sections[base_index(pbase) * MAX_SECTION_LABEL];
    int j;
    const char **slist;
    struct requirement_vector *reqs;
    const char *gui_str;

    pbase->buildable = secfile_lookup_bool_default(file, TRUE,
                                                  "%s.buildable", section);
    pbase->pillageable = secfile_lookup_bool_default(file, TRUE,
                                                     "%s.pillageable", section);

    sz_strlcpy(pbase->graphic_str,
               secfile_lookup_str_default(file, "-", "%s.graphic", section));
    sz_strlcpy(pbase->graphic_alt,
               secfile_lookup_str_default(file, "-",
                                          "%s.graphic_alt", section));
    sz_strlcpy(pbase->activity_gfx,
               secfile_lookup_str_default(file, "-",
                                          "%s.activity_gfx", section));

    reqs = lookup_req_list(file, section, "reqs", base_rule_name(pbase));
    requirement_vector_copy(&pbase->reqs, reqs);

    slist = secfile_lookup_str_vec(file, &nval, "%s.native_to", section);
    BV_CLR_ALL(pbase->native_to);
    for (j = 0; j < nval; j++) {
      struct unit_class *class = unit_class_by_rule_name(slist[j]);

      if (!class) {
        ruleset_error(LOG_FATAL,
                      "\"%s\" base \"%s\" is native to unknown unit class \"%s\".",
                      filename,
                      base_rule_name(pbase),
                      slist[j]);
      } else {
        BV_SET(pbase->native_to, uclass_index(class));
      }
    }
    free(slist);

    gui_str = secfile_lookup_str(file,"%s.gui_type", section);
    pbase->gui_type = base_gui_type_by_name(gui_str, fc_strcasecmp);
    if (!base_gui_type_is_valid(pbase->gui_type)) {
      ruleset_error(LOG_FATAL, "\"%s\" base \"%s\": unknown gui_type \"%s\".",
                    filename,
                    base_rule_name(pbase),
                    gui_str);
    }

    if (!secfile_lookup_int(file, &pbase->build_time,
                            "%s.build_time", section)) {
      ruleset_error(LOG_FATAL, "%s", secfile_error());
    }
    pbase->border_sq  = secfile_lookup_int_default(file, -1, "%s.border_sq",
                                                   section);
    pbase->vision_main_sq   = secfile_lookup_int_default(file, -1,
                                                         "%s.vision_main_sq",
                                                         section);
    pbase->vision_invis_sq  = secfile_lookup_int_default(file, -1,
                                                         "%s.vision_invis_sq",
                                                         section);
    pbase->defense_bonus  = secfile_lookup_int_default(file, 0,
                                                       "%s.defense_bonus",
                                                       section);

    slist = secfile_lookup_str_vec(file, &nval, "%s.flags", section);
    BV_CLR_ALL(pbase->flags);
    for (j = 0; j < nval; j++) {
      const char *sval = slist[j];
      enum base_flag_id flag = base_flag_id_by_name(sval, fc_strcasecmp);

      if (!base_flag_id_is_valid(flag)) {
        ruleset_error(LOG_FATAL, "\"%s\" base \"%s\": unknown flag \"%s\".",
                      filename,
                      base_rule_name(pbase),
                      sval);
      } else {
        BV_SET(pbase->flags, flag);
      }
    }

    free(slist);

    slist = secfile_lookup_str_vec(file, &nval, "%s.conflicts", section);
    for (j = 0; j < nval; j++) {
      const char *sval = slist[j];
      struct base_type *pbase2 = base_type_by_rule_name(sval);

      if (pbase2 == NULL) {
        ruleset_error(LOG_FATAL, "\"%s\" base \"%s\": unknown conflict base \"%s\".",
                      filename,
                      base_rule_name(pbase),
                      sval);
      } else {
        BV_SET(pbase->conflicts, base_index(pbase2));
        BV_SET(pbase2->conflicts, base_index(pbase));
      }
    }
    
    free(slist);

    if (territory_claiming_base(pbase)) {
      base_type_iterate(pbase2) {
        if (pbase == pbase2) {
          /* End of the fully initialized bases iteration. */
          break;
        }
        if (territory_claiming_base(pbase2)) {
          BV_SET(pbase->conflicts, base_index(pbase2));
          BV_SET(pbase2->conflicts, base_index(pbase));
        }
      } base_type_iterate_end;
    }

    pbase->helptext = lookup_strvec(file, section, "helptext");
  } base_type_iterate_end;

  road_type_iterate(proad) {
    const char *section = &road_sections[road_index(proad) * MAX_SECTION_LABEL];
    const char **slist;

    slist = secfile_lookup_str_vec(file, &nval, "%s.native_to", section);
    BV_CLR_ALL(proad->native_to);
    for (j = 0; j < nval; j++) {
      struct unit_class *class = unit_class_by_rule_name(slist[j]);

      if (!class) {
        ruleset_error(LOG_FATAL,
                      "\"%s\" road \"%s\" is native to unknown unit class \"%s\".",
                      filename,
                      road_rule_name(proad),
                      slist[j]);
      } else {
        BV_SET(proad->native_to, uclass_index(class));
      }
    }
    free(slist);
  } road_type_iterate_end;

  secfile_check_unused(file);
  secfile_destroy(file);
}

/**************************************************************************
  Load names of governments so other rulesets can refer to governments with
  their name.
**************************************************************************/
static void load_government_names(struct section_file *file)
{
  int nval = 0;
  struct section_list *sec;
  const char *filename = secfile_name(file);

  (void) secfile_entry_by_path(file, "datafile.description");   /* unused */

  sec = secfile_sections_by_name_prefix(file, GOVERNMENT_SECTION_PREFIX);
  if (NULL == sec || 0 == (nval = section_list_size(sec))) {
    ruleset_error(LOG_FATAL, "\"%s\": No governments?!?", filename);
  } else if(nval > G_MAGIC) {
    /* upper limit is really about 255 for 8-bit id values, but
       use G_MAGIC elsewhere as a sanity check, and should be plenty
       big enough --dwp */
    ruleset_error(LOG_FATAL, "\"%s\": Too many governments (%d, max %d)",
                  filename, nval, G_MAGIC);
  }
  governments_alloc(nval);

  /* Government names are needed early so that get_government_by_name will
   * work. */
  governments_iterate(gov) {
    const char *sec_name =
        section_name(section_list_get(sec, government_index(gov)));
    ruleset_load_names(&gov->name, file, sec_name);
  } governments_iterate_end;
  section_list_destroy(sec);
}

/**************************************************************************
  This loads information from given governments.ruleset
**************************************************************************/
static void load_ruleset_governments(struct section_file *file)
{
  struct section_list *sec;
  const char *filename = secfile_name(file);

  (void) check_ruleset_capabilities(file, RULESET_CAPABILITIES, filename);

  sec = secfile_sections_by_name_prefix(file, GOVERNMENT_SECTION_PREFIX);

  game.government_during_revolution
    = lookup_government(file, "governments.during_revolution", filename);
  game.info.government_during_revolution_id =
    government_number(game.government_during_revolution);

  /* easy ones: */
  governments_iterate(g) {
    const int i = government_index(g);
    const char *sec_name = section_name(section_list_get(sec, i));
    struct requirement_vector *reqs =
      lookup_req_list(file, sec_name, "reqs", government_rule_name(g));

    if (NULL != secfile_entry_lookup(file, "%s.ai_better", sec_name)) {
      char entry[100];

      fc_snprintf(entry, sizeof(entry), "%s.ai_better", sec_name);
      g->ai.better = lookup_government(file, entry, filename);
    } else {
      g->ai.better = NULL;
    }
    requirement_vector_copy(&g->reqs, reqs);
    
    sz_strlcpy(g->graphic_str,
               secfile_lookup_str(file, "%s.graphic", sec_name));
    sz_strlcpy(g->graphic_alt,
               secfile_lookup_str(file, "%s.graphic_alt", sec_name));

    g->helptext = lookup_strvec(file, sec_name, "helptext");
  } governments_iterate_end;


  /* titles */
  governments_iterate(g) {
    const char *sec_name =
        section_name(section_list_get(sec, government_index(g)));
    const char *male, *female;

    if (!(male = secfile_lookup_str(file, "%s.ruler_male_title", sec_name))
        || !(female = secfile_lookup_str(file, "%s.ruler_female_title",
                                         sec_name))) {
      ruleset_error(LOG_FATAL, "Lack of default ruler titles for "
                    "government \"%s\" (nb %d): %s",
                    government_rule_name(g), government_number(g),
                    secfile_error());
    } else if (NULL == government_ruler_title_new(g, NULL, male, female)) {
      ruleset_error(LOG_FATAL, "Lack of default ruler titles for "
                    "government \"%s\" (nb %d).",
                    government_rule_name(g), government_number(g));
    }
  } governments_iterate_end;

  section_list_destroy(sec);
  secfile_check_unused(file);
  secfile_destroy(file);
}

/**************************************************************************
  Send information in packet_ruleset_control (numbers of units etc, and
  other miscellany) to specified connections.

  The client assumes that exactly one ruleset control packet is sent as
  a part of each ruleset send.  So after sending this packet we have to
  resend every other part of the rulesets (and none of them should be
  is-info in the network code!).  The client frees ruleset data when
  receiving this packet and then re-initializes as it receives the
  individual ruleset packets.  See packhand.c.
**************************************************************************/
static void send_ruleset_control(struct conn_list *dest)
{
  struct packet_ruleset_control packet;

  packet = game.control;
  lsend_packet_ruleset_control(dest, &packet);
}

/****************************************************************************
  This checks if nations[pos] leader names are not already defined in any
  previous nation, or twice in its own leader name table.
  If not return NULL, if yes return pointer to name which is repeated
  and id of a conflicting nation as second parameter.
****************************************************************************/
static const char *check_leader_names(struct nation_type *pnation,
                                      struct nation_type **ppconflict_nation)
{
  nation_leader_list_iterate(nation_leaders(pnation), pleader) {
    const char *name = nation_leader_name(pleader);

    nation_leader_list_iterate(nation_leaders(pnation), prev_leader) {
      if (prev_leader == pleader) {
        break;
      } else if (0 == fc_strcasecmp(name, nation_leader_name(prev_leader))) {
        *ppconflict_nation = pnation;
        return name;
      }
    } nation_leader_list_iterate_end;
  } nation_leader_list_iterate_end;

  nations_iterate(prev_nation) {
    if (prev_nation == pnation) {
      break;
    }

    nation_leader_list_iterate(nation_leaders(prev_nation), pleader) {
      const char *name = nation_leader_name(pleader);

      nation_leader_list_iterate(nation_leaders(prev_nation), prev_leader) {
        if (prev_leader == pleader) {
          break;
        } else if (0 == fc_strcasecmp(name,
                                      nation_leader_name(prev_leader))) {
          *ppconflict_nation = prev_nation;
          return name;
        }
      } nation_leader_list_iterate_end;
    } nation_leader_list_iterate_end;
  } nations_iterate_end;
  return NULL;
}

/**************************************************************************
  Load names of nations so other rulesets can refer to nations with
  their name.
**************************************************************************/
static void load_nation_names(struct section_file *file)
{
  struct section_list *sec;
  int j;

  (void) secfile_entry_by_path(file, "datafile.description");   /* unused */

  sec = secfile_sections_by_name_prefix(file, NATION_SECTION_PREFIX);
  if (NULL == sec) {
    ruleset_error(LOG_FATAL, "No available nations in this ruleset!");
  }
  game.control.nation_count = section_list_size(sec);
  nations_alloc(game.control.nation_count);

  nations_iterate(pl) {
    const int i = nation_index(pl);
    const char *sec_name = section_name(section_list_get(sec, i));
    const char *noun_plural = secfile_lookup_str(file,
                                                 "%s.plural", sec_name);

    ruleset_load_names(&pl->adjective, file, sec_name);
    name_set(&pl->noun_plural, noun_plural);

    /* Check if nation name is already defined. */
    for(j = 0; j < i; j++) {
      struct nation_type *n2 = nation_by_number(j);

      /* Compare strings after stripping off qualifiers -- we don't want
       * two nations to end up with identical adjectives displayed to users.
       * (This check only catches English, not localisations, of course.) */
      if (0 == strcmp(Qn_(untranslated_name(&n2->adjective)),
                      Qn_(untranslated_name(&pl->adjective)))) {
        ruleset_error(LOG_FATAL,
                      "Two nations defined with the same adjective \"%s\": "
                      "in section \'%s\' and section \'%s\'",
                      Qn_(untranslated_name(&pl->adjective)),
                      section_name(section_list_get(sec, j)), sec_name);
      } else if (0 == strcmp(rule_name(&n2->adjective),
                             rule_name(&pl->adjective))) {
        /* We cannot have the same rule name, as the game needs them to be
         * distinct. */
        ruleset_error(LOG_FATAL,
                      "Two nations defined with the same rule_name \"%s\": "
                      "in section \'%s\' and section \'%s\'",
                      rule_name(&pl->adjective),
                      section_name(section_list_get(sec, j)), sec_name);
      } else if (0 == strcmp(Qn_(untranslated_name(&n2->noun_plural)),
                             Qn_(untranslated_name(&pl->noun_plural)))) {
        /* We don't want identical English plural names either. */
        ruleset_error(LOG_FATAL,
                      "Two nations defined with the same plural name \"%s\": "
                      "in section \'%s\' and section \'%s\'",
                      Qn_(untranslated_name(&pl->noun_plural)),
                      section_name(section_list_get(sec, j)), sec_name);
      }
    }
  } nations_iterate_end;
  section_list_destroy(sec);
}

/****************************************************************************
  This function loads a city name list from a section file.  The file and
  two section names (which will be concatenated) are passed in.  The
  malloc'ed city name list (which is all filled out) will be returned.
****************************************************************************/
static void load_city_name_list(struct section_file *file,
                                struct nation_type *pnation,
                                const char *secfile_str1,
                                const char *secfile_str2)
{
  size_t dim, j;
  const char **cities = secfile_lookup_str_vec(file, &dim, "%s.%s",
                                               secfile_str1, secfile_str2);

  /* Each string will be of the form "<cityname> (<label>, <label>, ...)".
   * The cityname is just the name for this city, while each "label" matches
   * a terrain type for the city (or "river"), with a preceeding ! to negate
   * it. The parentheses are optional (but necessary to have the settings,
   * of course). Our job is now to parse it. */
  for (j = 0; j < dim; j++) {
    size_t len = strlen(cities[j]);
    char city_name[len + 1], *p, *next, *end;
    struct nation_city *pncity;

    sz_strlcpy(city_name, cities[j]);

    /* Now we wish to determine values for all of the city labels. A value
     * of NCP_NONE means no preference (which is necessary so that the use
     * of this is optional); NCP_DISLIKE means the label is negated and
     * NCP_LIKE means it's labelled. Mostly the parsing just involves
     * a lot of ugly string handling... */
    if ((p = strchr(city_name, '('))) {
      *p++ = '\0';

      if (!(end = strchr(p, ')'))) {
        ruleset_error(LOG_ERROR, "\"%s\" [%s] %s: city name \"%s\" "
                      "unmatched parenthesis.", secfile_name(file),
                      secfile_str1, secfile_str2, cities[j]);
      }

      for (*end++ = '\0'; '\0' != *end; end++) {
        if (!fc_isspace(*end)) {
          ruleset_error(LOG_ERROR, "\"%s\" [%s] %s: city name \"%s\" "
                        "contains characthers after last parenthesis, "
                        "ignoring...", secfile_name(file), secfile_str1,
                        secfile_str2, cities[j]);
        }
      }
    }

    /* Build the nation_city. */
    remove_leading_trailing_spaces(city_name);
    if (check_name(city_name)) {
      /* The ruleset contains a name that is too long. This shouldn't
       * happen - if it does, the author should get immediate feedback. */
      ruleset_error(LOG_ERROR, "\"%s\" [%s] %s: city name \"%s\" "
                    "is too long; shortening it.", secfile_name(file),
                    secfile_str1, secfile_str2, city_name);
      city_name[MAX_LEN_NAME - 1] = '\0';
    }
    pncity = nation_city_new(pnation, city_name);

    if (NULL != p) {
      /* Handle the labels one at a time. */
      do {
        enum nation_city_preference prefer;

        if ((next = strchr(p, ','))) {
          *next = '\0';
        }
        remove_leading_trailing_spaces(p);

        /* The ! is used to mark a negative, which is recorded with
         * NCP_DISLIKE. Otherwise we use a NCP_LIKE.
         */
        if (*p == '!') {
          p++;
          prefer = NCP_DISLIKE;
        } else {
          prefer = NCP_LIKE;
        }

        if (0 == fc_strcasecmp(p, "river")) {
          nation_city_set_river_preference(pncity, prefer);
        } else {
          const struct terrain *pterrain = terrain_by_rule_name(p);

          if (NULL == pterrain) {
            /* Try with removing frequent trailing 's'. */
            size_t l = strlen(p);

            if (0 < l && 's' == fc_tolower(p[l - 1])) {
              p[l - 1] = '\0';
            }
            pterrain = terrain_by_rule_name(p);
          }

          if (NULL != pterrain) {
            nation_city_set_terrain_preference(pncity, pterrain, prefer);
          } else {
            /* Nation authors may use terrains like "lake" that are
             * available in the default ruleset but not in civ1/civ2.
             * In normal use we should just ignore hints for unknown
             * terrains, but nation authors may want to know about this
             * to spot typos etc. */
             log_verbose("\"%s\" [%s] %s: terrain \"%s\" not found;"
                         " skipping it.",
                         secfile_name(file), secfile_str1, secfile_str2, p);
          }
        }

        p = next ? next + 1 : NULL;
      } while (NULL != p && '\0' != *p);
    }
  }

  if (NULL != cities) {
    free(cities);
  }
}

/**************************************************************************
Load nations.ruleset file
**************************************************************************/
static void load_ruleset_nations(struct section_file *file)
{
  struct government *gov;
  int j;
  size_t dim;
  char temp_name[MAX_LEN_NAME];
  const char **vec;
  const char *name, *bad_leader;
  int barb_land_count = 0;
  int barb_sea_count = 0;
  bool warn_city_style;
  const char *filename = secfile_name(file);
  struct section_list *sec;
  int trait_count = trait_max() + 1;
  int default_traits[trait_count];
  enum trait tr;

  (void) check_ruleset_capabilities(file, RULESET_CAPABILITIES, filename);

  warn_city_style
    = secfile_lookup_bool_default(file, TRUE,
				  "compatibility.warn_city_style");

  ruleset_load_traits(default_traits, file, "default_traits", "");
  for (tr = trait_begin(); tr < trait_end(); tr = trait_next(tr)) {
    if (default_traits[tr] < 0) {
      default_traits[tr] = TRAIT_DEFAULT_VALUE;
    }
  }

  sec = secfile_sections_by_name_prefix(file, NATION_GROUP_SECTION_PREFIX);
  section_list_iterate(sec, psection) {
    struct nation_group *pgroup;

    name = secfile_lookup_str(file, "%s.name", section_name(psection));
    if (NULL == name) {
      ruleset_error(LOG_FATAL, "Error: %s", secfile_error());
    }
    pgroup = nation_group_new(name);
    if (!secfile_lookup_int(file, &j, "%s.match", section_name(psection))) {
      ruleset_error(LOG_FATAL, "Error: %s", secfile_error());
    }
    nation_group_set_match(pgroup, j);
  } section_list_iterate_end;
  section_list_destroy(sec);

  sec = secfile_sections_by_name_prefix(file, NATION_SECTION_PREFIX);
  nations_iterate(pnation) {
    struct nation_type *pconflict;
    const int i = nation_index(pnation);
    char tmp[200] = "\0";
    const char *barb_type;
    const char *sec_name = section_name(section_list_get(sec, i));

    /* Nation groups. */
    vec = secfile_lookup_str_vec(file, &dim, "%s.groups", sec_name);
    for (j = 0; j < dim; j++) {
      struct nation_group *pgroup = nation_group_by_rule_name(vec[j]);

      if (NULL != pgroup) {
        nation_group_list_append(pnation->groups, pgroup);
      } else {
        /* For nation authors, this would probably be considered an error.
         * But it can happen normally. The civ1 compatibility ruleset only
         * uses the nations that were in civ1, so not all of the links will
         * exist. */
        log_verbose("Nation %s: Unknown group \"%s\".",
                    nation_rule_name(pnation), vec[j]);
      }
    }
    if (NULL != vec) {
      free(vec);
    }

    /* Nation conflicts. */
    vec = secfile_lookup_str_vec(file, &dim, "%s.conflicts_with", sec_name);
    for (j = 0; j < dim; j++) {
      pconflict = nation_by_rule_name(vec[j]);

      if (NULL != pconflict) {
        nation_list_append(pnation->server.conflicts_with, pconflict);
      } else {
        /* For nation authors, this would probably be considered an error.
         * But it can happen normally. The civ1 compatibility ruleset only
         * uses the nations that were in civ1, so not all of the links will
         * exist. */
        log_verbose("Nation %s: conflicts_with nation \"%s\" is unknown.",
                    nation_rule_name(pnation), vec[j]);
      }
    }
    if (NULL != vec) {
      free(vec);
    }

    /* Nation leaders. */
    for (j = 0; j < MAX_NUM_LEADERS; j++) {
      const char *sex;
      bool is_male = FALSE;

      name = secfile_lookup_str(file, "%s.leaders%d.name", sec_name, j);
      if (NULL == name) {
        /* No more to read. */
        break;
      }

      if (check_name(name)) {
        /* The ruleset contains a name that is too long. This shouldn't
         * happen - if it does, the author should get immediate feedback */
        sz_strlcpy(temp_name, name);
        ruleset_error(LOG_ERROR, "Nation %s: leader name \"%s\" "
                      "is too long; shortening it to \"%s\".",
                      nation_rule_name(pnation), name, temp_name);
        name = temp_name;
      }

      sex = secfile_lookup_str(file, "%s.leaders%d.sex", sec_name, j);
      if (NULL == sex) {
        ruleset_error(LOG_FATAL, "Nation %s: leader \"%s\": %s.",
                      nation_rule_name(pnation), name, secfile_error());
      } else if (0 == fc_strcasecmp("Male", sex)) {
        is_male = TRUE;
      } else if (0 != fc_strcasecmp("Female", sex)) {
        ruleset_error(LOG_FATAL, "Nation %s: leader \"%s\" has unsupported "
                      "sex variant \"%s\".",
                      nation_rule_name(pnation), name, sex);
      }
      (void) nation_leader_new(pnation, name, is_male);
    }

    /* Check the number of leaders. */
    if (MAX_NUM_LEADERS == j) {
      /* Too much leaders, get the real number defined in the ruleset. */
      while (NULL != secfile_entry_lookup(file, "%s.leaders%d.name",
                                          sec_name, j)) {
        j++;
      }
      log_error("Nation %s: Too many leaders; using %d of %d",
                nation_rule_name(pnation), MAX_NUM_LEADERS, j);
    } else if (0 == j) {
      ruleset_error(LOG_FATAL,
                    "Nation %s: no leaders; at least one is required.",
                    nation_rule_name(pnation));
    }

    /* Check if leader name is not already defined */
    if ((bad_leader = check_leader_names(pnation, &pconflict))) {
      if (pnation == pconflict) {
        ruleset_error(LOG_FATAL,
                      "Nation %s: leader \"%s\" defined more than once.",
                      nation_rule_name(pnation), bad_leader);
      } else {
        ruleset_error(LOG_FATAL,
                      "Nations %s and %s share the same leader \"%s\".",
                      nation_rule_name(pnation), nation_rule_name(pconflict),
                      bad_leader);
      }
    }

    /* Load nation traits */
    ruleset_load_traits(pnation->server.traits, file, "%s", "trait_");
    for (tr = trait_begin(); tr < trait_end(); tr = trait_next(tr)) {
      if (pnation->server.traits[tr] < 0) {
        pnation->server.traits[tr] = default_traits[tr];
      }
    }

    pnation->is_available =
        secfile_lookup_bool_default(file, TRUE, "%s.is_available", sec_name);
    pnation->is_playable =
        secfile_lookup_bool_default(file, TRUE, "%s.is_playable", sec_name);

    if (pnation->is_playable) {
      server.playable_nations++;
    }

    /* Check barbarian type. Default is "None" meaning not a barbarian */
    barb_type = secfile_lookup_str_default(file, "None",
                                           "%s.barbarian_type", sec_name);
    if (fc_strcasecmp(barb_type, "None") == 0) {
      pnation->barb_type = NOT_A_BARBARIAN;
    } else if (fc_strcasecmp(barb_type, "Land") == 0) {
      if (pnation->is_playable) {
        /* We can't allow players to use barbarian nations, barbarians
         * may run out of nations */
        ruleset_error(LOG_FATAL,
                      "Nation %s marked both barbarian and playable.",
                      nation_rule_name(pnation));
      }
      pnation->barb_type = LAND_BARBARIAN;
      barb_land_count++;
    } else if (fc_strcasecmp(barb_type, "Sea") == 0) {
      if (pnation->is_playable) {
        /* We can't allow players to use barbarian nations, barbarians
         * may run out of nations */
        ruleset_error(LOG_FATAL,
                      "Nation %s marked both barbarian and playable.",
                      nation_rule_name(pnation));
      }
      pnation->barb_type = SEA_BARBARIAN;
      barb_sea_count++;
    } else {
      ruleset_error(LOG_FATAL,
                    "Nation %s, barbarian_type is \"%s\". Must be "
                    "\"None\" or \"Land\" or \"Sea\".",
                    nation_rule_name(pnation), barb_type);
    }

    /* Flags */
    sz_strlcpy(pnation->flag_graphic_str,
               secfile_lookup_str_default(file, "-", "%s.flag", sec_name));
    sz_strlcpy(pnation->flag_graphic_alt,
               secfile_lookup_str_default(file, "-",
                                          "%s.flag_alt", sec_name));

    /* Ruler titles */
    for (j = 0;; j++) {
      const char *male, *female;

      name = secfile_lookup_str_default(file, NULL,
                                        "%s.ruler_titles%d.government",
                                        sec_name, j);
      if (NULL == name) {
        /* End of the list of ruler titles. */
        break;
      }

      /* NB: even if the government doesn't exist, we load the entries for
       * the ruler titles to avoid warnings about unused entries. */
      male = secfile_lookup_str(file, "%s.ruler_titles%d.male_title",
                                sec_name, j);
      female = secfile_lookup_str(file, "%s.ruler_titles%d.female_title",
                                  sec_name, j);
      gov = government_by_rule_name(name);

      if (NULL == gov) {
        /* log_verbose() rather than log_error() so that can use single
         * nation ruleset file with variety of government ruleset files: */
        log_verbose("Nation %s: government \"%s\" not found.",
                    nation_rule_name(pnation), name);
      } else if (NULL != male && NULL != female) {
        (void) government_ruler_title_new(gov, pnation, male, female);
      } else {
          ruleset_error(LOG_ERROR, "%s", secfile_error());
      }
    }

    /* City styles */
    name = secfile_lookup_str(file, "%s.city_style", sec_name);
    pnation->city_style = city_style_by_rule_name(name);
    if (0 > pnation->city_style) {
      if (warn_city_style) {
	log_error("Nation %s: city style \"%s\" is unknown, using default.",
		  nation_rule_name(pnation), name);
      }
      pnation->city_style = 0;
    }

    while (city_style_has_requirements(city_styles + pnation->city_style)) {
      if (pnation->city_style == 0) {
        ruleset_error(LOG_FATAL,
                      "Nation %s: the default city style is not available "
                      "from the beginning!", nation_rule_name(pnation));
        /* Note that we can't use temp_name here. */
      }
      log_error("Nation %s: city style \"%s\" is not available "
                "from beginning; using default.",
                nation_rule_name(pnation), name);
      pnation->city_style = 0;
    }

    /* Civilwar nations */
    vec = secfile_lookup_str_vec(file, &dim,
                                 "%s.civilwar_nations", sec_name);
    for (j = 0; j < dim; j++) {
      pconflict = nation_by_rule_name(vec[j]);

      /* No test for duplicate nations is performed.  If there is a duplicate
       * entry it will just cause that nation to have an increased
       * probability of being chosen. */
      if (NULL != pconflict) {
        nation_list_append(pnation->server.civilwar_nations, pconflict);
        nation_list_append(pconflict->server.parent_nations, pnation);
      } else {
        /* For nation authors, this would probably be considered an error.
         * But it can happen normally. The civ1 compatability ruleset only
         * uses the nations that were in civ1, so not all of the links will
         * exist. */
        log_verbose("Nation %s: civil war nation \"%s\" is unknown.",
                    nation_rule_name(pnation), vec[j]);
      }
    }
    if (NULL != vec) {
      free(vec);
    }

    /* Load nation specific initial items */
    lookup_tech_list(file, sec_name, "init_techs",
                       pnation->server.init_techs, filename);
    lookup_building_list(file, sec_name, "init_buildings",
                         pnation->server.init_buildings, filename);
    lookup_unit_list(file, sec_name, "init_units", LOG_ERROR,
                     pnation->server.init_units, filename);
    fc_strlcat(tmp, sec_name, 200);
    fc_strlcat(tmp, ".init_government", 200);
    pnation->server.init_government = lookup_government(file, tmp, filename);

    /* Read default city names. */
    load_city_name_list(file, pnation, sec_name, "cities");

    pnation->legend = fc_strdup(secfile_lookup_str(file, "%s.legend",
                                                   sec_name));
    if (check_strlen(pnation->legend, MAX_LEN_MSG, NULL)) {
      ruleset_error(LOG_ERROR,
                    "Nation %s: legend \"%s\" is too long;"
                    " shortening it.", nation_rule_name(pnation),
                    pnation->legend);
      pnation->legend[MAX_LEN_MSG - 1] = '\0';
    }

    pnation->player = NULL;
  } nations_iterate_end;

  section_list_destroy(sec);
  secfile_check_unused(file);
  secfile_destroy(file);

  if (barb_land_count == 0) {
    ruleset_error(LOG_FATAL,
                  "No land barbarian nation defined. At least one required!");
  }
  if (barb_sea_count == 0) {
    ruleset_error(LOG_FATAL,
                  "No sea barbarian nation defined. At least one required!");
  }
}

/**************************************************************************
  Load names of city styles so other rulesets can refer to city styles with
  their name.
**************************************************************************/
static void load_citystyle_names(struct section_file *file)
{
  struct section_list *styles;
  int i = 0;

  (void) secfile_entry_by_path(file, "datafile.description");   /* unused */

  /* The sections: */
  styles = secfile_sections_by_name_prefix(file, CITYSTYLE_SECTION_PREFIX);
  if (NULL != styles) {
    city_styles_alloc(section_list_size(styles));
    section_list_iterate(styles, style) {
      ruleset_load_names(&city_styles[i].name, file, section_name(style));
      i++;
    } section_list_iterate_end;
    section_list_destroy(styles);
  } else {
    city_styles_alloc(0);
  }
}

/**************************************************************************
Load cities.ruleset file
**************************************************************************/
static void load_ruleset_cities(struct section_file *file)
{
  const char *replacement;
  int i;
  const char *filename = secfile_name(file);
  const char *item;
  struct section_list *sec;

  (void) check_ruleset_capabilities(file, RULESET_CAPABILITIES, filename);

  /* Specialist options */
  sec = secfile_sections_by_name_prefix(file, SPECIALIST_SECTION_PREFIX);
  if (section_list_size(sec) >= SP_MAX) {
    ruleset_error(LOG_FATAL, "\"%s\": Too many specialists (%d, max %d).",
                  filename, section_list_size(sec), SP_MAX);
  }
  game.control.num_specialist_types = section_list_size(sec);

  i = 0;
  section_list_iterate(sec, psection) {
    struct specialist *s = specialist_by_number(i);
    struct requirement_vector *reqs;
    const char *sec_name = section_name(psection);

    ruleset_load_names(&s->name, file, sec_name);

    item = secfile_lookup_str_default(file, untranslated_name(&s->name),
                                      "%s.short_name", sec_name);
    name_set(&s->abbreviation, item);

    reqs = lookup_req_list(file, sec_name, "reqs", specialist_rule_name(s));
    requirement_vector_copy(&s->reqs, reqs);

    s->helptext = lookup_strvec(file, sec_name, "helptext");

    if (requirement_vector_size(&s->reqs) == 0 && DEFAULT_SPECIALIST == -1) {
      DEFAULT_SPECIALIST = i;
    }
    i++;
  } section_list_iterate_end;
  if (DEFAULT_SPECIALIST == -1) {
    ruleset_error(LOG_FATAL,
                  "\"%s\": must give a min_size of 0 for at least one "
                  "specialist type.", filename);
  }
  section_list_destroy(sec);

  /* City Parameters */

  game.info.celebratesize = 
    secfile_lookup_int_default(file, GAME_DEFAULT_CELEBRATESIZE,
                               "parameters.celebrate_size_limit");
  game.info.add_to_size_limit =
    secfile_lookup_int_default(file, 9, "parameters.add_to_size_limit");
  game.info.angrycitizen =
    secfile_lookup_bool_default(file, GAME_DEFAULT_ANGRYCITIZEN,
                                "parameters.angry_citizens");

  game.info.changable_tax = 
    secfile_lookup_bool_default(file, TRUE, "parameters.changable_tax");
  game.info.forced_science = 
    secfile_lookup_int_default(file, 0, "parameters.forced_science");
  game.info.forced_luxury = 
    secfile_lookup_int_default(file, 100, "parameters.forced_luxury");
  game.info.forced_gold = 
    secfile_lookup_int_default(file, 0, "parameters.forced_gold");
  if (game.info.forced_science + game.info.forced_luxury
      + game.info.forced_gold != 100) {
    ruleset_error(LOG_FATAL,
                  "\"%s\": Forced taxes do not add up in ruleset!",
                  filename);
  }

  /* civ1 & 2 didn't reveal tiles */
  game.server.vision_reveal_tiles =
    secfile_lookup_bool_default(file, FALSE, "parameters.vision_reveal_tiles");

  /* Citizens configuration. */
  game.info.citizen_nationality =
    secfile_lookup_bool_default(file, FALSE,
                                "citizen.nationality");
  /* City Styles ... */

  sec = secfile_sections_by_name_prefix(file, CITYSTYLE_SECTION_PREFIX);

  /* Get rest: */
  for (i = 0; i < game.control.styles_count; i++) {
    struct requirement_vector *reqs;
    const char *sec_name = section_name(section_list_get(sec, i));

    sz_strlcpy(city_styles[i].graphic, 
	       secfile_lookup_str(file, "%s.graphic", sec_name));
    sz_strlcpy(city_styles[i].graphic_alt, 
	       secfile_lookup_str(file, "%s.graphic_alt", sec_name));
    sz_strlcpy(city_styles[i].oceanic_graphic, 
	       secfile_lookup_str_default(file, "",
                                          "%s.oceanic_graphic", sec_name));
    sz_strlcpy(city_styles[i].oceanic_graphic_alt, 
	       secfile_lookup_str_default(file, "",
                                          "%s.oceanic_graphic_alt",
                                          sec_name));
    sz_strlcpy(city_styles[i].citizens_graphic,
	       secfile_lookup_str_default(file, "-", 
	    		"%s.citizens_graphic", sec_name));
    sz_strlcpy(city_styles[i].citizens_graphic_alt, 
	       secfile_lookup_str_default(file, "generic", 
	    		"%s.citizens_graphic_alt", sec_name));

    reqs = lookup_req_list(file, sec_name, "reqs", city_style_rule_name(i));
    requirement_vector_copy(&city_styles[i].reqs, reqs);

    replacement = secfile_lookup_str(file, "%s.replaced_by", sec_name);
    if(0 == strcmp(replacement, "-")) {
      city_styles[i].replaced_by = -1;
    } else {
      city_styles[i].replaced_by = city_style_by_rule_name(replacement);
      if (city_styles[i].replaced_by < 0) {
        log_error("\"%s\": style \"%s\" replacement \"%s\" not found",
                  filename, city_style_rule_name(i), replacement);
      }
    }
  }
  section_list_destroy(sec);

  secfile_check_unused(file);
  secfile_destroy(file);
}

/**************************************************************************
Load effects.ruleset file
**************************************************************************/
static void load_ruleset_effects(struct section_file *file)
{
  struct section_list *sec;
  const char *type;
  const char *filename;

  filename = secfile_name(file);
  (void) check_ruleset_capabilities(file, RULESET_CAPABILITIES, filename);
  (void) secfile_entry_by_path(file, "datafile.description");   /* unused */

  /* Parse effects and add them to the effects ruleset cache. */
  sec = secfile_sections_by_name_prefix(file, EFFECT_SECTION_PREFIX);
  section_list_iterate(sec, psection) {
    enum effect_type eff;
    int value;
    struct effect *peffect;
    const char *sec_name = section_name(psection);

    type = secfile_lookup_str(file, "%s.name", sec_name);
    if (!type) {
      log_error("\"%s\" [%s] missing effect name.", filename, sec_name);
      continue;
    }

    eff = effect_type_by_name(type, fc_strcasecmp);
    if (!effect_type_is_valid(eff)) {
      log_error("\"%s\" [%s] lists unknown effect type \"%s\".",
                filename, sec_name, type);
      continue;
    }

    value = secfile_lookup_int_default(file, 1, "%s.value", sec_name);

    peffect = effect_new(eff, value);

    requirement_vector_iterate(lookup_req_list(file, sec_name, "reqs", type),
                               req) {
      struct requirement *preq = fc_malloc(sizeof(*preq));

      *preq = *req;
      effect_req_append(peffect, FALSE, preq);
    } requirement_vector_iterate_end;
    requirement_vector_iterate(lookup_req_list(file, sec_name,
                                               "nreqs", type),
                               req) {
      struct requirement *preq = fc_malloc(sizeof(*preq));

      *preq = *req;
      effect_req_append(peffect, TRUE, preq);
    } requirement_vector_iterate_end;
  } section_list_iterate_end;
  section_list_destroy(sec);

  secfile_check_unused(file);
  secfile_destroy(file);
}

/**************************************************************************
  Print an error message if the value is out of range.
**************************************************************************/
static int secfile_lookup_int_default_min_max(struct section_file *file,
                                              int def, int min, int max,
                                              const char *path, ...)
                                              fc__attribute((__format__ (__printf__, 5, 6)));
static int secfile_lookup_int_default_min_max(struct section_file *file,
                                              int def, int min, int max,
                                              const char *path, ...)
{
  char fullpath[256];
  int ival;
  va_list args;

  va_start(args, path);
  fc_vsnprintf(fullpath, sizeof(fullpath), path, args);
  va_end(args);

  if (!secfile_lookup_int(file, &ival, "%s", fullpath)) {
    ival = def;
  }

  if (ival < min) {
    ruleset_error(LOG_ERROR,"\"%s\" should be in the interval [%d, %d] "
                  "but is %d; using the minimal value.",
                  fullpath, min, max, ival);
    ival = min;
  }

  if (ival > max) {
    ruleset_error(LOG_ERROR,"\"%s\" should be in the interval [%d, %d] "
                  "but is %d; using the maximal value.",
                  fullpath, min, max, ival);
    ival = max;
  }

  return ival;
}

/**************************************************************************
  Load ruleset file.
**************************************************************************/
static void load_ruleset_game(void)
{
  struct section_file *file;
  const char *sval, **svec;
  const char *filename;
  int *food_ini;
  int i;
  size_t teams;
  const char *text;
  size_t gni_tmp;

  file = openload_ruleset_file("game");
  filename = secfile_name(file);

  /* section: datafile */
  (void) check_ruleset_capabilities(file, RULESET_CAPABILITIES, filename);
  (void) secfile_entry_by_path(file, "datafile.description");   /* unused */

  /* section: tileset */
  text = secfile_lookup_str_default(file, "", "tileset.prefered");
  if (text[0] != '\0') {
    /* There was tileset suggestion */
    sz_strlcpy(game.control.prefered_tileset, text);
  } else {
    /* No tileset suggestions */
    game.control.prefered_tileset[0] = '\0';
  }

  /* section: about */
  text = secfile_lookup_str(file, "about.name");
  /* Ruleset/modpack name found */
  sz_strlcpy(game.control.name, text);

  text = secfile_lookup_str_default(file, "", "about.description");
  if (text[0] != '\0') {
    /* Ruleset/modpack description found */
    sz_strlcpy(game.control.description, text);
  } else {
    /* No description */
    game.control.description[0] = '\0';
  }

  /* section: options */
  lookup_tech_list(file, "options", "global_init_techs",
                   game.server.rgame.global_init_techs, filename);
  lookup_building_list(file, "options", "global_init_buildings",
                       game.server.rgame.global_init_buildings, filename);

  /* section: civstyle */
  game.info.base_pollution
    = secfile_lookup_int_default(file, RS_DEFAULT_BASE_POLLUTION,
                                 "civstyle.base_pollution");
  game.info.happy_cost
    = secfile_lookup_int_def_min_max(file,
                                                                         RS_DEFAULT_HAPPY_COST,
                                     RS_MIN_HAPPY_COST,
                                     RS_MAX_HAPPY_COST,
                                     "civstyle.happy_cost");
  game.info.food_cost
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_FOOD_COST,
                                         RS_MIN_FOOD_COST,
                                         RS_MAX_FOOD_COST,
                                         "civstyle.food_cost");
  /* TODO: move to global_unit_options */
  game.info.base_bribe_cost
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_BASE_BRIBE_COST,
                                         RS_MIN_BASE_BRIBE_COST,
                                         RS_MAX_BASE_BRIBE_COST,
                                         "civstyle.base_bribe_cost");
  /* TODO: move to global_unit_options */
  game.server.ransom_gold
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_RANSOM_GOLD,
                                         RS_MIN_RANSOM_GOLD,
                                         RS_MAX_RANSOM_GOLD,
                                         "civstyle.ransom_gold");
  /* TODO: move to global_unit_options */
  game.info.pillage_select
    = secfile_lookup_bool_default(file, RS_DEFAULT_PILLAGE_SELECT,
                                  "civstyle.pillage_select");
  /* TODO: move to global_unit_options */
  game.server.upgrade_veteran_loss
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_UPGRADE_VETERAN_LOSS,
                                         RS_MIN_UPGRADE_VETERAN_LOSS,
                                         RS_MAX_UPGRADE_VETERAN_LOSS,
                                         "civstyle.upgrade_veteran_loss");
  /* TODO: move to global_unit_options */
  game.server.autoupgrade_veteran_loss
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_UPGRADE_VETERAN_LOSS,
                                         RS_MIN_UPGRADE_VETERAN_LOSS,
                                         RS_MAX_UPGRADE_VETERAN_LOSS,
                                         "civstyle.autoupgrade_veteran_loss");
  /* TODO: move to new section research */
  game.info.base_tech_cost
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_BASE_TECH_COST,
                                         RS_MIN_BASE_TECH_COST,
                                         RS_MAX_BASE_TECH_COST,
                                         "civstyle.base_tech_cost");

  food_ini = secfile_lookup_int_vec(file, &gni_tmp,
                                    "civstyle.granary_food_ini");
  game.info.granary_num_inis = (int) gni_tmp;

  if (game.info.granary_num_inis > MAX_GRANARY_INIS) {
    ruleset_error(LOG_FATAL,
                  "Too many granary_food_ini entries (%d, max %d)",
                  game.info.granary_num_inis, MAX_GRANARY_INIS);
  } else if (game.info.granary_num_inis == 0) {
    log_error("No values for granary_food_ini. Using default "
              "value %d.", RS_DEFAULT_GRANARY_FOOD_INI);
    game.info.granary_num_inis = 1;
    game.info.granary_food_ini[0] = RS_DEFAULT_GRANARY_FOOD_INI;
  } else {
    int i;

    /* check for <= 0 entries */
    for (i = 0; i < game.info.granary_num_inis; i++) {
      if (food_ini[i] <= 0) {
        if (i == 0) {
          food_ini[i] = RS_DEFAULT_GRANARY_FOOD_INI;
        } else {
          food_ini[i] = food_ini[i - 1];
        }
        log_error("Bad value for granary_food_ini[%i]. Using %i.",
                  i, food_ini[i]);
      }
      game.info.granary_food_ini[i] = food_ini[i];
    }
  }
  free(food_ini);

  game.info.granary_food_inc
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_GRANARY_FOOD_INC,
                                         RS_MIN_GRANARY_FOOD_INC,
                                         RS_MAX_GRANARY_FOOD_INC,
                                         "civstyle.granary_food_inc");

  output_type_iterate(o) {
    game.info.min_city_center_output[o]
      = secfile_lookup_int_default_min_max(file,
                                           RS_DEFAULT_CITY_CENTER_OUTPUT,
                                           RS_MIN_CITY_CENTER_OUTPUT,
                                           RS_MAX_CITY_CENTER_OUTPUT,
                                           "civstyle.min_city_center_%s",
                                           get_output_identifier(o));
  } output_type_iterate_end;

  sval = secfile_lookup_str(file, "civstyle.nuke_contamination" );
  if (fc_strcasecmp(sval, "Pollution") == 0) {
    game.server.nuke_contamination = CONTAMINATION_POLLUTION;
  } else if (fc_strcasecmp(sval, "Fallout") == 0) {
    game.server.nuke_contamination = CONTAMINATION_FALLOUT;
  } else {
    log_error("Bad value %s for nuke_contamination. Using "
              "\"Pollution\".", sval);
    game.server.nuke_contamination = CONTAMINATION_POLLUTION;
  }

  game.server.init_vis_radius_sq
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_VIS_RADIUS_SQ,
                                         RS_MIN_VIS_RADIUS_SQ,
                                         RS_MAX_VIS_RADIUS_SQ,
                                         "civstyle.init_vis_radius_sq");

  game.info.init_city_radius_sq
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_CITY_RADIUS_SQ,
                                         RS_MIN_CITY_RADIUS_SQ,
                                         RS_MAX_CITY_RADIUS_SQ,
                                         "civstyle.init_city_radius_sq");

  game.info.gold_upkeep_style
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_GOLD_UPKEEP_STYLE,
                                         RS_MIN_GOLD_UPKEEP_STYLE,
                                         RS_MAX_GOLD_UPKEEP_STYLE,
                                         "civstyle.gold_upkeep_style");

  /* TODO: move to new section research */
  game.info.tech_cost_style
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_TECH_COST_STYLE,
                                         RS_MIN_TECH_COST_STYLE,
                                         RS_MAX_TECH_COST_STYLE,
                                         "civstyle.tech_cost_style");
  /* TODO: move to new section research */
  game.info.tech_leakage
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_TECH_LEAKAGE,
                                         RS_MIN_TECH_LEAKAGE,
                                         RS_MAX_TECH_LEAKAGE,
                                         "civstyle.tech_leakage");
  if (game.info.tech_cost_style == 0 && game.info.tech_leakage != 0) {
    log_error("Only tech_leakage 0 supported with tech_cost_style 0.");
    log_error("Switching to tech_leakage 0.");
    game.info.tech_leakage = 0;
  }

  /* section: illness */
  game.info.illness_on
    = secfile_lookup_bool_default(file, RS_DEFAULT_ILLNESS_ON,
                                  "illness.illness_on");
  game.info.illness_base_factor
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_ILLNESS_BASE_FACTOR,
                                         RS_MIN_ILLNESS_BASE_FACTOR,
                                         RS_MAX_ILLNESS_BASE_FACTOR,
                                         "illness.illness_base_factor");
  game.info.illness_min_size
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_ILLNESS_MIN_SIZE,
                                         RS_MIN_ILLNESS_MIN_SIZE,
                                         RS_MAX_ILLNESS_MIN_SIZE,
                                         "illness.illness_min_size");
  game.info.illness_trade_infection
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_ILLNESS_TRADE_INFECTION_PCT,
                                         RS_MIN_ILLNESS_TRADE_INFECTION_PCT,
                                         RS_MAX_ILLNESS_TRADE_INFECTION_PCT,
                                         "illness.illness_trade_infection");
  game.info.illness_pollution_factor
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_ILLNESS_POLLUTION_PCT,
                                         RS_MIN_ILLNESS_POLLUTION_PCT,
                                         RS_MAX_ILLNESS_POLLUTION_PCT,
                                         "illness.illness_pollution_factor");

  /* section: incite_cost */
  game.server.base_incite_cost
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_INCITE_BASE_COST,
                                         RS_MIN_INCITE_BASE_COST,
                                         RS_MAX_INCITE_BASE_COST,
                                         "incite_cost.base_incite_cost");
  game.server.incite_improvement_factor
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_INCITE_IMPROVEMENT_FCT,
                                         RS_MIN_INCITE_IMPROVEMENT_FCT,
                                         RS_MAX_INCITE_IMPROVEMENT_FCT,
                                         "incite_cost.improvement_factor");
  game.server.incite_unit_factor
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_INCITE_UNIT_FCT,
                                         RS_MIN_INCITE_UNIT_FCT,
                                         RS_MAX_INCITE_UNIT_FCT,
                                         "incite_cost.unit_factor");
  game.server.incite_total_factor
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_INCITE_TOTAL_FCT,
                                         RS_MIN_INCITE_TOTAL_FCT,
                                         RS_MAX_INCITE_TOTAL_FCT,
                                         "incite_cost.total_factor");

  /* section: global_unit_options */
  game.info.slow_invasions
    = secfile_lookup_bool_default(file, RS_DEFAULT_SLOW_INVASIONS,
                                  "global_unit_options.slow_invasions");

  /* section: combat_rules */
  game.info.killstack
    = secfile_lookup_bool_default(file, RS_DEFAULT_KILLSTACK,
                                  "combat_rules.killstack");
  game.info.tired_attack
    = secfile_lookup_bool_default(file, RS_DEFAULT_TIRED_ATTACK,
                                  "combat_rules.tired_attack");

  /* section: borders */
  game.info.border_city_radius_sq
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_BORDER_RADIUS_SQ_CITY,
                                         RS_MIN_BORDER_RADIUS_SQ_CITY,
                                         RS_MAX_BORDER_RADIUS_SQ_CITY,
                                         "borders.radius_sq_city");
  game.info.border_size_effect
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_BORDER_SIZE_EFFECT,
                                         RS_MIN_BORDER_SIZE_EFFECT,
                                         RS_MAX_BORDER_SIZE_EFFECT,
                                         "borders.size_effect");

  /* section: research */
  game.info.tech_upkeep_style
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_TECH_UPKEEP_STYLE,
                                         RS_MIN_TECH_UPKEEP_STYLE,
                                         RS_MAX_TECH_UPKEEP_STYLE,
                                         "research.tech_upkeep_style");
  game.info.tech_upkeep_divider
    = secfile_lookup_int_default_min_max(file,
                                         RS_DEFAULT_TECH_UPKEEP_DIVIDER,
                                         RS_MIN_TECH_UPKEEP_DIVIDER,
                                         RS_MAX_TECH_UPKEEP_DIVIDER,
                                         "research.tech_upkeep_divider");

  /* section: calendar */
  game.info.calendar_skip_0
    = secfile_lookup_bool_default(file, RS_DEFAULT_CALENDAR_SKIP_0,
                                  "calendar.skip_year_0");
  game.server.start_year
    = secfile_lookup_int_default(file, GAME_START_YEAR,
                                 "calendar.start_year");
  sz_strlcpy(game.info.positive_year_label,
             _(secfile_lookup_str_default(file,
                                          RS_DEFAULT_POS_YEAR_LABEL,
                                          "calendar.positive_label")));
  sz_strlcpy(game.info.negative_year_label,
             _(secfile_lookup_str_default(file,
                                          RS_DEFAULT_NEG_YEAR_LABEL,
                                          "calendar.negative_label")));

  /* section playercolors */
  {
    struct rgbcolor *prgbcolor = NULL;
    bool read = TRUE;

    /* Check if the player list is defined and empty. */
    fc_assert_ret(playercolor_count() == 0);
    i = 0;
    while (read) {
      prgbcolor = NULL;

      read = rgbcolor_load(file, &prgbcolor, "playercolors.colorlist%d", i);
      if (read) {
        playercolor_add(prgbcolor);
      }

      i++;
    }

    if (playercolor_count() == 0) {
      ruleset_error(LOG_FATAL, "No player colors defined!");
    }

    if (game.plr_bg_color != NULL) {
      rgbcolor_destroy(game.plr_bg_color);
      game.plr_bg_color = NULL;
    }
    if (!rgbcolor_load(file, &game.plr_bg_color, "playercolors.background")) {
      ruleset_error(LOG_FATAL, "No background player color defined! (%s)",
                    secfile_error());
    }
  }

  /* section: teams */
  svec = secfile_lookup_str_vec(file, &teams, "teams.names");
  if (team_slot_count() < teams) {
    teams = team_slot_count();
  }
  for (i = 0; i < teams; i++) {
    team_slot_set_defined_name(team_slot_by_number(i), svec[i]);
  }
  free(svec);

  settings_ruleset(file, "settings");

  secfile_check_unused(file);
  secfile_destroy(file);
}

/**************************************************************************
  Send the units ruleset information (all individual unit classes) to the
  specified connections.
**************************************************************************/
static void send_ruleset_unit_classes(struct conn_list *dest)
{
  struct packet_ruleset_unit_class packet;

  unit_class_iterate(c) {
    packet.id = uclass_number(c);
    sz_strlcpy(packet.name, untranslated_name(&c->name));
    sz_strlcpy(packet.rule_name, rule_name(&c->name));
    packet.move_type = c->move_type;
    packet.min_speed = c->min_speed;
    packet.hp_loss_pct = c->hp_loss_pct;
    packet.hut_behavior = c->hut_behavior;
    packet.flags = c->flags;

    lsend_packet_ruleset_unit_class(dest, &packet);
  } unit_class_iterate_end;
}

/**************************************************************************
  Send the units ruleset information (all individual units) to the
  specified connections.
**************************************************************************/
static void send_ruleset_units(struct conn_list *dest)
{
  struct packet_ruleset_unit packet;
  int i;

  unit_type_iterate(u) {
    packet.id = utype_number(u);
    sz_strlcpy(packet.name, untranslated_name(&u->name));
    sz_strlcpy(packet.rule_name, rule_name(&u->name));
    sz_strlcpy(packet.sound_move, u->sound_move);
    sz_strlcpy(packet.sound_move_alt, u->sound_move_alt);
    sz_strlcpy(packet.sound_fight, u->sound_fight);
    sz_strlcpy(packet.sound_fight_alt, u->sound_fight_alt);
    sz_strlcpy(packet.graphic_str, u->graphic_str);
    sz_strlcpy(packet.graphic_alt, u->graphic_alt);
    packet.unit_class_id = uclass_number(utype_class(u));
    packet.build_cost = u->build_cost;
    packet.pop_cost = u->pop_cost;
    packet.attack_strength = u->attack_strength;
    packet.defense_strength = u->defense_strength;
    packet.move_rate = u->move_rate;
    packet.tech_requirement = u->require_advance
                              ? advance_number(u->require_advance) : -1;
    packet.impr_requirement = u->need_improvement
                              ? improvement_number(u->need_improvement) : -1;
    packet.gov_requirement = u->need_government
                             ? government_number(u->need_government) : -1;
    packet.vision_radius_sq = u->vision_radius_sq;
    packet.transport_capacity = u->transport_capacity;
    packet.hp = u->hp;
    packet.firepower = u->firepower;
    packet.obsoleted_by = u->obsoleted_by
                          ? utype_number(u->obsoleted_by) : -1;
    packet.converted_to = u->converted_to
                          ? utype_number(u->converted_to) : -1;
    packet.fuel = u->fuel;
    packet.flags = u->flags;
    packet.roles = u->roles;
    packet.happy_cost = u->happy_cost;
    output_type_iterate(o) {
      packet.upkeep[o] = u->upkeep[o];
    } output_type_iterate_end;
    packet.paratroopers_range = u->paratroopers_range;
    packet.paratroopers_mr_req = u->paratroopers_mr_req;
    packet.paratroopers_mr_sub = u->paratroopers_mr_sub;
    packet.bombard_rate = u->bombard_rate;
    packet.city_size = u->city_size;
    packet.cargo = u->cargo;
    packet.targets = u->targets;

    if (u->veteran == NULL) {
      /* Use the default veteran system. */
      packet.veteran_levels = 0;
    } else {
      /* Per unit veteran system definition. */
      packet.veteran_levels = utype_veteran_levels(u);

      for (i = 0; i < packet.veteran_levels; i++) {
        const struct veteran_level *vlevel = utype_veteran_level(u, i);

        sz_strlcpy(packet.veteran_name[i], untranslated_name(&vlevel->name));
        packet.power_fact[i] = vlevel->power_fact;
        packet.move_bonus[i] = vlevel->move_bonus;
      }
    }
    PACKET_STRVEC_COMPUTE(packet.helptext, u->helptext);

    lsend_packet_ruleset_unit(dest, &packet);
  } unit_type_iterate_end;
}

/**************************************************************************
  Send the specialists ruleset information (all individual specialist
  types) to the specified connections.
**************************************************************************/
static void send_ruleset_specialists(struct conn_list *dest)
{
  struct packet_ruleset_specialist packet;

  specialist_type_iterate(spec_id) {
    struct specialist *s = specialist_by_number(spec_id);
    int j;

    packet.id = spec_id;
    sz_strlcpy(packet.plural_name, untranslated_name(&s->name));
    sz_strlcpy(packet.rule_name, rule_name(&s->name));
    sz_strlcpy(packet.short_name, untranslated_name(&s->abbreviation));
    j = 0;
    requirement_vector_iterate(&s->reqs, preq) {
      packet.reqs[j++] = *preq;
    } requirement_vector_iterate_end;
    packet.reqs_count = j;

    PACKET_STRVEC_COMPUTE(packet.helptext, s->helptext);

    lsend_packet_ruleset_specialist(dest, &packet);
  } specialist_type_iterate_end;
}

/**************************************************************************
  Send the techs ruleset information (all individual advances) to the
  specified connections.
**************************************************************************/
static void send_ruleset_techs(struct conn_list *dest)
{
  struct packet_ruleset_tech packet;

  advance_iterate(A_NONE, a) {
    packet.id = advance_number(a);
    sz_strlcpy(packet.name, untranslated_name(&a->name));
    sz_strlcpy(packet.rule_name, rule_name(&a->name));
    sz_strlcpy(packet.graphic_str, a->graphic_str);
    sz_strlcpy(packet.graphic_alt, a->graphic_alt);

    packet.req[AR_ONE] = a->require[AR_ONE]
                         ? advance_number(a->require[AR_ONE]) : -1;
    packet.req[AR_TWO] = a->require[AR_TWO]
                         ? advance_number(a->require[AR_TWO]) : -1;
    packet.root_req = a->require[AR_ROOT]
                      ? advance_number(a->require[AR_ROOT]) : -1;

    packet.flags = a->flags;
    packet.preset_cost = a->preset_cost;
    packet.num_reqs = a->num_reqs;
    PACKET_STRVEC_COMPUTE(packet.helptext, a->helptext);

    lsend_packet_ruleset_tech(dest, &packet);
  } advance_iterate_end;
}

/**************************************************************************
  Send the buildings ruleset information (all individual improvements and
  wonders) to the specified connections.
**************************************************************************/
static void send_ruleset_buildings(struct conn_list *dest)
{
  improvement_iterate(b) {
    struct packet_ruleset_building packet;
    int j;

    packet.id = improvement_number(b);
    packet.genus = b->genus;
    sz_strlcpy(packet.name, untranslated_name(&b->name));
    sz_strlcpy(packet.rule_name, rule_name(&b->name));
    sz_strlcpy(packet.graphic_str, b->graphic_str);
    sz_strlcpy(packet.graphic_alt, b->graphic_alt);
    j = 0;
    requirement_vector_iterate(&b->reqs, preq) {
      packet.reqs[j++] = *preq;
    } requirement_vector_iterate_end;
    packet.reqs_count = j;
    packet.obsolete_by = b->obsolete_by
                         ? advance_number(b->obsolete_by) : -1;
    packet.replaced_by = b->replaced_by
                         ? improvement_number(b->replaced_by) : -1;
    packet.build_cost = b->build_cost;
    packet.upkeep = b->upkeep;
    packet.sabotage = b->sabotage;
    packet.flags = b->flags;
    sz_strlcpy(packet.soundtag, b->soundtag);
    sz_strlcpy(packet.soundtag_alt, b->soundtag_alt);
    PACKET_STRVEC_COMPUTE(packet.helptext, b->helptext);

    lsend_packet_ruleset_building(dest, &packet);
  } improvement_iterate_end;
}

/**************************************************************************
  Send the terrain ruleset information (terrain_control, and the individual
  terrain types) to the specified connections.
**************************************************************************/
static void send_ruleset_terrain(struct conn_list *dest)
{
  struct packet_ruleset_terrain packet;

  lsend_packet_ruleset_terrain_control(dest, &terrain_control);

  terrain_type_iterate(pterrain) {
    struct resource **r;

    packet.id = terrain_number(pterrain);
    packet.native_to = pterrain->native_to;

    sz_strlcpy(packet.name, untranslated_name(&pterrain->name));
    sz_strlcpy(packet.rule_name, rule_name(&pterrain->name));
    sz_strlcpy(packet.graphic_str, pterrain->graphic_str);
    sz_strlcpy(packet.graphic_alt, pterrain->graphic_alt);

    packet.movement_cost = pterrain->movement_cost;
    packet.defense_bonus = pterrain->defense_bonus;

    output_type_iterate(o) {
      packet.output[o] = pterrain->output[o];
    } output_type_iterate_end;

    packet.num_resources = 0;
    for (r = pterrain->resources; *r; r++) {
      packet.resources[packet.num_resources++] = resource_number(*r);
    }

    packet.road_trade_incr = pterrain->road_trade_incr;
    packet.road_time = pterrain->road_time;

    packet.irrigation_result = (pterrain->irrigation_result
				? terrain_number(pterrain->irrigation_result)
				: terrain_count());
    packet.irrigation_food_incr = pterrain->irrigation_food_incr;
    packet.irrigation_time = pterrain->irrigation_time;

    packet.mining_result = (pterrain->mining_result
			    ? terrain_number(pterrain->mining_result)
			    : terrain_count());
    packet.mining_shield_incr = pterrain->mining_shield_incr;
    packet.mining_time = pterrain->mining_time;

    packet.transform_result = (pterrain->transform_result
			       ? terrain_number(pterrain->transform_result)
			       : terrain_count());
    packet.transform_time = pterrain->transform_time;
    packet.rail_time = pterrain->rail_time;
    packet.clean_pollution_time = pterrain->clean_pollution_time;
    packet.clean_fallout_time = pterrain->clean_fallout_time;

    packet.flags = pterrain->flags;

    packet.color_red = pterrain->rgb->r;
    packet.color_green = pterrain->rgb->g;
    packet.color_blue = pterrain->rgb->b;

    PACKET_STRVEC_COMPUTE(packet.helptext, pterrain->helptext);

    lsend_packet_ruleset_terrain(dest, &packet);
  } terrain_type_iterate_end;
}

/****************************************************************************
  Send the resource ruleset information to the specified connections.
****************************************************************************/
static void send_ruleset_resources(struct conn_list *dest)
{
  struct packet_ruleset_resource packet;

  resource_type_iterate (presource) {
    packet.id = resource_number(presource);

    sz_strlcpy(packet.name, untranslated_name(&presource->name));
    sz_strlcpy(packet.rule_name, rule_name(&presource->name));
    sz_strlcpy(packet.graphic_str, presource->graphic_str);
    sz_strlcpy(packet.graphic_alt, presource->graphic_alt);

    output_type_iterate(o) {
      packet.output[o] = presource->output[o];
    } output_type_iterate_end;

    lsend_packet_ruleset_resource(dest, &packet);
  } resource_type_iterate_end;
}

/**************************************************************************
  Send the base ruleset information (all individual base types) to the
  specified connections.
**************************************************************************/
static void send_ruleset_bases(struct conn_list *dest)
{
  struct packet_ruleset_base packet;

  base_type_iterate(b) {
    int j;

    packet.id = base_number(b);
    sz_strlcpy(packet.name, untranslated_name(&b->name));
    sz_strlcpy(packet.rule_name, rule_name(&b->name));
    sz_strlcpy(packet.graphic_str, b->graphic_str);
    sz_strlcpy(packet.graphic_alt, b->graphic_alt);
    sz_strlcpy(packet.activity_gfx, b->activity_gfx);
    packet.buildable = b->buildable;
    packet.pillageable = b->pillageable;

    j = 0;
    requirement_vector_iterate(&b->reqs, preq) {
      packet.reqs[j++] = *preq;
    } requirement_vector_iterate_end;
    packet.reqs_count = j;
    packet.native_to = b->native_to;

    packet.gui_type = b->gui_type;
    packet.build_time = b->build_time;
    packet.defense_bonus = b->defense_bonus;
    packet.border_sq = b->border_sq;
    packet.vision_main_sq = b->vision_main_sq;
    packet.vision_invis_sq = b->vision_invis_sq;

    packet.flags = b->flags;
    packet.conflicts = b->conflicts;

    PACKET_STRVEC_COMPUTE(packet.helptext, b->helptext);

    lsend_packet_ruleset_base(dest, &packet);
  } base_type_iterate_end;
}

/**************************************************************************
  Send the road ruleset information (all individual road types) to the
  specified connections.
**************************************************************************/
static void send_ruleset_roads(struct conn_list *dest)
{
  struct packet_ruleset_road packet;

  road_type_iterate(r) {
    packet.id = road_number(r);

    sz_strlcpy(packet.name, untranslated_name(&r->name));
    sz_strlcpy(packet.rule_name, rule_name(&r->name));

    packet.native_to = r->native_to;

    lsend_packet_ruleset_road(dest, &packet);
  } road_type_iterate_end;
}

/**************************************************************************
  Send the government ruleset information to the specified connections.
  One packet per government type, and for each type one per ruler title.
**************************************************************************/
static void send_ruleset_governments(struct conn_list *dest)
{
  struct packet_ruleset_government gov;
  struct packet_ruleset_government_ruler_title title;
  int j;

  governments_iterate(g) {
    /* send one packet_government */
    gov.id = government_number(g);

    j = 0;
    requirement_vector_iterate(&g->reqs, preq) {
      gov.reqs[j++] = *preq;
    } requirement_vector_iterate_end;
    gov.reqs_count = j;

    sz_strlcpy(gov.name, untranslated_name(&g->name));
    sz_strlcpy(gov.rule_name, rule_name(&g->name));
    sz_strlcpy(gov.graphic_str, g->graphic_str);
    sz_strlcpy(gov.graphic_alt, g->graphic_alt);
    PACKET_STRVEC_COMPUTE(gov.helptext, g->helptext);

    lsend_packet_ruleset_government(dest, &gov);

    /* Send one packet_government_ruler_title per ruler title. */
    ruler_titles_iterate(government_ruler_titles(g), pruler_title) {
      const struct nation_type *pnation = ruler_title_nation(pruler_title);

      title.gov = government_number(g);
      title.nation = (NULL != pnation ? nation_number(pnation) : -1);
      sz_strlcpy(title.male_title,
                 ruler_title_male_untranslated_name(pruler_title));
      sz_strlcpy(title.female_title,
                 ruler_title_female_untranslated_name(pruler_title));
      lsend_packet_ruleset_government_ruler_title(dest, &title);
    } ruler_titles_iterate_end;
  } governments_iterate_end;
}

/**************************************************************************
  Send the nations ruleset information (info on each nation) to the
  specified connections.
**************************************************************************/
static void send_ruleset_nations(struct conn_list *dest)
{
  struct packet_ruleset_nation packet;
  struct packet_ruleset_nation_groups groups_packet;
  int i;

  groups_packet.ngroups = nation_group_count();
  i = 0;
  nation_groups_iterate(pgroup) {
    sz_strlcpy(groups_packet.groups[i++],
               nation_group_untranslated_name(pgroup));
  } nation_groups_iterate_end;
  lsend_packet_ruleset_nation_groups(dest, &groups_packet);

  nations_iterate(n) {
    packet.id = nation_number(n);
    sz_strlcpy(packet.adjective, untranslated_name(&n->adjective));
    sz_strlcpy(packet.rule_name, rule_name(&n->adjective));
    sz_strlcpy(packet.noun_plural, untranslated_name(&n->noun_plural));
    sz_strlcpy(packet.graphic_str, n->flag_graphic_str);
    sz_strlcpy(packet.graphic_alt, n->flag_graphic_alt);

    i = 0;
    nation_leader_list_iterate(nation_leaders(n), pleader) {
      sz_strlcpy(packet.leader_name[i], nation_leader_name(pleader));
      packet.leader_is_male[i] = nation_leader_is_male(pleader);
      i++;
    } nation_leader_list_iterate_end;
    packet.leader_count = i;

    packet.city_style = n->city_style;
    packet.is_playable = n->is_playable;
    packet.is_available = n->is_available;
    packet.barbarian_type = n->barb_type;

    sz_strlcpy(packet.legend, n->legend);

    i = 0;
    nation_group_list_iterate(n->groups, pgroup) {
      packet.groups[i++] = nation_group_number(pgroup);
    } nation_group_list_iterate_end;
    packet.ngroups = i;

    lsend_packet_ruleset_nation(dest, &packet);
  } nations_iterate_end;
}

/**************************************************************************
  Send the city-style ruleset information (each style) to the specified
  connections.
**************************************************************************/
static void send_ruleset_cities(struct conn_list *dest)
{
  struct packet_ruleset_city city_p;
  int k, j;

  for (k = 0; k < game.control.styles_count; k++) {
    city_p.style_id = k;
    city_p.replaced_by = city_styles[k].replaced_by;

    j = 0;
    requirement_vector_iterate(&city_styles[k].reqs, preq) {
      city_p.reqs[j++] = *preq;
    } requirement_vector_iterate_end;
    city_p.reqs_count = j;

    sz_strlcpy(city_p.name, untranslated_name(&city_styles[k].name));
    sz_strlcpy(city_p.rule_name, rule_name(&city_styles[k].name));
    sz_strlcpy(city_p.graphic, city_styles[k].graphic);
    sz_strlcpy(city_p.graphic_alt, city_styles[k].graphic_alt);
    sz_strlcpy(city_p.oceanic_graphic, city_styles[k].oceanic_graphic);
    sz_strlcpy(city_p.oceanic_graphic_alt, city_styles[k].oceanic_graphic_alt);
    sz_strlcpy(city_p.citizens_graphic, city_styles[k].citizens_graphic);
    sz_strlcpy(city_p.citizens_graphic_alt,
			 city_styles[k].citizens_graphic_alt);

    lsend_packet_ruleset_city(dest, &city_p);
  }
}

/**************************************************************************
  Send information in packet_ruleset_game (miscellaneous rules) to the
  specified connections.
**************************************************************************/
static void send_ruleset_game(struct conn_list *dest)
{
  struct packet_ruleset_game misc_p;
  int i;

  fc_assert_ret(game.veteran != NULL);

  /* Per unit veteran system definition. */
  misc_p.veteran_levels = game.veteran->levels;

  for (i = 0; i < misc_p.veteran_levels; i++) {
    const struct veteran_level *vlevel = game.veteran->definitions + i;

    sz_strlcpy(misc_p.veteran_name[i], untranslated_name(&vlevel->name));
    misc_p.power_fact[i] = vlevel->power_fact;
    misc_p.move_bonus[i] = vlevel->move_bonus;
  }

  fc_assert(sizeof(misc_p.global_init_techs)
            == sizeof(game.server.rgame.global_init_techs));
  fc_assert(ARRAY_SIZE(misc_p.global_init_techs)
            == ARRAY_SIZE(game.server.rgame.global_init_techs));
  memcpy(misc_p.global_init_techs, game.server.rgame.global_init_techs,
         sizeof(misc_p.global_init_techs));

  misc_p.default_specialist = DEFAULT_SPECIALIST;

  fc_assert_ret(game.plr_bg_color != NULL);

  misc_p.background_red = game.plr_bg_color->r;
  misc_p.background_green = game.plr_bg_color->g;
  misc_p.background_blue = game.plr_bg_color->b;

  lsend_packet_ruleset_game(dest, &misc_p);
}

/**************************************************************************
  Send all team names defined in the ruleset file(s) to the
  specified connections.
**************************************************************************/
static void send_ruleset_team_names(struct conn_list *dest)
{
  struct packet_team_name_info team_name_info_p;

  team_slots_iterate(tslot) {
    const char *name = team_slot_defined_name(tslot);

    if (NULL == name) {
      /* End of defined names. */
      break;
    }

    team_name_info_p.team_id = team_slot_index(tslot);
    sz_strlcpy(team_name_info_p.team_name, name);

    lsend_packet_team_name_info(dest, &team_name_info_p);
  } team_slots_iterate_end;
}

/****************************************************************************
  HACK: reset any nations that have been set so far.

  FIXME: this should be moved into nationhand.c.
****************************************************************************/
static void reset_player_nations(void)
{
  players_iterate(pplayer) {
    /* We cannot use player_set_nation() here since this is
     * called before nations are loaded. Player pointers
     * from nations will be initialized to NULL when nations are
     * loaded. */
    pplayer->nation = NO_NATION_SELECTED;
    pplayer->city_style = 0;
  } players_iterate_end;
  send_player_info_c(NULL, game.est_connections);
}

/**************************************************************************
  Loads the ruleset currently given in game.rulesetdir.

  This may be called more than once and it will free any stale data.
**************************************************************************/
void load_rulesets(void)
{
  struct section_file *techfile, *unitfile, *buildfile, *govfile, *terrfile;
  struct section_file *cityfile, *nationfile, *effectfile;

  log_normal(_("Loading rulesets."));

  game_ruleset_free();
  /* Reset the list of available player colors. */
  playercolor_free();
  playercolor_init();
  game_ruleset_init();

  reset_player_nations();
  server.playable_nations = 0;

  techfile = openload_ruleset_file("techs");
  load_tech_names(techfile);

  buildfile = openload_ruleset_file("buildings");
  load_building_names(buildfile);

  govfile = openload_ruleset_file("governments");
  load_government_names(govfile);

  unitfile = openload_ruleset_file("units");
  load_unit_names(unitfile);

  terrfile = openload_ruleset_file("terrain");
  load_terrain_names(terrfile);

  cityfile = openload_ruleset_file("cities");
  load_citystyle_names(cityfile);

  nationfile = openload_ruleset_file("nations");
  load_nation_names(nationfile);

  effectfile = openload_ruleset_file("effects");

  load_ruleset_techs(techfile);
  load_ruleset_cities(cityfile);
  load_ruleset_governments(govfile);
  load_ruleset_terrain(terrfile);  /* terrain must precede nations and units */
  load_ruleset_units(unitfile);
  load_ruleset_buildings(buildfile);
  load_ruleset_nations(nationfile);
  load_ruleset_effects(effectfile);
  load_ruleset_game();

  /* Init nations we just loaded. */
  init_available_nations();

  sanity_check_ruleset_data();

  precalc_tech_data();

  if (base_sections) {
    free(base_sections);
    base_sections = NULL;
  }
  if (road_sections) {
    free(road_sections);
    road_sections = NULL;
  }
  if (resource_sections) {
    free(resource_sections);
    resource_sections = NULL;
  }
  if (terrain_sections) {
    free(terrain_sections);
    terrain_sections = NULL;
  }

  script_server_free();

  script_server_init();
  openload_script_file("default");
  openload_script_file("script");

  /* Build advisors unit class cache corresponding to loaded rulesets */
  adv_units_ruleset_init();
  CALL_FUNC_EACH_AI(units_ruleset_init);

  /* We may need to adjust the number of AI players
   * if the number of available nations changed. */
  if (game.info.aifill > server.playable_nations) {
    log_normal(_("Reducing aifill because there "
                 "are not enough playable nations."));
    game.info.aifill = server.playable_nations;
    aifill(game.info.aifill);
  }
}

/**************************************************************************
  Reload the game settings saved in the ruleset file.
**************************************************************************/
void reload_rulesets_settings(void)
{
  struct section_file *file;

  file = openload_ruleset_file("game");
  settings_ruleset(file, "settings");
  secfile_destroy(file);
}

/**************************************************************************
  Send all ruleset information to the specified connections.
**************************************************************************/
void send_rulesets(struct conn_list *dest)
{
  conn_list_compression_freeze(dest);

  send_ruleset_control(dest);
  send_ruleset_game(dest);
  send_ruleset_team_names(dest);
  send_ruleset_techs(dest);
  send_ruleset_governments(dest);
  send_ruleset_unit_classes(dest);
  send_ruleset_units(dest);
  send_ruleset_specialists(dest);
  send_ruleset_resources(dest);
  send_ruleset_terrain(dest);
  send_ruleset_bases(dest);
  send_ruleset_roads(dest);
  send_ruleset_buildings(dest);
  send_ruleset_nations(dest);
  send_ruleset_cities(dest);
  send_ruleset_cache(dest);

  /* changed game settings will be send in
   * connecthand.c:establish_new_connection() */

  conn_list_compression_thaw(dest);
}

/**************************************************************************
  Does nation have tech initially?
**************************************************************************/
static bool nation_has_initial_tech(struct nation_type *pnation,
                                    struct advance *tech)
{
  int i;

  /* See if it's given as global init tech */
  for (i = 0; i < MAX_NUM_TECH_LIST
       && game.server.rgame.global_init_techs[i] != A_LAST; i++) {
    if (game.server.rgame.global_init_techs[i] == advance_number(tech)) {
      return TRUE;
    }
  }

  /* See if it's given as national init tech */
  for (i = 0;
       i < MAX_NUM_TECH_LIST && pnation->server.init_techs[i] != A_LAST;
       i++) {
    if (pnation->server.init_techs[i] == advance_number(tech)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**************************************************************************
  Helper function for sanity_check_req_list() and sanity_check_req_vec()
**************************************************************************/
static bool sanity_check_req_set(int reqs_of_type[], int local_reqs_of_type[],
                                 struct requirement *preq,
                                 int max_tiles, const char *list_for)
{
  int rc;

  fc_assert_ret_val(universals_n_is_valid(preq->source.kind), FALSE);

  /* Add to counter */
  reqs_of_type[preq->source.kind]++;
  rc = reqs_of_type[preq->source.kind];

  if (preq->range == REQ_RANGE_LOCAL) {
    local_reqs_of_type[preq->source.kind]++;

    switch (preq->source.kind) {
     case VUT_TERRAINCLASS:
       if (local_reqs_of_type[VUT_TERRAIN] > 0) {
         log_error("%s: Requirement list has both local terrain and terrainclass requirement",
                   list_for);
         return FALSE;
       }
       break;
     case VUT_TERRAIN:
       if (local_reqs_of_type[VUT_TERRAINCLASS] > 0) {
         log_error("%s: Requirement list has both local terrain and terrainclass requirement",
                   list_for);
         return FALSE;
       }
       break;
     default:
       break;
    }
  }

  if (rc > 1) {
    /* Multiple requirements of the same type */
    switch (preq->source.kind) {
     case VUT_GOVERNMENT:
     case VUT_NATION:
     case VUT_UTYPE:
     case VUT_UCLASS:
     case VUT_OTYPE:
     case VUT_SPECIALIST:
     case VUT_MINSIZE: /* Breaks nothing, but has no sense either */
     case VUT_MINYEAR:
     case VUT_AI_LEVEL:
     case VUT_TERRAINCLASS:
     case VUT_TERRAINALTER: /* Local range only */
     case VUT_CITYTILE:
       /* There can be only one requirement of these types (with current
        * range limitations)
        * Requirements might be identical, but we consider multiple
        * declarations error anyway. */

       log_error("%s: Requirement list has multiple %s requirements",
                 list_for, universal_type_rule_name(&preq->source));
       return FALSE;
       break;

     case VUT_TERRAIN:
       /* There can be only up to max_tiles requirements of these types */
       if (max_tiles != -1 && rc > max_tiles) {
         log_error("%s: Requirement list has more %s requirements than "
                   "can ever be fullfilled.", list_for,
                   universal_type_rule_name(&preq->source));
         return FALSE;
       }
       break;

     case VUT_SPECIAL:
     case VUT_BASE:
       /* Note that there can be more than 1 special or base / tile. */
     case VUT_NONE:
     case VUT_ADVANCE:
     case VUT_IMPROVEMENT:
     case VUT_UTFLAG:
     case VUT_UCFLAG:
       /* Can have multiple requirements of these types */
       break;
     case VUT_COUNT:
       /* Should never be in requirement vector */
       fc_assert(FALSE);
       return FALSE;
       break;
       /* No default handling here, as we want compiler warning
        * if new requirement type is added to enum and it's not handled
        * here. */
    }
  }

  return TRUE;
}

/**************************************************************************
  Check if requirement list is free of conflicting requirements.
  max_tiles is number of tiles that can provide requirement. Value -1
  disables checking based on number of tiles.

  Returns TRUE iff everything ok.

  TODO: This is based on current hardcoded range limitations.
        - There should be method of automatically determining these
          limitations for each requirement type
        - This function should check also problems caused by defining
          range to less than hardcoded max for requirement type
**************************************************************************/
static bool sanity_check_req_list(const struct requirement_list *preqs,
                                  int max_tiles,
                                  const char *list_for)
{
  int reqs_of_type[VUT_COUNT];
  int local_reqs_of_type[VUT_COUNT];

  /* Initialize requirement counters */
  memset(reqs_of_type, 0, sizeof(reqs_of_type));
  memset(local_reqs_of_type, 0, sizeof(local_reqs_of_type));

  requirement_list_iterate(preqs, preq) {
    if (!sanity_check_req_set(reqs_of_type, local_reqs_of_type, preq, max_tiles, list_for)) {
      return FALSE;
    }
  } requirement_list_iterate_end;

  return TRUE;
}

/**************************************************************************
  Requirement vector version of requirement sanity checking. See
  retuirement list version for comments.
**************************************************************************/
static bool sanity_check_req_vec(const struct requirement_vector *preqs,
                                 int max_tiles,
                                 const char *list_for)
{
  int reqs_of_type[VUT_COUNT];
  int local_reqs_of_type[VUT_COUNT];

  /* Initialize requirement counters */
  memset(reqs_of_type, 0, sizeof(reqs_of_type));
  memset(local_reqs_of_type, 0, sizeof(local_reqs_of_type));

  requirement_vector_iterate(preqs, preq) {
    if (!sanity_check_req_set(reqs_of_type, local_reqs_of_type, preq, max_tiles, list_for)) {
      return FALSE;
    }
  } requirement_vector_iterate_end;

  return TRUE;
}

/**************************************************************************
  Check that requirement list and negated requirements list do not have
  confliciting requirements.

  Returns TRUE iff everything ok.
**************************************************************************/
static bool sanity_check_req_nreq_list(const struct requirement_list *preqs,
                                       const struct requirement_list *pnreqs,
                                       int one_tile,
                                       const char *list_for)
{
  /* Check internal sanity of requirement list */
  if (!sanity_check_req_list(preqs, one_tile, list_for)) {
    return FALSE;
  }

  /* There is no pnreqs in all cases */
  if (pnreqs != NULL) {
    /* Check sanity between reqs and nreqs */
    requirement_list_iterate(preqs, preq) {
      requirement_list_iterate(pnreqs, pnreq) {
        if (are_requirements_equal(preq, pnreq)) {
          log_error("%s: Identical %s requirement in requirements and "
                    "negated requirements.", list_for,
                    universal_type_rule_name(&preq->source));
          return FALSE;
        }
      } requirement_list_iterate_end;
    } requirement_list_iterate_end;
  }

  return TRUE;
}

/**************************************************************************
  Sanity check callback for iterating effects cache.
**************************************************************************/
static bool effect_list_sanity_cb(const struct effect *peffect)
{
  int one_tile = -1; /* TODO: Determine correct value from effect.
                      *       -1 disables checking */

  return sanity_check_req_nreq_list(peffect->reqs, peffect->nreqs, one_tile,
                                    effect_type_name(peffect->type));
}

/**************************************************************************
  Some more sanity checking once all rulesets are loaded. These check
  for some cross-referencing which was impossible to do while only one
  party was loaded in load_ruleset_xxx()

  Returns TRUE iff everything ok.
**************************************************************************/
static bool sanity_check_ruleset_data(void)
{
  int num_utypes;
  int i;
  bool ok = TRUE; /* Store failures to variable instead of returning
                   * immediately so all errors get printed, not just first
                   * one. */

  /* Check that all players can have their initial techs */
  nations_iterate(pnation) {
    int i;

    /* Check global initial techs */
    for (i = 0; i < MAX_NUM_TECH_LIST
         && game.server.rgame.global_init_techs[i] != A_LAST; i++) {
      Tech_type_id tech = game.server.rgame.global_init_techs[i];
      struct advance *a = valid_advance_by_number(tech);

      if (!a) {
        ruleset_error(LOG_FATAL,
                      "Tech %s does not exist, but is initial "
                      "tech for everyone.",
                      advance_rule_name(advance_by_number(tech)));
      }
      if (advance_by_number(A_NONE) != a->require[AR_ROOT]
          && !nation_has_initial_tech(pnation, a->require[AR_ROOT])) {
        /* Nation has no root_req for tech */
        ruleset_error(LOG_FATAL,
                      "Tech %s is initial for everyone, but %s has "
                      "no root_req for it.",
                      advance_rule_name(a),
                      nation_rule_name(pnation));
      }
    }

    /* Check national initial techs */
    for (i = 0;
         i < MAX_NUM_TECH_LIST && pnation->server.init_techs[i] != A_LAST;
         i++) {
      Tech_type_id tech = pnation->server.init_techs[i];
      struct advance *a = valid_advance_by_number(tech);

      if (!a) {
        ruleset_error(LOG_FATAL,
                      "Tech %s does not exist, but is tech for %s.",
                      advance_rule_name(advance_by_number(tech)),
                      nation_rule_name(pnation));
        ok = FALSE;
      }
      if (advance_by_number(A_NONE) != a->require[AR_ROOT]
          && !nation_has_initial_tech(pnation, a->require[AR_ROOT])) {
        /* Nation has no root_req for tech */
        ruleset_error(LOG_FATAL,
                      "Tech %s is initial for %s, but they have "
                      "no root_req for it.",
                      advance_rule_name(a),
                      nation_rule_name(pnation));
        ok = FALSE;
      }
    }
  } nations_iterate_end;

  /* Check against unit upgrade loops */
  num_utypes = game.control.num_unit_types;
  unit_type_iterate(putype) {
    int chain_length = 0;
    struct unit_type *upgraded = putype;

    while(upgraded != NULL) {
      upgraded = upgraded->obsoleted_by;
      chain_length++;
      if (chain_length > num_utypes) {
        ruleset_error(LOG_FATAL,
                      "There seems to be obsoleted_by loop in update "
                      "chain that starts from %s", utype_rule_name(putype));
        ok = FALSE;
      }
    }
  } unit_type_iterate_end;

  /* Check requirement sets against conflicting requirements.
   * Effects use requirement lists */
  if (!iterate_effect_cache(effect_list_sanity_cb)) {
    ruleset_error(LOG_FATAL, "Effects have conflicting requirements!");
    ok = FALSE;
  }

  /* Others use requirement vectors
   * Buildings */
  improvement_iterate(pimprove) {
    if (!sanity_check_req_vec(&pimprove->reqs, -1,
                              improvement_rule_name(pimprove))) {
      ruleset_error(LOG_FATAL, "Buildings have conflicting requirements!");
      ok = FALSE;
    }
  } improvement_iterate_end;

  /* Governments */
  governments_iterate(pgov) {
    if (!sanity_check_req_vec(&pgov->reqs, -1,
                              government_rule_name(pgov))) {
      ruleset_error(LOG_FATAL, "Governments have conflicting requirements!");
      ok = FALSE;
    }
  } governments_iterate_end;

  /* Specialists */
  specialist_type_iterate(sp) {
    struct specialist *psp = specialist_by_number(sp);

    if (!sanity_check_req_vec(&psp->reqs, -1,
                              specialist_rule_name(psp))) {
      ruleset_error(LOG_FATAL, "Specialists have conflicting requirements!");
      ok = FALSE;
    }
  } specialist_type_iterate_end;

  /* Bases */
  base_type_iterate(pbase) {
    if (!sanity_check_req_vec(&pbase->reqs, -1,
                              base_rule_name(pbase))) {
      ruleset_error(LOG_FATAL, "Bases have conflicting requirements!");
      ok = FALSE;
    }
  } base_type_iterate_end

  /* City styles */
  for (i = 0; i < game.control.styles_count; i++) {
    if (!sanity_check_req_vec(&city_styles[i].reqs, -1,
                              city_style_rule_name(i))) {
      ruleset_error(LOG_FATAL, "City styles have conflicting requirements!");
      ok = FALSE;
    }
  }

  terrain_type_iterate(pterr) {
    unit_class_iterate(uc) {
      if (BV_ISSET(pterr->native_to, uclass_index(uc))) {
        if (is_ocean(pterr) && uc->move_type == UMT_LAND) {
          ruleset_error(LOG_FATAL,
                        "Oceanic %s is native to land units.",
                        terrain_rule_name(pterr));
          ok = FALSE;
        } else if (!is_ocean(pterr) && uc->move_type == UMT_SEA) {
          ruleset_error(LOG_FATAL,
                        "Non-oceanic %s is native to sea units.",
                        terrain_rule_name(pterr));
          ok = FALSE;
        }
      }
    } unit_class_iterate_end;
  } terrain_type_iterate_end;

  return ok;
}
