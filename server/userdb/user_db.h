/**********************************************************************
 Freeciv - Copyright (C) 2003 - M.C. Kaufman
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__USER_DB_H
#define FC__USER_DB_H

struct user;

/**************************************************
 The user db statuses are:

 1: an error occurred, possibly we couldn't access the database file.
 2: we were able to successfully insert an entry. or we found the entry 
    we were searching for
 3: the user we were searching for was not found.
***************************************************/
enum userdb_status {
  USER_DB_ERROR = 1,
  USER_DB_SUCCESS,
  USER_DB_NOT_FOUND
};

enum userdb_status user_db_load(struct user *puser);
enum userdb_status user_db_save(struct user *puser);

#endif /* FC__USER_DB_H */
