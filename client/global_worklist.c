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

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "registry.h"
#include "shared.h"
#include "support.h"

/* common */
#include "fc_types.h"
#include "requirements.h"
#include "worklist.h"

/* client */
#include "client_main.h"

#include "global_worklist.h"

/* The global worklist structure. */
struct global_worklist {
  int id;
  struct worklist worklist;
};

static bool global_worklist_load(struct section_file *file,
                                 const char *path, ...)
  fc__attribute((__format__ (__printf__, 2, 3)));

/***********************************************************************
  Initialize the client global worklists.
***********************************************************************/
void global_worklists_init(void)
{
  if (!client.worklists) {
    client.worklists = global_worklist_list_new();
  }
}

/***********************************************************************
  Free the client global worklists.
***********************************************************************/
void global_worklists_free(void)
{
  if (client.worklists) {
    global_worklists_iterate_all(pgwl) {
      global_worklist_destroy(pgwl);
    } global_worklists_iterate_all_end;
    global_worklist_list_free(client.worklists);
    client.worklists = NULL;
  }
}

/***********************************************************************
  Returns the number of valid global worklists.
  N.B.: This counts only the valid global worklists.
***********************************************************************/
int global_worklists_number(void)
{
  int count = 0;

  global_worklists_iterate(pgwl) {
    count++;
  } global_worklists_iterate_end;

  return count;
}

/***********************************************************************
  Returns a new created global worklist structure.
***********************************************************************/
static struct global_worklist *global_worklist_alloc(void)
{
  static int last_id = 0;
  struct global_worklist *pgwl = fc_calloc(1, sizeof(struct global_worklist));

  pgwl->id = ++last_id;
  worklist_init(&pgwl->worklist);

  global_worklist_list_append(client.worklists, pgwl);

  return pgwl;
}

/***********************************************************************
  Destroys a glocal worklist.
***********************************************************************/
void global_worklist_destroy(struct global_worklist *pgwl)
{
  assert(NULL != pgwl);

  global_worklist_list_unlink(client.worklists, pgwl);
  free(pgwl);
}

/***********************************************************************
  Creates a new global worklist form a normal worklist.
***********************************************************************/
struct global_worklist *global_worklist_new(const char *name)
{
  struct global_worklist *pgwl = global_worklist_alloc();

  global_worklist_set_name(pgwl, name);
  return pgwl;
}

/***********************************************************************
  Returns TRUE if this global worklist is valid.
***********************************************************************/
bool global_worklist_is_valid(const struct global_worklist *pgwl)
{
  return pgwl && pgwl->worklist.is_valid;
}

/***********************************************************************
  Sets the worklist. Return TRUE on success.
***********************************************************************/
bool global_worklist_set(struct global_worklist *pgwl,
                         const struct worklist *pwl)
{
  if (pgwl && pwl) {
    char name[MAX_LEN_NAME];    /* FIXME: Remove this hack! */

    sz_strlcpy(name, global_worklist_name(pgwl));
    worklist_copy(&pgwl->worklist, pwl);
    global_worklist_set_name(pgwl, name);
    return TRUE;
  }
  return FALSE;
}

/***********************************************************************
  Returns the worklist of this global worklist or NULL if it's not valid.
***********************************************************************/
const struct worklist *global_worklist_get(const struct global_worklist *pgwl)
{
  if (pgwl) {
    return &pgwl->worklist;
  } else {
    return NULL;
  }
}

/***********************************************************************
  Returns the id of the global worklist.
***********************************************************************/
int global_worklist_id(const struct global_worklist *pgwl)
{
  assert(NULL != pgwl);
  return pgwl->id;
}

/***********************************************************************
  Returns the global worklist corresponding to this id.
  N.B.: It can returns an invalid glocbal worklist.
***********************************************************************/
struct global_worklist *global_worklist_by_id(int id)
{
  global_worklists_iterate_all(pgwl) {
    if (pgwl->id == id) {
      return pgwl;
    }
  } global_worklists_iterate_all_end;

  return NULL;
}

/***********************************************************************
  Sets the name of this global worklist.
***********************************************************************/
void global_worklist_set_name(struct global_worklist *pgwl,
                              const char *name)
{
  if (name) {
    sz_strlcpy(pgwl->worklist.name, name);
  }
}

/***********************************************************************
  Return the name of the global worklist.
***********************************************************************/
const char *global_worklist_name(const struct global_worklist *pgwl)
{
  assert(NULL != pgwl);
  return pgwl->worklist.name;
}

/***********************************************************************
  Load a global worklist form a section file.  Returns FALSE if we
  reached the end of the array.
***********************************************************************/
static bool global_worklist_load(struct section_file *file,
                                 const char *path, ...)
{
  struct global_worklist *pgwl;
  struct worklist worklist;
  char path_str[1024];
  va_list args;

  va_start(args, path);
  my_vsnprintf(path_str, sizeof(path_str), path, args);
  va_end(args);

  if (-1 == secfile_lookup_int_default(file, -1, "%s.wl_length", path_str)) {
    /* Not set. */
    return FALSE;
  }

  worklist_load(file, &worklist, "%s", path_str);
  if (worklist.is_valid) {
    pgwl = global_worklist_alloc();
    global_worklist_set_name(pgwl, worklist.name);
    global_worklist_set(pgwl, &worklist);
  }
  return TRUE;
}

/***********************************************************************
  Load all global worklist from a section file.
***********************************************************************/
void global_worklists_load(struct section_file *file)
{
  int i;

  /* Clear the current global worklists. */
  global_worklists_iterate_all(pgwl) {
    global_worklist_destroy(pgwl);
  } global_worklists_iterate_all_end;

  for (i = 0; global_worklist_load(file, "worklists.worklist%d", i); i++) {
    /* Nothing to do more. */
  }
}

/***********************************************************************
  Save all global worklist to a section file.
***********************************************************************/
void global_worklists_save(struct section_file *file)
{
  int max_size = 0;
  int i = 0;

  /* We want to keep savegame in tabular format, so each line has to be
   * of equal length.  So we need to know about the biggest worklist. */
  global_worklists_iterate_all(pgwl) {
    max_size = MAX(max_size, worklist_length(&pgwl->worklist));
  } global_worklists_iterate_all_end;

  global_worklists_iterate_all(pgwl) {
    worklist_save(file, &pgwl->worklist, max_size,
                  "worklists.worklist%d", i++);
  } global_worklists_iterate_all_end;
}
