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

/****************************************************************************
   A general-purpose generic hash table implementation.

   Based on implementation previous included in registry.c, but separated
   out so that can be used more generally.  Maybe we should just use glib?

      Original author:  David Pfitzner  dwp@mso.anu.edu.au

   A hash table maps keys to user data values, using a user-supplied hash
   function to do this efficiently. Here both keys and values are general
   data represented by (void*) pointers. Memory management of both keys
   and data is the responsibility of the caller: that is, the caller must
   ensure that the memory (especially for keys) remains valid (allocated)
   for as long as required (typically, the life of the genhash table).
   (Otherwise, to allocate keys internally would either have to restrict
   key type (e.g., strings), or have user-supplied function to duplicate
   a key type.  See further comments below.)

   User-supplied functions required are:
   key_val_func: map key to bucket number given number of buckets; should
                 map keys fairly evenly to range 0 to (num_buckets - 1)
                 inclusive.

   key_comp_func: compare keys for equality, necessary for lookups for keys
                  which map to the same genhash value. Keys which compare
                  equal should map to the same hash value. Returns 0 for
                  equality, so can use qsort-type comparison function (but
                  the hash table does not make use of the ordering
                  information if the return value is non-zero).

   Some constructors also accept following functions to be registered:
   key_copy_func: This is called when assigning a new value to a bucket.
   key_free_func: This is called when genhash no longer needs key construct.
                  Note that one key construct gets freed even when it is
                  replaced with another that is considered identical by
                  key_comp_func().
   data_copy_func: same as 'key_copy_func', but for data.
   data_free_func: same as 'key_free_func', but for data.

   Resize genhash table when deemed necessary by making and populating a new
   table.
****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

/* utility */
#include "log.h"
#include "mem.h"
#include "shared.h"     /* ARRAY_SIZE */

#include "genhash.h"

#define FULL_RATIO 0.75         /* consider expanding when above this */
#define MIN_RATIO 0.24          /* shrink when below this */

struct genhash_bucket {
  bool used;
  void *key;
  void *data;
  genhash_val_t hash_val; /* To avoid recalculating in lookup functions. */
};

/* Contents of the opaque type: */
struct genhash {
  struct genhash_bucket *buckets;
  genhash_val_fn_t key_val_func;
  genhash_comp_fn_t key_comp_func;
  genhash_copy_fn_t key_copy_func;
  genhash_free_fn_t key_free_func;
  genhash_copy_fn_t data_copy_func;
  genhash_free_fn_t data_free_func;
  size_t num_buckets;
  size_t num_entries;
  bool frozen;                  /* Do not auto-resize when set. */
  bool no_shrink;               /* Do not auto-shrink when set. */
};

struct genhash_iter {
  struct iterator vtable;
  const struct genhash_bucket *b, *end;
};

#define GENHASH_ITER(p) ((struct genhash_iter *) (p))


/****************************************************************************
  A supplied genhash function appropriate to nul-terminated strings.
  Prefers table sizes that are prime numbers.
****************************************************************************/
genhash_val_t genhash_str_val_func(const void *vkey, size_t num_buckets)
{
  const char *key = (const char *) vkey;
  unsigned long result = 0;

  for (; *key != '\0'; key++) {
    result *= 5; 
    result += *key;
  }
  result &= 0xFFFFFFFF; /* To make results independent of sizeof(long) */
  return (result % num_buckets);
}

/****************************************************************************
  A supplied function for comparison of nul-terminated strings:
****************************************************************************/
bool genhash_str_comp_func(const void *vkey1, const void *vkey2)
{
  return 0 == strcmp((const char *) vkey1, (const char *) vkey2);
}

/****************************************************************************
  Copy function for string allocation.
****************************************************************************/
void *genhash_str_copy_func(const void *vkey)
{
  return fc_strdup(NULL != vkey ? (const char *) vkey : "");
}

/****************************************************************************
  Free function for string allocation.
****************************************************************************/
void genhash_str_free_func(void *vkey)
{
#ifdef DEBUG
  fc_assert_ret(NULL != vkey);
#endif
  free(vkey);
}

