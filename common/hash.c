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

/**************************************************************************
   A general-purpose hash table implementation.

   Based on implementation previous included in registry.c, but separated
   out so that can be used more generally.  Maybe we should just use glib?

      Original author:  David Pfitzner  dwp@mso.anu.edu.au

   A hash table maps keys to user data values, using a user-supplied hash
   function to do this efficiently.  Here both keys and values are general
   data represented by (void*) pointers.  Memory management of both keys
   and data is the responsibility of the caller: that is, the caller must
   ensure that the memory (especially for keys) remains valid (allocated)
   for as long as required (typically, the life of the hash table).
   (Otherwise, to allocate keys internally would either have to restrict
   key type (e.g., strings), or have user-supplied function to duplicate
   a key type.)

   User-supplied functions required are:
   
     fval: map key to bucket number given number of buckets; should map
   keys fairly evenly to range 0 to (num_buckets-1) inclusive.
   
     fcmp: compare keys for equality, necessary for lookups for keys
   which map to the same hash value.  Keys which compare equal should
   map to the same hash value.  Returns 0 for equality, so can use
   qsort-type comparison function (but the hash table does not make
   use of the ordering information if the return value is non-zero).

   Implementation uses closed hashing with simple collision resolution.

   There are currently some big limitations:
   -- Cannot resize the hash table!
   -- Cannot delete individual entries!

***************************************************************************/

#include <string.h>

#include "log.h"
#include "mem.h"

#include "hash.h"

struct hash_bucket {
  int used;			/* 0 or 1 */
  const void *key;
  const void *data;
  unsigned int hash_val;	/* to avoid recalculating, or an extra fcmp,
				   in lookup */
};

/* Contents of the opaque type: */
struct hash_table {
  struct hash_bucket *buckets;
  hash_val_fn_t fval;
  hash_cmp_fn_t fcmp;
  unsigned int num_buckets;
  unsigned int num_entries;
};

/* Calculate hash value given hash_table ptr and key: */
#define HASH_VAL(h,k) (((h)->fval)((k), ((h)->num_buckets)))

/**************************************************************************
  Initialize a hash bucket to "zero" data:
**************************************************************************/
static void zero_hbucket(struct hash_bucket *bucket)
{
  bucket->used = 0;
  bucket->key = NULL;
  bucket->data = NULL;
  bucket->hash_val = 0;
}

/**************************************************************************
  Initialize a hash table to "zero" data:
**************************************************************************/
static void zero_htable(struct hash_table *h)
{
  h->buckets = NULL;
  h->fval = NULL;
  h->fcmp = NULL;
  h->num_buckets = h->num_entries = 0;
}

/**************************************************************************
  A supplied hash function appropriate to nul-terminated strings.
  Anyone know a good one?
**************************************************************************/
/* plenty of primes, not too small, in random order: */
static unsigned int coeff[] = { 113, 59, 23, 73, 31, 79, 97, 103, 67, 109,
				71, 89, 53, 37, 101, 41, 29, 43, 13, 61,
				107, 47, 83, 17 };
#define NCOEFF (sizeof(coeff)/sizeof(coeff[0]))
unsigned int hash_fval_string(const void *vkey, unsigned int num_buckets)
{
  const char *key = (const char*)vkey;
  unsigned int result=0;
  int i;

  for(i=0; *key; i++, key++) {
    if (i == NCOEFF) {
      i = 0;
    }
    result *= (unsigned int)5;
    result += coeff[i] * (unsigned int)(*key);
  }
  return (result % num_buckets);
}

/**************************************************************************
  A supplied function for comparison of nul-terminated strings:
**************************************************************************/
int hash_fcmp_string(const void *vkey1, const void *vkey2)
{
  return strcmp((const char*)vkey1, (const char*)vkey2);
}

/**************************************************************************
  Calculate a "reasonable" number of buckets for a given number
  of entries.  Use a power of 2 (not sure how important), with
  first power of 2 which is larger than num_entries, then extra
  factor of 2 for breathing space.  Say minimum of 32.
**************************************************************************/
static unsigned int calc_appropriate_nbuckets(unsigned int num_entries)
{
  unsigned int pow2;
  
  pow2 = 2;
  while (num_entries) {
    num_entries >>= 1;
    pow2 <<= 1;
  }
  if (pow2 < 32) pow2 = 32;
  return pow2;
}

