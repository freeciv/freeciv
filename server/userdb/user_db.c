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

#include "connection.h"
#include "registry.h"
#include "shared.h"
#include "support.h"

#include "lockfile.h"
#include "user_db.h"

#ifndef USER_DATABASE
#define FC_USER_DATABASE "freeciv_user_database"
#endif

static void fill_entry(struct section_file *sf, 
                       struct connection *pconn, int no);

static void fill_conn(struct section_file *sf, 
                      struct connection *pconn, int no);

/**************************************************************************
 helper function to fill the user and password in the connection struct 
 from the database
**************************************************************************/
static void fill_conn(struct section_file *sf, 
                      struct connection *pconn, int no)
{
  sz_strlcpy(pconn->username, secfile_lookup_str(sf, "db.users%d.name", no));
  sz_strlcpy(pconn->server.password,
	     secfile_lookup_str(sf, "db.users%d.pass", no));
}

/**************************************************************************
 helper function to insert some fields of the connection struct to the database
**************************************************************************/
static void fill_entry(struct section_file *sf, 
                       struct connection *pconn, int no)
{
  secfile_insert_str(sf, pconn->username, "db.users%d.name", no);
  secfile_insert_str(sf, pconn->server.password, "db.users%d.pass", no);
}

/**************************************************************************
  Check if the password with length len matches that given in 
  pconn->server.password.
  If so, return 1, else return 0. We return an int instead of boolean here
  because external databases might not know what bool is.
***************************************************************************/
int userdb_check_password(struct connection *pconn, 
		          const char *password, int len)
{
  return (strncmp(pconn->server.password, password, len) == 0) ? 1 : 0;
}

/**************************************************************************
 Loads a user from the database.
**************************************************************************/
enum userdb_status user_db_load(struct connection *pconn)
{
  struct section_file sf;
  int i, num_entries = 0;
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
    num_entries = secfile_lookup_int(&sf, "db.num_entries");
  }

  /* find the connection in the database */
  for (i = 0; i < num_entries; i++) {
    char *username = secfile_lookup_str(&sf, "db.users%d.name", i);
    if (strncmp(username, pconn->username, MAX_LEN_NAME) == 0) {
      break;
    }
  }

  /* if we couldn't find the connection, return, else,
   * fill the connection struct */
  if (i == num_entries) {
    result = USER_DB_NOT_FOUND;
  } else {
    fill_conn(&sf, pconn, i);
    result = USER_DB_SUCCESS;
  }
  section_file_free(&sf);

  unlock_end:;
  remove_lock();
  end:;
  return result;
}

/**************************************************************************
 Saves pconn fields to the database. If the username already exists, 
 replace the data.
**************************************************************************/
enum userdb_status user_db_save(struct connection *pconn)
{
  struct section_file old, new;
  struct connection conn; 
  int i, num_entries = 0, new_num = 1;
  enum userdb_status result;

  if (!create_lock()) {
    result = USER_DB_ERROR;
    goto end;
  }

  section_file_init(&new);

  /* add the pconns info to the database */
  fill_entry(&new, pconn, 0);

  /* load the file, and if it exists, save it to the new file */
  if (section_file_load(&old, FC_USER_DATABASE)) {
    num_entries = secfile_lookup_int(&old, "db.num_entries");

    /* fill the new database with the old one. skip pconn if he
     * exists, since we already inserted his new information */
  
    for (i = 0; i < num_entries; i++) {
      fill_conn(&old, &conn, i);
      if (strncmp(conn.username, pconn->username, MAX_LEN_NAME) != 0) {
        fill_entry(&new, &conn, new_num++);
      }
    }

    section_file_free(&old);
  }

  /* insert the number of connections */
  secfile_insert_int(&new, new_num, "db.num_entries");

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