/****************************************************************************
  A supplied genhash function which operates on the key pointer values
  themselves; this way a void* (or, with casting, a long) can be used
  as a key, and also without having allocated space for it.
***************************************************************************/
genhash_val_t genhash_ptr_val_func(const void *vkey, size_t num_buckets)
{
  unsigned long result = ((unsigned long) vkey);
  return (result % num_buckets);
}

/****************************************************************************
  A supplied function for comparison of the raw void pointers (or,
  with casting, longs).
****************************************************************************/
bool genhash_ptr_comp_func(const void *vkey1, const void *vkey2)
{
  /* Simplicity itself. */
  return vkey1 == vkey2;
}


/****************************************************************************
  Default function of type 'hash_copy_fn_t'.
****************************************************************************/
static void *genhash_default_copy_func(const void *ptr)
{
  return (void *) ptr;
}

/****************************************************************************
  Default function of type 'hash_free_fn_t'.
****************************************************************************/
static void genhash_default_free_func(void *ptr)
{
  /* Do nothing. */
}


/****************************************************************************
  Calculate a "reasonable" number of buckets for a given number of entries.
  Gives a prime number far from powers of 2, allowing at least a factor of
  2 from the given number of entries for breathing room.

  Generalized restrictions on the behavior of this function:
  * MIN_BUCKETS <= genhash_calc_num_buckets(x)
  * genhash_calc_num_buckets(x) * MIN_RATIO < x  whenever
    x > MIN_BUCKETS * MIN_RATIO.
  * genhash_calc_num_buckets(x) * FULL_RATIO > x.
  This one is more of a recommendation, to ensure enough free space:
  * genhash_calc_num_buckets(x) >= 2 * x.
****************************************************************************/
#define MIN_BUCKETS 29  /* Historical purposes. */
static size_t genhash_calc_num_buckets(size_t num_entries)
{
  /* A bunch of prime numbers close to successive elements of the sequence
   * A_n = 3 * 2 ^ n; to be used for table sizes. */
  static const size_t sizes[] = {
    MIN_BUCKETS,          53,         97,           193,
    389,       769,       1543,       3079,         6151,
    12289,     24593,     49157,      98317,        196613,
    393241,    786433,    1572869,    3145739,      6291469,
    12582917,  25165843,  50331653,   100663319,    201326611,
    402653189, 805306457, 1610612741, 3221225473ul, 4294967291ul
  };
  const size_t *pframe = sizes, *pmid;
  int fsize = ARRAY_SIZE(sizes) - 1, lpart;

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

/****************************************************************************
  Calculate genhash value given genhash_table and key.
****************************************************************************/
static inline genhash_val_t genhash_val_calc(const struct genhash *pgenhash,
                                             const void *key)
{
  return pgenhash->key_val_func(key, pgenhash->num_buckets);
}

/****************************************************************************
  Internal constructor, specifying exact number of buckets.
  Allows to specify functions to free the memory allocated for the key and
  user-data that get called when removing the bucket from the hash table or
  changing key/user-data values.

  NB: Be sure to check the "copy constructor" genhash_copy() if you change
  this function significantly.
****************************************************************************/
static struct genhash *
genhash_new_nbuckets(genhash_val_fn_t key_val_func,
                     genhash_comp_fn_t key_comp_func,
                     genhash_copy_fn_t key_copy_func,
                     genhash_free_fn_t key_free_func,
                     genhash_copy_fn_t data_copy_func,
                     genhash_free_fn_t data_free_func,
                     size_t num_buckets)
{
  struct genhash *pgenhash = fc_malloc(sizeof(*pgenhash));

  log_debug("New genhash table with %lu buckets",
            (long unsigned) num_buckets);

  pgenhash->buckets = fc_calloc(num_buckets, sizeof(*pgenhash->buckets));
  pgenhash->key_val_func = key_val_func;
  pgenhash->key_comp_func = key_comp_func;
  pgenhash->key_copy_func = (NULL != key_copy_func ? key_copy_func
                             : genhash_default_copy_func);
  pgenhash->key_free_func = (NULL != key_free_func ? key_free_func
                             : genhash_default_free_func);
  pgenhash->data_copy_func = (NULL != data_copy_func ? data_copy_func
                              : genhash_default_copy_func);
  pgenhash->data_free_func = (NULL != data_free_func ? data_free_func
                              : genhash_default_free_func);
  pgenhash->num_buckets = num_buckets;
  pgenhash->num_entries = 0;
  pgenhash->frozen = FALSE;
  pgenhash->no_shrink = FALSE;

  return pgenhash;
}

/****************************************************************************
  Constructor specifying number of entries.
  Allows to specify functions to free the memory allocated for the key and
  user-data that get called when removing the bucket from the hash table or
  changing key/user-data values.
****************************************************************************/
struct genhash *
genhash_new_nentries_full(genhash_val_fn_t key_val_func,
                          genhash_comp_fn_t key_comp_func,
                          genhash_copy_fn_t key_copy_func,
                          genhash_free_fn_t key_free_func,
                          genhash_copy_fn_t data_copy_func,
                          genhash_free_fn_t data_free_func,
                          size_t nentries)
{
  return genhash_new_nbuckets(key_val_func, key_comp_func,
                              key_copy_func, key_free_func,
                              data_copy_func, data_free_func,
                              genhash_calc_num_buckets(nentries));
}

/****************************************************************************
  Constructor specifying number of entries.
****************************************************************************/
struct genhash *genhash_new_nentries(genhash_val_fn_t key_val_func,
                                     genhash_comp_fn_t key_comp_func,
                                     size_t nentries)
{
  return genhash_new_nbuckets(key_val_func, key_comp_func,
                              NULL, NULL, NULL, NULL,
                              genhash_calc_num_buckets(nentries));
}

/****************************************************************************
  Constructor with unspecified number of entries.
  Allows to specify functions to free the memory allocated for the key and
  user-data that get called when removing the bucket from the hash table or
  changing key/user-data values.
****************************************************************************/
struct genhash *genhash_new_full(genhash_val_fn_t key_val_func,
                                 genhash_comp_fn_t key_comp_func,
                                 genhash_copy_fn_t key_copy_func,
                                 genhash_free_fn_t key_free_func,
                                 genhash_copy_fn_t data_copy_func,
                                 genhash_free_fn_t data_free_func)
{
  return genhash_new_nbuckets(key_val_func, key_comp_func,
                              key_copy_func, key_free_func,
                              data_copy_func, data_free_func, MIN_BUCKETS);
}

/****************************************************************************
  Constructor with unspecified number of entries.
****************************************************************************/
struct genhash *genhash_new(genhash_val_fn_t key_val_func,
                            genhash_comp_fn_t key_comp_func)
{
  return genhash_new_nbuckets(key_val_func, key_comp_func,
                              NULL, NULL, NULL, NULL, MIN_BUCKETS);
}

/**************************************************************************
  Destructor: free internal memory.
**************************************************************************/
void genhash_destroy(struct genhash *pgenhash)
{
  fc_assert_ret(NULL != pgenhash);
  pgenhash->no_shrink = TRUE;
  genhash_clear(pgenhash);
  free(pgenhash->buckets);
  free(pgenhash);
}

/****************************************************************************
  Resize the genhash table: create a new table, insert, then remove the old
  and assign.
****************************************************************************/
static void genhash_resize_table(struct genhash *pgenhash,
                                 size_t new_nbuckets)
{
  struct genhash *pgenhash_new;
  const struct genhash_bucket *b, *end;
  bool success;

  fc_assert_ret(new_nbuckets >= pgenhash->num_entries);

  pgenhash_new = genhash_new_nbuckets(pgenhash->key_val_func,
                                      pgenhash->key_comp_func,
                                      pgenhash->key_copy_func,
                                      pgenhash->key_free_func,
                                      pgenhash->data_copy_func,
                                      pgenhash->data_free_func,
                                      new_nbuckets);
  pgenhash_new->frozen = TRUE;

  end = pgenhash->buckets + pgenhash->num_buckets;
  for(b = pgenhash->buckets; b < end; b++) {
    if (b->used) {
      success = genhash_insert(pgenhash_new, b->key, b->data);
      fc_assert(TRUE == success);
    }
  }
  pgenhash_new->frozen = FALSE;

  free(pgenhash->buckets);
  *pgenhash = *pgenhash_new;
  free(pgenhash_new);
}

/****************************************************************************
  Call this when an entry might be added or deleted: resizes the genhash
  table if seems like a good idea.  Count deleted entries in check
  because efficiency may be degraded if there are too many deleted
  entries.  But for determining new size, ignore deleted entries,
  since they'll be removed by rehashing.
****************************************************************************/
#define genhash_maybe_expand(htab) genhash_maybe_resize((htab), TRUE)
#define genhash_maybe_shrink(htab) genhash_maybe_resize((htab), FALSE)
static void genhash_maybe_resize(struct genhash *pgenhash, bool expandingp)
{
  size_t limit, new_nbuckets;

  if (pgenhash->frozen
      || (!expandingp && pgenhash->no_shrink)) {
    return;
  }
  if (expandingp) {
    limit = FULL_RATIO * pgenhash->num_buckets;
    if (pgenhash->num_entries < limit) {
      return;
    }
  } else {
    if (pgenhash->num_buckets <= MIN_BUCKETS) {
      return;
    }
    limit = MIN_RATIO * pgenhash->num_buckets;
    if (pgenhash->num_entries > limit) {
      return;
    }
  }

  new_nbuckets = genhash_calc_num_buckets(pgenhash->num_entries);

  log_debug("%s genhash (entries = %lu, buckets =  %lu, new = %lu, "
            "%s limit = %lu)",
            (new_nbuckets < pgenhash->num_buckets ? "Shrinking"
             : (new_nbuckets > pgenhash->num_buckets
                ? "Expanding" : "Rehashing")),
            (long unsigned) pgenhash->num_entries,
            (long unsigned) pgenhash->num_buckets,
            (long unsigned) new_nbuckets,
            expandingp ? "up": "down", (long unsigned) limit);
  genhash_resize_table(pgenhash, new_nbuckets);
}

/****************************************************************************
  Return pointer to bucket in genhash table where key resides, or where it
  should go if it is to be a new key. Note caller needs to provide
  pre-calculated genhash_val (this is to avoid re-calculations).
****************************************************************************/
static struct genhash_bucket *
genhash_bucket_lookup(const struct genhash *pgenhash,
                      const void *key, genhash_val_t hash_val)
{
  struct genhash_bucket *b = pgenhash->buckets + hash_val;
  const struct genhash_bucket *const start = b;
  const struct genhash_bucket *const end = (pgenhash->buckets
                                            + pgenhash->num_buckets);

  do {
    if (!b->used) {
      return b;
    } else if (b->hash_val == hash_val
               && pgenhash->key_comp_func(b->key, key)) {
      return b;
    }
    b++;
    if (b >= end) {
      b = pgenhash->buckets;
    }
  } while (b != start); /* Catch loop all the way round. */

  fc_assert_msg(b != start, /* Always fails. */
                "Full genhash table -- and somehow did not resize!!");
  return NULL;
}

/****************************************************************************
  Function to store from invalid data.
****************************************************************************/
static inline void genhash_default_get(void **pkey, void **data)
{
  if (NULL != pkey) {
    *pkey = NULL;
  }
  if (NULL != data) {
    *data = NULL;
  }
}

/****************************************************************************
  Function to store from valid data.
****************************************************************************/
static inline void genhash_bucket_get(const struct genhash_bucket *bucket,
                                      void **pkey, void **data)
{
  if (NULL != pkey) {
    *pkey = bucket->key;
  }
  if (NULL != data) {
    *data = bucket->data;
  }
}

/****************************************************************************
  Call the free callbacks.
****************************************************************************/
static inline void genhash_bucket_free(struct genhash *pgenhash,
                                       struct genhash_bucket *bucket)
{
  pgenhash->key_free_func(bucket->key);
  pgenhash->data_free_func(bucket->data);
}

/****************************************************************************
  Call the copy callbacks.
****************************************************************************/
static inline void genhash_bucket_set(struct genhash *pgenhash,
                                      struct genhash_bucket *bucket,
                                      const void *key, const void *data)
{
  bucket->key = pgenhash->key_copy_func(key);
  bucket->data = pgenhash->data_copy_func(data);
}

/****************************************************************************
  Prevent or allow the genhash table automatically shrinking. Returns the
  old value of the setting.
****************************************************************************/
bool genhash_set_no_shrink(struct genhash *pgenhash, bool no_shrink)
{
  bool old;

  fc_assert_ret_val(NULL != pgenhash, FALSE);
  old = pgenhash->no_shrink;
  pgenhash->no_shrink = no_shrink;
  return old;
}

/****************************************************************************
  Returns the number of entries in the genhash table.
****************************************************************************/
size_t genhash_size(const struct genhash *pgenhash)
{
  fc_assert_ret_val(NULL != pgenhash, 0);
  return pgenhash->num_entries;
}

/****************************************************************************
  Returns the number of buckets in the genhash table.
****************************************************************************/
size_t genhash_capacity(const struct genhash *pgenhash)
{
  fc_assert_ret_val(NULL != pgenhash, 0);
  return pgenhash->num_buckets;
}

/****************************************************************************
  Returns a newly allocated mostly deep copy of the given genhash table.
****************************************************************************/
struct genhash *genhash_copy(const struct genhash *pgenhash)
{
  struct genhash *new_genhash;
  size_t size;

  fc_assert_ret_val(NULL != pgenhash, NULL);

  new_genhash = fc_malloc(sizeof(*new_genhash));

  /* Copy fields. */
  *new_genhash = *pgenhash;

  /* But make fresh buckets. */
  size = new_genhash->num_buckets * sizeof(*new_genhash->buckets);
  new_genhash->buckets = fc_malloc(size);

  /* Copy the buckets. */
  if (genhash_default_copy_func == new_genhash->key_copy_func
      && genhash_default_copy_func == new_genhash->data_copy_func) {
    /* NB: Default callbacks, shallow copy. */
    memcpy(new_genhash->buckets, pgenhash->buckets, size);
  } else {
    /* We have callbacks. */
    struct genhash_bucket *b = new_genhash->buckets;
    struct genhash_bucket *const end =
        new_genhash->buckets + new_genhash->num_buckets;
    const struct genhash_bucket *src = pgenhash->buckets;

    memset(b, 0, size);
    for (; b < end; b++, src++) {
      if (src->used) {
        genhash_bucket_set(new_genhash, b, src->key, src->data);
        b->used = TRUE;
      }
    }
  }

  return new_genhash;
}

/****************************************************************************
  Remove all entries of the genhash table.
****************************************************************************/
void genhash_clear(struct genhash *pgenhash)
{
  fc_assert_ret(NULL != pgenhash);

  if (genhash_default_free_func != pgenhash->key_free_func
      || genhash_default_free_func != pgenhash->data_free_func) {
    /* We have callbacks. */
    struct genhash_bucket *b = pgenhash->buckets;
    struct genhash_bucket *const end =
        pgenhash->buckets + pgenhash->num_buckets;

    for (; b < end; b++) {
      if (b->used) {
        genhash_bucket_free(pgenhash, b);
      }
    }
  }
  memset(pgenhash->buckets, 0,
         sizeof(*pgenhash->buckets) * pgenhash->num_buckets);

  pgenhash->num_entries = 0;
  genhash_maybe_shrink(pgenhash);
}

/****************************************************************************
  Insert entry: returns TRUE if inserted, or FALSE if there was already an
  entry with the same key, in which case the entry was not inserted.
****************************************************************************/
bool genhash_insert(struct genhash *pgenhash, const void *key,
                    const void *data)
{
  struct genhash_bucket *bucket;
  genhash_val_t hash_val;

  fc_assert_ret_val(NULL != pgenhash, FALSE);

  genhash_maybe_expand(pgenhash);
  hash_val = genhash_val_calc(pgenhash, key);
  bucket = genhash_bucket_lookup(pgenhash, key, hash_val);
  if (bucket->used) {
    return FALSE;
  } else {
    genhash_bucket_set(pgenhash, bucket, key, data);
    bucket->hash_val = hash_val;
    bucket->used = TRUE;
    pgenhash->num_entries++;
    return TRUE;
  }
}

/****************************************************************************
  Insert entry, replacing any existing entry which has the same key.
  Returns TRUE if a data have been replaced, FALSE if it was a simple
  insertion.
****************************************************************************/
bool genhash_replace(struct genhash *pgenhash, const void *key,
                     const void *data)
{
  return genhash_replace_full(pgenhash, key, data, NULL, NULL);
}

/****************************************************************************
  Insert entry, replacing any existing entry which has the same key.
  Returns TRUE if a data have been replaced, FALSE if it was a simple
  insertion.

  Returns in 'old_pkey' and 'old_pdata' the old content of the bucket if
  they are not NULL. NB: It can returns freed pointers if free functions
  were supplied to the genhash table.
****************************************************************************/
bool genhash_replace_full(struct genhash *pgenhash, const void *key,
                          const void *data, void **old_pkey,
                          void **old_pdata)
{
  struct genhash_bucket *bucket;
  genhash_val_t hash_val;

  fc_assert_action(NULL != pgenhash,
                   genhash_default_get(old_pkey, old_pdata); return FALSE);

  genhash_maybe_expand(pgenhash);
  hash_val = genhash_val_calc(pgenhash, key);
  bucket = genhash_bucket_lookup(pgenhash, key, hash_val);
  if (bucket->used) {
    /* Replace. */
    genhash_bucket_get(bucket, old_pkey, old_pdata);
    genhash_bucket_free(pgenhash, bucket);
    genhash_bucket_set(pgenhash, bucket, key, data);
    bucket->hash_val = hash_val;
    return TRUE;
  } else {
    /* Insert. */
    genhash_default_get(old_pkey, old_pdata);
    genhash_bucket_set(pgenhash, bucket, key, data);
    bucket->hash_val = hash_val;
    bucket->used = TRUE;
    pgenhash->num_entries++;
    return FALSE;
  }
}

/****************************************************************************
  Lookup data. Return TRUE on success, then pdata - if not NULL will be set
  to the data value.
****************************************************************************/
bool genhash_lookup(const struct genhash *pgenhash, const void *key,
                    void **pdata)
{
  const struct genhash_bucket *bucket;

  fc_assert_action(NULL != pgenhash,
                   genhash_default_get(NULL, pdata); return FALSE);

  bucket = genhash_bucket_lookup(pgenhash, key,
                                 genhash_val_calc(pgenhash, key));
  if (bucket->used) {
    genhash_bucket_get(bucket, NULL, pdata);
    return TRUE;
  } else {
    genhash_default_get(NULL, pdata);
    return FALSE;
  }
}

/****************************************************************************
  Delete an entry from the genhash table. Returns TRUE on success.
****************************************************************************/
bool genhash_remove(struct genhash *pgenhash, const void *key)
{
  return genhash_remove_full(pgenhash, key, NULL, NULL);
}

/****************************************************************************
  Delete an entry from the genhash table. Returns TRUE on success.

  Returns in 'deleted_pkey' and 'deleted_pdata' the old contents of the
  deleted entry if not NULL. NB: It can returns freed pointers if free
  functions were supplied to the genhash table.
****************************************************************************/
bool genhash_remove_full(struct genhash *pgenhash, const void *key,
                         void **deleted_pkey, void **deleted_pdata)
{
  struct genhash_bucket *bucket;

  fc_assert_action(NULL != pgenhash,
                   genhash_default_get(deleted_pkey, deleted_pdata);
                   return FALSE);

  genhash_maybe_shrink(pgenhash);
  bucket = genhash_bucket_lookup(pgenhash, key,
                                 genhash_val_calc(pgenhash, key));
  if (bucket->used) {
    genhash_bucket_get(bucket, deleted_pkey, deleted_pdata);
    bucket->used = FALSE;
    fc_assert(0 < pgenhash->num_entries);
    pgenhash->num_entries--;
    return TRUE;
  } else {
    genhash_default_get(deleted_pkey, deleted_pdata);
    return FALSE;
  }
}


/****************************************************************************
  "Sizeof" function implementation for generic_iterate genhash iterators.
****************************************************************************/
size_t genhash_iter_sizeof(void)
{
  return sizeof(struct genhash_iter);
}

/****************************************************************************
  Helper function for genhash (key, value) pair iteration.
****************************************************************************/
void *genhash_iter_key(const struct iterator *genhash_iter)
{
  struct genhash_iter *iter = GENHASH_ITER(genhash_iter);
  return (void *) iter->b->key;
}

/****************************************************************************
  Helper function for genhash (key, value) pair iteration.
****************************************************************************/
void *genhash_iter_value(const struct iterator *genhash_iter)
{
  struct genhash_iter *iter = GENHASH_ITER(genhash_iter);
  return (void *) iter->b->data;
}

/****************************************************************************
  Iterator interface 'next' function implementation.
****************************************************************************/
static void genhash_iter_next(struct iterator *genhash_iter)
{
  struct genhash_iter *iter = GENHASH_ITER(genhash_iter);
  do {
    iter->b++;
  } while (iter->b < iter->end && !iter->b->used);
}

/****************************************************************************
  Iterator interface 'get' function implementation. This just returns the
  iterator itself, so you would need to use genhash_iter_get_key/value to
  get the actual keys and values.
****************************************************************************/
static void *genhash_iter_get(const struct iterator *genhash_iter)
{
  return (void *) genhash_iter;
}

/****************************************************************************
  Iterator interface 'valid' function implementation.
****************************************************************************/
static bool genhash_iter_valid(const struct iterator *genhash_iter)
{
  struct genhash_iter *iter = GENHASH_ITER(genhash_iter);
  return iter->b < iter->end;
}

/****************************************************************************
  Common genhash iterator initializer.
****************************************************************************/
static inline struct iterator *
genhash_iter_init_common(struct genhash_iter *iter,
                         const struct genhash *pgenhash,
                         void * (*get) (const struct iterator *))
{
  if (NULL == pgenhash) {
    return invalid_iter_init(ITERATOR(iter));
  }

  iter->vtable.next = genhash_iter_next;
  iter->vtable.get = get;
  iter->vtable.valid = genhash_iter_valid;
  iter->b = pgenhash->buckets - 1;
  iter->end = pgenhash->buckets + pgenhash->num_buckets;

  /* Seek to the first used bucket. */
  genhash_iter_next(ITERATOR(iter));

  return ITERATOR(iter);
}

/****************************************************************************
  Returns an iterator that iterates over both keys and values of the genhash
  table. NB: iterator_get() returns an iterator pointer, so use the helper
  functions genhash_iter_get_{key,value} to access the key and value.
****************************************************************************/
struct iterator *genhash_iter_init(struct genhash_iter *iter,
                                   const struct genhash *pgenhash)
{
  return genhash_iter_init_common(iter, pgenhash, genhash_iter_get);
}

/****************************************************************************
  Returns an iterator over the genhash table's k genhashgenhashenhashys.
****************************************************************************/
struct iterator *genhash_key_iter_init(struct genhash_iter *iter,
                                       const struct genhash *pgenhash)
{
  return genhash_iter_init_common(iter, pgenhash, genhash_iter_key);
}

/****************************************************************************
  Returns an iterator over the hash table's values.
****************************************************************************/
struct iterator *genhash_value_iter_init(struct genhash_iter *iter,
                                         const struct genhash *pgenhash)
{
  return genhash_iter_init_common(iter, pgenhash, genhash_iter_value);
}