/**************************************************************************
  Internal constructor, specifying exact number of buckets:
**************************************************************************/
static struct hash_table *hash_new_nbuckets(hash_val_fn_t fval,
					    hash_cmp_fn_t fcmp,
					    unsigned int nbuckets)
{
  struct hash_table *h;
  int i;

  h = fc_malloc(sizeof(struct hash_table));
  zero_htable(h);

  h->num_buckets = nbuckets;
  h->num_entries = 0;
  h->fval = fval;
  h->fcmp = fcmp;

  h->buckets = fc_malloc(h->num_buckets*sizeof(struct hash_bucket));

  for(i=0; i<h->num_buckets; i++) {
    zero_hbucket(&h->buckets[i]);
  }
  return h;
}

/**************************************************************************
  Constructor specifying number of entries:
**************************************************************************/
struct hash_table *hash_new_nentries(hash_val_fn_t fval, hash_cmp_fn_t fcmp,
				     unsigned int nentries)
{
  return hash_new_nbuckets(fval, fcmp, calc_appropriate_nbuckets(nentries));
}

/**************************************************************************
  Constructor with unspecified number of entries:
  (This is not very useful now because hash table does not grow,
  but should be more useful in future.)
**************************************************************************/
struct hash_table *hash_new(hash_val_fn_t fval, hash_cmp_fn_t fcmp)
{
  return hash_new_nentries(fval, fcmp, 0);
}

/**************************************************************************
  Destructor: free internal memory (not user-data memory)
  (also zeros memory, ... may be useful?)
**************************************************************************/
void hash_free(struct hash_table *h)
{
  int i;

  for(i=0; i<h->num_buckets; i++) {
    zero_hbucket(&h->buckets[i]);
  }
  free(h->buckets);
  zero_htable(h);
  free(h);
}

/**************************************************************************
  Return pointer to bucket in hash table where key resides, or where it
  should go if it is to be a new key.  Note caller needs to provide
  pre-calculated hash_val (this is to avoid re-calculations).
**************************************************************************/
static struct hash_bucket *internal_lookup(const struct hash_table *h,
					   const void *key,
					   unsigned int hash_val)
{
  struct hash_bucket *bucket;
  unsigned int i = hash_val;

  do {
    bucket = &h->buckets[i];
    if (!bucket->used) {
      return bucket;
    }
    if (bucket->hash_val==hash_val &&
	h->fcmp(bucket->key, key)==0) { /* match */
      return bucket;
    }
    i++;
    if (i==h->num_buckets) {
      i=0;
    }
  } while (i!=hash_val);	/* catch loop all the way round  */
  freelog(LOG_FATAL, "Full hash table -- and resize not implemented!!");
  exit(1);
}

/**************************************************************************
  Insert entry: returns 1 if inserted, or 0 if there was already an entry
  with the same key, in which case the entry was not inserted.
**************************************************************************/
int hash_insert(struct hash_table *h, const void *key, const void *data)
{
  struct hash_bucket *bucket;
  int hash_val;
    
  hash_val = HASH_VAL(h, key);
  bucket = internal_lookup(h, key, hash_val);
  if (bucket->used) {
    return 0;
  }
  bucket->key = key;
  bucket->data = data;
  bucket->used = 1;
  bucket->hash_val = hash_val;
  h->num_entries++;
  return 1;
}

/**************************************************************************
  Insert entry, replacing any existing entry which has the same key.
  Returns user-data of replaced entry if there was one, or NULL.
  (E.g. this allows caller to free or adjust data being replaced.)
**************************************************************************/
void *hash_replace(struct hash_table *h, const void *key, const void *data)
{
  struct hash_bucket *bucket;
  int hash_val;
  const void *ret;
    
  hash_val = HASH_VAL(h, key);
  bucket = internal_lookup(h, key, hash_val);
  if (bucket->used) {
    ret = bucket->data;
  } else {
    ret = NULL;
    h->num_entries++;
    bucket->used = 1;
  }
  bucket->key = key;
  bucket->data = data;
  bucket->hash_val = hash_val;
  return (void*)ret;
}

/**************************************************************************
  Lookup: return existence:
**************************************************************************/
int hash_key_exists(const struct hash_table *h, const void *key)
{
  struct hash_bucket *bucket = internal_lookup(h, key, HASH_VAL(h,key));
  return bucket->used;
}

/**************************************************************************
  Lookup: return user-data, or NULL.
  (Note that in other respects NULL is treated as a valid value, this is
  merely intended as a convenience when caller never uses NULL as value.)
**************************************************************************/
void *hash_lookup_data(const struct hash_table *h, const void *key)
{
  struct hash_bucket *bucket = internal_lookup(h, key, HASH_VAL(h,key));
  return (bucket->used ? (void*)bucket->data : NULL);
}

/**************************************************************************
  Accessor functions:
**************************************************************************/
unsigned int hash_num_entries(const struct hash_table *h)
{
  return h->num_entries;
}
unsigned int hash_num_buckets(const struct hash_table *h)
{
  return h->num_buckets;
}
