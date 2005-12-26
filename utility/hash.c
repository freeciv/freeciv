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
   a key type.  See further comments below.)

   User-supplied functions required are:
   
     fval: map key to bucket number given number of buckets; should map
   keys fairly evenly to range 0 to (num_buckets-1) inclusive.
   
     fcmp: compare keys for equality, necessary for lookups for keys
   which map to the same hash value.  Keys which compare equal should
   map to the same hash value.  Returns 0 for equality, so can use
   qsort-type comparison function (but the hash table does not make
   use of the ordering information if the return value is non-zero).

   Implementation uses closed hashing with simple collision resolution.
   Deleted elements are marked as DELETED rather than UNUSED so that
   lookups on previously added entries work properly.
   Resize hash table when deemed necessary by making and populating
   a new table.


   * More on memory management of keys and user-data:
   
   The above scheme of key memory management may cause some difficulties
   in ensuring that keys are freed appropriately, since the caller may
   need to keep another copy of the key pointer somewhere to be able to
   free it.  Possible approaches within the current hash implementation:

   - Allocate keys using sbuffer, and free them all together when done;
     this is the approach taken in registry and tilespec.
   - Store the keys within the "object" being stored as user-data in the
     hash.  Then when the data is returned by hash_delete or hash_replace,
     the key is available to be freed (or can just be freed as part of
     deleting the "object").  (This is done in registry hsec, but the
     allocation is still via sbuffer.)
   - Keep another hash from object pointers to key pointers?!  Seems a
     bit too perverse...

   Possible implementation changes to avoid/reduce this problem:

   - As noted above, could restrict key type (e.g., strings), or pass
     in more function pointers and user data to allow duplicating and
     freeing keys within hash table code.
   - Have some mechanism to allow external access to the key pointers
     in the hash table, especially when deleting/replacing elements
     -- and when freeing the hash table?

   Actually, there are some potential problems with memory management
   of the user-data pointers too:
   
   - If may have multiple keys pointing to the same data, cannot just
     free the data when deleted/replaced from hash.  (Eg, tilespec;
     eg, could reference count.)
   - When hash table as whole is deleted, need other access to user-data
     pointers if they need to be freed.

   Approaches:

   - Don't have hash table as only access to user-data.
   - Allocate user-data using sbuffer.

   
***************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>

#include "log.h"
#include "mem.h"
#include "shared.h" /* ARRAY_SIZE */

#include "hash.h"

#define FULL_RATIO 0.75         /* consider expanding when above this */
#define MIN_RATIO 0.24          /* shrink when below this */

enum Bucket_State { BUCKET_UNUSED=0, BUCKET_USED, BUCKET_DELETED };

struct hash_bucket {
  enum Bucket_State used;
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
  unsigned int num_entries;	/* does not included deleted entries */
  unsigned int num_deleted;
  bool frozen;			/* do not auto-resize when set */
};

/* Calculate hash value given hash_table ptr and key: */
#define HASH_VAL(h,k) (((h)->fval)((k), ((h)->num_buckets)))

/**************************************************************************
  Initialize a hash bucket to "zero" data:
**************************************************************************/
static void zero_hbucket(struct hash_bucket *bucket)
{
  bucket->used = BUCKET_UNUSED;
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
  h->num_buckets = h->num_entries = h->num_deleted = 0;
  h->frozen = FALSE;
}


/**************************************************************************
  A supplied hash function where key is pointer to int.  
  Prefers table sizes that are prime numbers.
**************************************************************************/
unsigned int hash_fval_int(const void *vkey, unsigned int num_buckets)
{
  const unsigned int key = (unsigned int) *(const int*)vkey;
  return (key % num_buckets);
}

/**************************************************************************
  A supplied function for comparison of pointers to int:
**************************************************************************/
int hash_fcmp_int(const void *vkey1, const void *vkey2)
{
  const int key1 = *(const int*)vkey1;
  const int key2 = *(const int*)vkey2;
  /* avoid overflow issues: */
  return (key1 < key2) ? -1 : (key1 > key2) ? 1 : 0;
}


