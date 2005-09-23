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
#ifndef FC__HASH_H
#define FC__HASH_H

/**************************************************************************
   A general-purpose hash table implementation.
   See comments in hash.c.
***************************************************************************/

#include "shared.h"		/* bool type */

struct hash_table;		/* opaque */

/* Function typedefs: */
typedef unsigned int (*hash_val_fn_t)(const void *, unsigned int);
typedef int (*hash_cmp_fn_t)(const void *, const void *);

/* Supplied functions (matching above typedefs) appropriate for
   keys being normal nul-terminated strings: */
unsigned int hash_fval_string(const void *vkey, unsigned int num_buckets);
int hash_fcmp_string(const void *vkey1, const void *vkey2);

/* Appropriate for int values: */
unsigned int hash_fval_int(const void *vkey, unsigned int num_buckets);
int hash_fcmp_int(const void *vkey1, const void *vkey2);

/* Appropriate for void pointers or casted longs, used as keys
   directly instead of by reference. */
unsigned int hash_fval_keyval(const void *vkey, unsigned int num_buckets);
int hash_fcmp_keyval(const void *vkey1, const void *vkey2);

/* General functions: */
struct hash_table *hash_new(hash_val_fn_t fval, hash_cmp_fn_t fcmp);
struct hash_table *hash_new_nentries(hash_val_fn_t fval, hash_cmp_fn_t fcmp,
				     unsigned int nentries);

void hash_free(struct hash_table *h);

bool hash_insert(struct hash_table *h, const void *key, const void *data);
void *hash_replace(struct hash_table *h, const void *key, const void *data);

bool hash_key_exists(const struct hash_table *h, const void *key);
void *hash_lookup_data(const struct hash_table *h, const void *key);

void *hash_delete_entry(struct hash_table *h, const void *key);
void *hash_delete_entry_full(struct hash_table *h, const void *key,
			     void **deleted_key);
void hash_delete_all_entries(struct hash_table *h);

const void *hash_key_by_number(const struct hash_table *h,
			       unsigned int entry_number);
const void *hash_value_by_number(const struct hash_table *h,
				 unsigned int entry_number);

unsigned int hash_num_entries(const struct hash_table *h);
unsigned int hash_num_buckets(const struct hash_table *h);
unsigned int hash_num_deleted(const struct hash_table *h);

#endif  /* FC__HASH_H */
