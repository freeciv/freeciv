/**********************************************************************
 Freeciv - Copyright (C) 2003  - M.C. Kaufman
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "registry.h"
#include "shared.h"
#include "support.h"

#include "lockfile.h"
#include "user.h"
#include "user_db.h"

#ifndef USER_DATABASE
#define FC_USER_DATABASE "freeciv_user_database"
#endif

static void fill_entry(struct section_file *sf, struct user *puser, int no);
static void fill_user(struct section_file *sf, struct user *puser, int no);

/**************************************************************************
 helper function to retrieve a user struct from the database
**************************************************************************/
static void fill_user(struct section_file *sf, struct user *puser, int no)
{
  sz_strlcpy(puser->name, secfile_lookup_str(sf, "db.users%d.name", no));
  sz_strlcpy(puser->password, secfile_lookup_str(sf, "db.users%d.pass", no));
}

/**************************************************************************
 helper function to insert a user struct to the database
**************************************************************************/
static void fill_entry(struct section_file *sf, struct user *puser, int no)
{
  secfile_insert_str(sf, puser->name, "db.users%d.name", no);
  secfile_insert_str(sf, puser->password, "db.users%d.pass", no);
}

/**************************************************************************
 Loads a user from the database.
**************************************************************************/
enum userdb_status user_db_load(struct user *puser)
{
  struct section_file sf;
  int i, num_users = 0;
  enum userdb_status result;

  if (!create_lock()) {
    result = USER_DB_ERROR;
    goto end;
  }

  /* load the file */
  if (!section_file_load(&sf, FC_USER_DATABASE)) {
    result = USER_DB_NOT_FOUND;
    goto unlock_end;
  } else {
    num_users = secfile_lookup_int(&sf, "db.user_num");
  }

  /* find the user in the database */
  for (i = 0; i < num_users; i++) {
    char *name = secfile_lookup_str(&sf, "db.users%d.name", i);
    if (strncmp(name, puser->name, MAX_LEN_NAME) == 0) {
      break;
    }
  }

  /* if we couldn't find the user, return, else, fill the user struct */
  if (i == num_users) {
    result = USER_DB_NOT_FOUND;
  } else {
    fill_user(&sf, puser, i);
    result = USER_DB_SUCCESS;
  }
  section_file_free(&sf);

  unlock_end:;
  remove_lock();
  end:;
  return result;
}

/**************************************************************************
 Saves a user to the database. If the user already exists, replace the data.
**************************************************************************/
enum userdb_status user_db_save(struct user *puser)
{
  struct section_file old, new;
  struct user entry; 
  int i, num_users = 0, new_num = 1;
  enum userdb_status result;

  if (!create_lock()) {
    result = USER_DB_ERROR;
    goto end;
  }

  section_file_init(&new);

  /* add the pusers info to the database */
  fill_entry(&new, puser, 0);

  /* load the file, and if it exists, save it to the new file */
  if (section_file_load(&old, FC_USER_DATABASE)) {
    num_users = secfile_lookup_int(&old, "db.user_num");

    /* fill the new database with the old one. skip puser if he
     * exists, since we already inserted his new information */
  
    for (i = 0; i < num_users; i++) {
      fill_user(&old, &entry, i);
      if (strncmp(entry.name, puser->name, MAX_LEN_NAME) != 0) {
        fill_entry(&new, &entry, new_num++);
      }
    }

    section_file_free(&old);
  }

  /* insert the number of users */
  secfile_insert_int(&new, new_num, "db.user_num");

  /* save to file */
  if (!section_file_save(&new, FC_USER_DATABASE, 0)) {
    result = USER_DB_ERROR;
  } else {
    result = USER_DB_SUCCESS;
  }
  section_file_free(&new);
  remove_lock();

  end:;
  return result;
}