/**************************************************************************
  A supplied hash function appropriate to nul-terminated strings.
  Prefers table sizes that are prime numbers.
**************************************************************************/
unsigned int hash_fval_string(const void *vkey, unsigned int num_buckets)
{
  const char *key = (const char*)vkey;
  unsigned long result=0;

  for (; *key != '\0'; key++) {
    result *= 5; 
    result += *key;
  }
  result &= 0xFFFFFFFF; /* To make results independent of sizeof(long) */
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
  A supplied hash function which operates on the key pointer values
  themselves; this way a void* (or, with casting, a long) can be used
  as a key, and also without having allocated space for it.
**************************************************************************/
unsigned int hash_fval_keyval(const void *vkey, unsigned int num_buckets)
{
  unsigned long result = ((unsigned long)vkey);
  return (result % num_buckets);
}

/**************************************************************************
  A supplied function for comparison of the raw void pointers (or,
  with casting, longs)
**************************************************************************/
int hash_fcmp_keyval(const void *vkey1, const void *vkey2)
{
  /* Simplicity itself. */
  return (vkey1 < vkey2) ? -1 : (vkey1 > vkey2) ? 1 : 0;
}


/**************************************************************************
  A bunch of prime numbers close to successive elements of the
  sequence A_n=3*2^n; to be used for table sizes.
**************************************************************************/
#define MIN_BUCKETS 29 /* historical purposes */
static const unsigned long ht_sizes[] =
{
  MIN_BUCKETS,          53,         97,           193, 
  389,       769,       1543,       3079,         6151,     
  12289,     24593,     49157,      98317,        196613,    
  393241,    786433,    1572869,    3145739,      6291469,   
  12582917,  25165843,  50331653,   100663319,    201326611, 
  402653189, 805306457, 1610612741, 3221225473ul, 4294967291ul
};
#define NSIZES ARRAY_SIZE(ht_sizes)

/**************************************************************************
  Calculate a "reasonable" number of buckets for a given number of
  entries.  Gives a prime number far from powers of 2 (see ht_sizes
  above; this allows for simpler hash-functions), allowing at least a
  factor of 2 from the given number of entries for breathing room.
***************************************************************************
  Generalized restrictions on the behavior of this function:
  * calc_appropriate_nbuckets(x) >= MIN_BUCKETS
  * calc_appropriate_nbuckets(x) * MIN_RATIO < x  whenever 
    x > MIN_BUCKETS * MIN_RATIO
  * calc_appropriate_nbuckets(x) * FULL_RATIO > x
  This one is more of a recommendation, to ensure enough free space:
  * calc_appropriate_nbuckets(x) >= 2 * x
**************************************************************************/
static unsigned int calc_appropriate_nbuckets(unsigned int num_entries)
{
  const unsigned long *pframe=&ht_sizes[0], *pmid;
  int fsize=NSIZES-1, lpart;

  num_entries <<= 1; /* breathing room */

  while (fsize > 0) {
    lpart = fsize >> 1;
    pmid = pframe + lpart;
    if (*pmid < num_entries) {
      pframe = pmid + 1;
      fsize = fsize - lpart - 1;
    } else {
      fsize = lpart;
    }
  }
  return *pframe;
}

/**************************************************************************
  Internal constructor, specifying exact number of buckets:
**************************************************************************/
static struct hash_table *hash_new_nbuckets(hash_val_fn_t fval,
					    hash_cmp_fn_t fcmp,
					    unsigned int nbuckets)
{
  struct hash_table *h;
  unsigned i;

  freelog(LOG_DEBUG, "New hash table with %u buckets", nbuckets);
  
  h = (struct hash_table *)fc_malloc(sizeof(struct hash_table));
  zero_htable(h);

  h->num_buckets = nbuckets;
  h->num_entries = 0;
  h->fval = fval;
  h->fcmp = fcmp;

  h->buckets = (struct hash_bucket *)
      	       fc_malloc(h->num_buckets*sizeof(struct hash_bucket));

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
**************************************************************************/
struct hash_table *hash_new(hash_val_fn_t fval, hash_cmp_fn_t fcmp)
{
  return hash_new_nentries(fval, fcmp, 0);
}

/**************************************************************************
  Free the contents of the hash table, _except_ leave the struct itself
  intact.  (also zeros memory, ... may be useful?)
**************************************************************************/
static void hash_free_contents(struct hash_table *h)
{
  unsigned i;

  for(i=0; i<h->num_buckets; i++) {
    zero_hbucket(&h->buckets[i]);
  }
  free(h->buckets);
  zero_htable(h);
}

/**************************************************************************
  Destructor: free internal memory (not user-data memory)
  (also zeros memory, ... may be useful?)
**************************************************************************/
void hash_free(struct hash_table *h)
{
  hash_free_contents(h);
  free(h);
}

/**************************************************************************
  Resize the hash table: create a new table, insert, then remove
  the old and assign.
**************************************************************************/
static void hash_resize_table(struct hash_table *h, unsigned int new_nbuckets)
{
  struct hash_table *h_new;
  unsigned i;

  assert(new_nbuckets >= h->num_entries);

  h_new = hash_new_nbuckets(h->fval, h->fcmp, new_nbuckets);
  h_new->frozen = TRUE;
  
  for(i=0; i<h->num_buckets; i++) {
    struct hash_bucket *bucket = &h->buckets[i];

    if (bucket->used == BUCKET_USED) {
      if (!hash_insert(h_new, bucket->key, bucket->data)) {
	assert(0);
      }
    }
  }
  h_new->frozen = FALSE;

  hash_free_contents(h);
  *h = *h_new;
  free(h_new);
}

/**************************************************************************
  Call this when an entry might be added or deleted: resizes the hash
  table if seems like a good idea.  Count deleted entries in check
  because efficiency may be degraded if there are too many deleted
  entries.  But for determining new size, ignore deleted entries,
  since they'll be removed by rehashing.
**************************************************************************/
#define hash_maybe_expand(htab) hash_maybe_resize((htab), TRUE)
#define hash_maybe_shrink(htab) hash_maybe_resize((htab), FALSE)
static void hash_maybe_resize(struct hash_table *h, bool expandingp)
{
  unsigned int num_used, limit, new_nbuckets;

  if (h->frozen) {
    return;
  }
  num_used = h->num_entries + h->num_deleted;
  if (expandingp) {
    limit = FULL_RATIO * h->num_buckets;
    if (num_used < limit) {
      return;
    }
  } else {
    if (h->num_buckets <= MIN_BUCKETS) {
      return;
    }
    limit = MIN_RATIO * h->num_buckets;
    if (h->num_entries > limit) {
      return;
    }
  }
  
  new_nbuckets = calc_appropriate_nbuckets(h->num_entries);
  
  freelog(LOG_DEBUG, "%s hash table "
	  "(entry %u del %u used %u nbuck %u new %u %slimit %u)",
	  (new_nbuckets<h->num_buckets) ? "Shrinking" :
	  (new_nbuckets>h->num_buckets) ? "Expanding" : "Rehashing",
	  h->num_entries, h->num_deleted, num_used, 
	  h->num_buckets, new_nbuckets, expandingp?"up":"dn", limit);
  hash_resize_table(h, new_nbuckets);
}

/**************************************************************************
  Return pointer to bucket in hash table where key resides, or where it
  should go if it is to be a new key.  Note caller needs to provide
  pre-calculated hash_val (this is to avoid re-calculations).
  Return any bucket marked "deleted" in preference to using an
  unused bucket.  (But have to get to an unused bucket first, to
  know that the key is not in the table.  Use first such deleted
  to speed subsequent lookups on that key.)
**************************************************************************/
static struct hash_bucket *internal_lookup(const struct hash_table *h,
					   const void *key,
					   unsigned int hash_val)
{
  struct hash_bucket *bucket;
  struct hash_bucket *deleted = NULL;
  unsigned int i = hash_val;

  do {
    bucket = &h->buckets[i];
    switch (bucket->used) {
    case BUCKET_UNUSED:
      return deleted ? deleted : bucket;
    case BUCKET_USED:
      if (bucket->hash_val==hash_val
	  && h->fcmp(bucket->key, key)==0) { /* match */
	return bucket;
      }
      break;
    case BUCKET_DELETED:
      if (!deleted) {
	deleted = bucket;
      }
      break;
    default:
      die("Bad value %d in switch(bucket->used)", (int) bucket->used);
    }
    i++;
    if (i==h->num_buckets) {
      i=0;
    }
  } while (i!=hash_val);	/* catch loop all the way round  */

  if (deleted) {
    return deleted;
  }
  die("Full hash table -- and somehow did not resize!!");
  return NULL;
}

/**************************************************************************
  Insert entry: returns 1 if inserted, or 0 if there was already an entry
  with the same key, in which case the entry was not inserted.
**************************************************************************/
bool hash_insert(struct hash_table *h, const void *key, const void *data)
{
  struct hash_bucket *bucket;
  int hash_val;

  hash_maybe_expand(h);
  hash_val = HASH_VAL(h, key);
  bucket = internal_lookup(h, key, hash_val);
  if (bucket->used == BUCKET_USED) {
    return FALSE;
  }
  if (bucket->used == BUCKET_DELETED) {
    assert(h->num_deleted>0);
    h->num_deleted--;
  }
  bucket->key = key;
  bucket->data = data;
  bucket->used = BUCKET_USED;
  bucket->hash_val = hash_val;
  h->num_entries++;
  return TRUE;
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
    
  hash_maybe_expand(h);
  hash_val = HASH_VAL(h, key);
  bucket = internal_lookup(h, key, hash_val);
  if (bucket->used == BUCKET_USED) {
    ret = bucket->data;
  } else {
    ret = NULL;
    if (bucket->used == BUCKET_DELETED) {
      assert(h->num_deleted>0);
      h->num_deleted--;
    }
    h->num_entries++;
    bucket->used = BUCKET_USED;
  }
  bucket->key = key;
  bucket->data = data;
  bucket->hash_val = hash_val;
  return (void*)ret;
}

/**************************************************************************
  Delete an entry with specified key.  Returns user-data of deleted
  entry, or NULL if not found.
**************************************************************************/
void *hash_delete_entry(struct hash_table *h, const void *key)
{
  return hash_delete_entry_full(h, key, NULL);
}

/**************************************************************************
  Delete an entry with specified key.  Returns user-data of deleted
  entry, or NULL if not found.  old_key, if non-NULL, will be set to the
  key that was used for the bucket (the caller may need to free this
  value).
**************************************************************************/
void *hash_delete_entry_full(struct hash_table *h, const void *key,
			     void **old_key)
{
  struct hash_bucket *bucket;

  hash_maybe_shrink(h);  
  bucket = internal_lookup(h, key, HASH_VAL(h,key));
  if (bucket->used == BUCKET_USED) {
    const void *ret = bucket->data;

    if (old_key) {
      *old_key = (void *)bucket->key;
    }
    zero_hbucket(bucket);
    bucket->used = BUCKET_DELETED;
    h->num_deleted++;
    assert(h->num_entries>0);
    h->num_entries--;
    return (void*) ret;
  } else {
    if (old_key) {
      *old_key = NULL;
    }
    return NULL;
  }
}

/**************************************************************************
  Delete all entries of the hash.
**************************************************************************/
void hash_delete_all_entries(struct hash_table *h)
{
  while (hash_num_entries(h) > 0)
    (void) hash_delete_entry(h, hash_key_by_number(h, 0));
}

/**************************************************************************
  Lookup: return existence:
**************************************************************************/
bool hash_key_exists(const struct hash_table *h, const void *key)
{
  struct hash_bucket *bucket = internal_lookup(h, key, HASH_VAL(h,key));
  return (bucket->used == BUCKET_USED);
}

/**************************************************************************
  Lookup: return user-data, or NULL.
  (Note that in other respects NULL is treated as a valid value, this is
  merely intended as a convenience when caller never uses NULL as value.)
**************************************************************************/
void *hash_lookup_data(const struct hash_table *h, const void *key)
{
  struct hash_bucket *bucket = internal_lookup(h, key, HASH_VAL(h,key));
  return ((bucket->used == BUCKET_USED) ? (void*)bucket->data : NULL);
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
unsigned int hash_num_deleted(const struct hash_table *h)
{
  return h->num_deleted;
}

/**************************************************************************
  Enumeration: returns the pointer to a key. The keys are returned in
  random order.
**************************************************************************/
const void *hash_key_by_number(const struct hash_table *h,
			       unsigned int entry_number)
{
  unsigned int bucket_nr, counter = 0;
  assert(entry_number < h->num_entries);

  for (bucket_nr = 0; bucket_nr < h->num_buckets; bucket_nr++) {
    struct hash_bucket *bucket = &h->buckets[bucket_nr];

    if (bucket->used != BUCKET_USED)
      continue;

    if (entry_number == counter)
      return bucket->key;
    counter++;
  }
  die("never reached");
  return NULL;
}

/**************************************************************************
  Enumeration: returns the pointer to a value. 
**************************************************************************/
const void *hash_value_by_number(const struct hash_table *h,
				 unsigned int entry_number)
{
  return hash_lookup_data(h, hash_key_by_number(h, entry_number));
}
