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

/* utility */
#include "mem.h"
#include "registry.h"

#include "section_file.h"

#define MAX_LEN_ERRORBUF 1024

static char error_buffer[MAX_LEN_ERRORBUF] = "\0";

/**************************************************************************
  Returns the last error which occured in a string.  It never returns NULL.
**************************************************************************/
const char *secfile_error(void)
{
  return error_buffer;
}

/**************************************************************************
  Edit the error_buffer.
**************************************************************************/
void secfile_log(const struct section_file *secfile,
                 const struct section *psection,
                 const char *file, const char *function, int line,
                 const char *format, ...)
{
  char message[MAX_LEN_ERRORBUF];
  va_list args;

  va_start(args, format);
  fc_vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  fc_snprintf(error_buffer, sizeof(error_buffer),
              "In %s() [%s:%d]: secfile '%s' in section '%s': %s",
              function, file, line, secfile_name(secfile),
              psection != NULL ? section_name(psection) : "NULL", message);
}

/**************************************************************************
  Returns the section name.
**************************************************************************/
const char *section_name(const struct section *psection)
{
  return (NULL != psection ? psection->name : NULL);
}

/**************************************************************************
  Create a new empty section file.
**************************************************************************/
struct section_file *secfile_new(bool allow_duplicates)
{
  struct section_file *secfile = fc_malloc(sizeof(struct section_file));

  secfile->name = NULL;
  secfile->num_entries = 0;
  secfile->sections = section_list_new_full(section_destroy);
  secfile->allow_duplicates = allow_duplicates;

  secfile->hash.sections = section_hash_new();
  /* Maybe allocated later. */
  secfile->hash.entries = NULL;

  return secfile;
}

/**************************************************************************
  Free a section file.
**************************************************************************/
void secfile_destroy(struct section_file *secfile)
{
  SECFILE_RETURN_IF_FAIL(secfile, NULL, secfile != NULL);

  section_hash_destroy(secfile->hash.sections);
  /* Mark it NULL to be sure to don't try to make operations when
   * deleting the entries. */
  secfile->hash.sections = NULL;
  if (NULL != secfile->hash.entries) {
    entry_hash_destroy(secfile->hash.entries);
    /* Mark it NULL to be sure to don't try to make operations when
     * deleting the entries. */
    secfile->hash.entries = NULL;
  }

  section_list_destroy(secfile->sections);

  if (NULL != secfile->name) {
    free(secfile->name);
  }

  free(secfile);
}
