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
#ifndef FC__SECTION_FILE_H
#define FC__SECTION_FILE_H

/* This header contains internals of section_file that its users should
 * not care about. This header should be included by soruce files implementing
 * registry itself. */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Section structure. */
struct section {
  struct section_file *secfile; /* Parent structure. */
  char *name;                   /* Name of the section. */
  struct entry_list *entries;   /* The list of the children. */
};

/* The section file struct itself. */
struct section_file {
  char *name;                           /* Can be NULL. */
  size_t num_entries;
  struct section_list *sections;
  bool allow_duplicates;
  struct {
    struct section_hash *sections;
    struct entry_hash *entries;
  } hash;
};

void secfile_log(const struct section_file *secfile,
                 const struct section *psection,
                 const char *file, const char *function, int line,
                 const char *format, ...)
  fc__attribute((__format__(__printf__, 6, 7)));

#define SECFILE_LOG(secfile, psection, format, ...)                         \
  secfile_log(secfile, psection, __FILE__, __FUNCTION__, __FC_LINE__,       \
              format, ## __VA_ARGS__)
#define SECFILE_RETURN_IF_FAIL(secfile, psection, condition)                \
  if (!(condition)) {                                                       \
    SECFILE_LOG(secfile, psection, "Assertion '%s' failed.", #condition);   \
    return;                                                                 \
  }
#define SECFILE_RETURN_VAL_IF_FAIL(secfile, psection, condition, value)     \
  if (!(condition)) {                                                       \
    SECFILE_LOG(secfile, psection, "Assertion '%s' failed.", #condition);   \
    return value;                                                           \
  }

#define SPECHASH_TAG section
#define SPECHASH_KEY_TYPE char *
#define SPECHASH_DATA_TYPE struct section *
#define SPECHASH_KEY_VAL genhash_str_val_func
#define SPECHASH_KEY_COMP genhash_str_comp_func
#include "spechash.h"

#define SPECHASH_TAG entry
#define SPECHASH_KEY_TYPE char *
#define SPECHASH_DATA_TYPE struct entry *
#define SPECHASH_KEY_VAL genhash_str_val_func
#define SPECHASH_KEY_COMP genhash_str_comp_func
#define SPECHASH_KEY_COPY genhash_str_copy_func
#define SPECHASH_KEY_FREE genhash_str_free_func
#include "spechash.h"

bool entry_from_token(struct section *psection, const char *name,
                      const char *tok);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* FC__SECTION_FILE_H */
