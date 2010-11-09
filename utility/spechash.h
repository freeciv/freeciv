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

/* spechashs: "specific genhash".
 *
 * This file is used to implement a "specific" genhash.
 * That is, a (sometimes) type-checked genhash. (Or at least a
 * genhash with related functions with distinctly typed parameters.)
 *
 * Before including this file, you must define the following:
 *   SPECHASH_TAG - this tag will be used to form names for functions etc.
 *   SPECHASH_KEY_TYPE - the typed genhash will use this type as key.
 *   SPECHASH_DATA_TYPE - the typed genhash will use this type as data.
 * You may also define:
 *   SPECHASH_KEY_VAL - The default hash function.
 *   SPECHASH_KEY_COMP - The default hash key comparator function.
 *   SPECHASH_KEY_COPY - The default key copy function.
 *   SPECHASH_KEY_FREE - The default key free function.
 *   SPECHASH_DATA_COMP - The default data comparator function.
 *   SPECHASH_DATA_COPY - The default data copy function.
 *   SPECHASH_DATA_FREE - The default data free function.
 *   SPECHASH_KEY_TO_PTR - A function or macro to convert a key to pointer.
 *   SPECHASH_PTR_TO_KEY - A function or macro to convert a pointer to key.
 *   SPECHASH_DATA_TO_PTR - A function or macro to convert a data to pointer.
 *   SPECHASH_PTR_TO_DATA - A function or macro to convert a pointer to data.
 * At the end of this file, these (and other defines) are undef-ed.
 *
 * Assuming SPECHASH_TAG were 'foo', SPECHASH_KEY_TYPE were 'key_t', and
 * SPECHASH_DATA_TYPE were 'data_t'.
 * including this file would provide a struct definition for:
 *    struct foo_hash;
 *    struct foo_hash_iter;
 *
 * function typedefs:
 *    typedef genhash_val_t (*foo_hash_key_val_fn_t) (const key_t, size_t);
 *    typedef bool (*foo_hash_key_comp_fn_t) (const key_t, const key_t);
 *    typedef key_t (*foo_hash_key_copy_fn_t) (const key_t);
 *    typedef void (*foo_hash_key_free_fn_t) (key_t);
 *    typedef bool (*foo_hash_data_comp_fn_t) (const data_t, const data_t);
 *    typedef data_t (*foo_hash_data_copy_fn_t) (const data_t);
 *    typedef void (*foo_hash_data_free_fn_t) (data_t);
 *
 * and prototypes for the following functions:
 *    struct foo_hash *foo_hash_new(void);
 *    struct foo_hash *foo_hash_new_full(foo_hash_key_val_fn_t key_val,
 *                                       foo_hash_key_comp_fn_t key_comp,
 *                                       foo_hash_key_copy_fn_t key_copy,
 *                                       foo_hash_key_free_fn_t key_free,
 *                                       foo_hash_data_copy_fn_t data_val,
 *                                       foo_hash_data_free_fn_t data_free);
 *    struct foo_hash *foo_hash_new_nentries(size_t nentries);
 *    struct foo_hash *
 *    foo_hash_new_nentries_full(foo_hash_key_val_fn_t key_val,
 *                               foo_hash_key_comp_fn_t key_comp,
 *                               foo_hash_key_copy_fn_t key_copy,
 *                               foo_hash_key_free_fn_t key_free,
 *                               foo_hash_data_copy_fn_t data_val,
 *                               foo_hash_data_free_fn_t data_free,
 *                               size_t nentries);
 *    void foo_hash_destroy(struct foo_hash *phash);
 *    bool foo_hash_set_no_shrink(struct foo_hash *phash, bool no_shrink);
 *    size_t foo_hash_size(const struct foo_hash *phash);
 *    size_t foo_hash_capacity(const struct foo_hash *phash);
 *    struct foo_hash *foo_hash_copy(const struct foo_hash *phash);
 *    void foo_hash_clear(struct foo_hash *phash);
 *    bool foo_hash_insert(struct foo_hash *phash, const key_t key,
 *                         const data_t data);
 *    bool foo_hash_replace(struct foo_hash *phash, const key_t key,
 *                          const data_t data);
 *    bool foo_hash_replace_full(struct foo_hash *phash, const key_t key,
 *                               const data_t data, key_t *old_pkey,
 *                               data_t *old_pdata);
 *    bool foo_hash_lookup(const struct foo_hash *phash, const key_t key,
 *                         data_t *pdata);
 *    bool foo_hash_remove(struct foo_hash *phash, const key_t key);
 *    bool foo_hash_remove_full(struct foo_hash *phash, const key_t key,
 *                              key_t *deleted_pkey, data_t *deleted_pdata);
 *
 *    bool foo_hashs_are_equal(const struct foo_hash *phash1,
 *                             const struct foo_hash *phash2);
 *    bool foo_hashs_are_equal_full(const struct foo_hash *phash1,
 *                                  const struct foo_hash *phash2,
 *                                  foo_hash_data_comp_fn_t data_comp_func);
 *
 *    size_t foo_hash_iter_sizeof(void);
 *    struct iterator *foo_hash_iter_init(struct foo_hash_iter *iter,
 *                                        const struct foo_hash *phash);
 *    struct iterator *foo_hash_key_iter_init(struct foo_hash_iter *iter,
 *                                        const struct foo_hash *phash);
 *    struct iterator *foo_hash_value_init(struct foo_hash_iter *iter,
 *                                         const struct foo_hash *phash);
 *
 * You should also define yourself (this file cannot do this for you):
 * #define foo_hash_data_iterate(phash, data)                               \
 *   TYPED_HASH_DATA_ITERATE(data_t, phash, data)
 * #define foo_hash_data_iterate_end HASH_DATA_ITERATE_END
 *
 * #define foo_hash_keys_iterate(phash, key)                                \
 *   TYPED_HASH_KEYS_ITERATE(key_t, phash, key)
 * #define foo_hash_keys_iterate_end HASH_KEYS_ITERATE_END
 *
 * #define foo_hash_iterate(phash, key, data)                               \
 *   TYPED_HASH_ITERATE(key_t, data_t, phash, key, data)
 * #define foo_hash_iterate_end HASH_ITERATE_END
 *
 * Note this is not protected against multiple inclusions; this is so that
 * you can have multiple different speclists. For each speclist, this file
 * should be included _once_, inside a .h file which _is_ itself protected
 * against multiple inclusions. */

/* utility */
#include "genhash.h"
#include "iterator.h"

#ifndef SPECHASH_TAG
#error Must define a SPECHASH_TAG to use this header
#endif
#ifndef SPECHASH_KEY_TYPE
#error Must define a SPECHASH_KEY_TYPE to use this header
#endif
#ifndef SPECHASH_DATA_TYPE
#error Must define a SPECHASH_DATA_TYPE to use this header
#endif

/* Default functions. */
#ifndef SPECHASH_KEY_VAL
#define SPECHASH_KEY_VAL genhash_ptr_val_func
#endif
#ifndef SPECHASH_KEY_COMP
#define SPECHASH_KEY_COMP genhash_ptr_comp_func
#endif
#ifndef SPECHASH_KEY_COPY
#define SPECHASH_KEY_COPY NULL
#endif
#ifndef SPECHASH_KEY_FREE
#define SPECHASH_KEY_FREE NULL
#endif
#ifndef SPECHASH_DATA_COMP
#define SPECHASH_DATA_COMP NULL
#endif
#ifndef SPECHASH_DATA_COPY
#define SPECHASH_DATA_COPY NULL
#endif
#ifndef SPECHASH_DATA_FREE
#define SPECHASH_DATA_FREE NULL
#endif

/* Other functions or macros. */
#ifndef SPECHASH_KEY_TO_PTR
#define SPECHASH_KEY_TO_PTR(key) (key)
#endif
#ifndef SPECHASH_PTR_TO_KEY
#define SPECHASH_PTR_TO_KEY(ptr) (ptr)
#endif
#ifndef SPECHASH_DATA_TO_PTR
#define SPECHASH_DATA_TO_PTR(data) (data)
#endif
#ifndef SPECHASH_PTR_TO_DATA
#define SPECHASH_PTR_TO_DATA(ptr) (ptr)
#endif

#define SPECHASH_PASTE_(x, y) x ## y
#define SPECHASH_PASTE(x, y) SPECHASH_PASTE_(x, y)

#define SPECHASH_HASH struct SPECHASH_PASTE(SPECHASH_TAG, _hash)
#define SPECHASH_ITER struct SPECHASH_PASTE(SPECHASH_TAG, _hash_iter)
#define SPECHASH_FOO(suffix) SPECHASH_PASTE(SPECHASH_TAG, suffix)

/* Dummy type. Actually a genhash, and not defined anywhere. */
SPECHASH_HASH;

/* Dummy type. Actually a genhash_iter, and not defined anywhere. */
SPECHASH_ITER;

/* Function related typedefs. */
typedef genhash_val_t
(*SPECHASH_FOO(_hash_key_val_fn_t)) (const SPECHASH_KEY_TYPE, size_t);
typedef bool (*SPECHASH_FOO(_hash_key_comp_fn_t)) (const SPECHASH_KEY_TYPE,
                                                   const SPECHASH_KEY_TYPE);
typedef SPECHASH_KEY_TYPE
(*SPECHASH_FOO(_hash_key_copy_fn_t)) (const SPECHASH_KEY_TYPE);
typedef void (*SPECHASH_FOO(_hash_key_free_fn_t)) (SPECHASH_KEY_TYPE);
typedef bool
(*SPECHASH_FOO(_hash_data_comp_fn_t)) (const SPECHASH_DATA_TYPE,
                                       const SPECHASH_DATA_TYPE);
typedef SPECHASH_DATA_TYPE
(*SPECHASH_FOO(_hash_data_copy_fn_t)) (const SPECHASH_DATA_TYPE);
typedef void (*SPECHASH_FOO(_hash_data_free_fn_t)) (SPECHASH_DATA_TYPE);

/****************************************************************************
  Create a new spechash.
****************************************************************************/
static inline SPECHASH_HASH *SPECHASH_FOO(_hash_new) (void)
fc__warn_unused_result;

static inline SPECHASH_HASH *SPECHASH_FOO(_hash_new) (void)
{
  return ((SPECHASH_HASH *)
          genhash_new_full((genhash_val_fn_t) SPECHASH_KEY_VAL,
                           (genhash_comp_fn_t) SPECHASH_KEY_COMP,
                           (genhash_copy_fn_t) SPECHASH_KEY_COPY,
                           (genhash_free_fn_t) SPECHASH_KEY_FREE,
                           (genhash_copy_fn_t) SPECHASH_DATA_COPY,
                           (genhash_free_fn_t) SPECHASH_DATA_FREE));
}

/****************************************************************************
  Create a new spechash with a set of control functions.
****************************************************************************/
static inline SPECHASH_HASH *
SPECHASH_FOO(_hash_new_full) (SPECHASH_FOO(_hash_key_val_fn_t) key_val_func,
                              SPECHASH_FOO(_hash_key_comp_fn_t)
                              key_comp_func,
                              SPECHASH_FOO(_hash_key_copy_fn_t)
                              key_copy_func,
                              SPECHASH_FOO(_hash_key_free_fn_t)
                              key_free_func,
                              SPECHASH_FOO(_hash_data_copy_fn_t)
                              data_copy_func,
                              SPECHASH_FOO(_hash_data_free_fn_t)
                              data_free_func)
fc__warn_unused_result;

static inline SPECHASH_HASH *
SPECHASH_FOO(_hash_new_full) (SPECHASH_FOO(_hash_key_val_fn_t) key_val_func,
                              SPECHASH_FOO(_hash_key_comp_fn_t)
                              key_comp_func,
                              SPECHASH_FOO(_hash_key_copy_fn_t)
                              key_copy_func,
                              SPECHASH_FOO(_hash_key_free_fn_t)
                              key_free_func,
                              SPECHASH_FOO(_hash_data_copy_fn_t)
                              data_copy_func,
                              SPECHASH_FOO(_hash_data_free_fn_t)
                              data_free_func)
{
  return ((SPECHASH_HASH *)
          genhash_new_full((genhash_val_fn_t) key_val_func,
                           (genhash_comp_fn_t) key_comp_func,
                           (genhash_copy_fn_t) key_copy_func,
                           (genhash_free_fn_t) key_free_func,
                           (genhash_copy_fn_t) data_copy_func,
                           (genhash_free_fn_t) data_free_func));
}

/****************************************************************************
  Create a new spechash with n entries.
****************************************************************************/
static inline SPECHASH_HASH *
SPECHASH_FOO(_hash_new_nentries) (size_t nentries)
fc__warn_unused_result;

static inline SPECHASH_HASH *
SPECHASH_FOO(_hash_new_nentries) (size_t nentries)
{
  return ((SPECHASH_HASH *)
          genhash_new_nentries_full((genhash_val_fn_t) SPECHASH_KEY_VAL,
                                    (genhash_comp_fn_t) SPECHASH_KEY_COMP,
                                    (genhash_copy_fn_t) SPECHASH_KEY_COPY,
                                    (genhash_free_fn_t) SPECHASH_KEY_FREE,
                                    (genhash_copy_fn_t) SPECHASH_DATA_COPY,
                                    (genhash_free_fn_t) SPECHASH_DATA_FREE,
                                    nentries));
}

/****************************************************************************
  Create a new spechash with n entries and a set of control functions.
****************************************************************************/
static inline SPECHASH_HASH *
SPECHASH_FOO(_hash_new_nentries_full) (SPECHASH_FOO(_hash_key_val_fn_t)
                                       key_val_func,
                                       SPECHASH_FOO(_hash_key_comp_fn_t)
                                       key_comp_func,
                                       SPECHASH_FOO(_hash_key_copy_fn_t)
                                       key_copy_func,
                                       SPECHASH_FOO(_hash_key_free_fn_t)
                                       key_free_func,
                                       SPECHASH_FOO(_hash_data_copy_fn_t)
                                       data_copy_func,
                                       SPECHASH_FOO(_hash_data_free_fn_t)
                                       data_free_func, size_t nentries)
fc__warn_unused_result;

static inline SPECHASH_HASH *
SPECHASH_FOO(_hash_new_nentries_full) (SPECHASH_FOO(_hash_key_val_fn_t)
                                       key_val_func,
                                       SPECHASH_FOO(_hash_key_comp_fn_t)
                                       key_comp_func,
                                       SPECHASH_FOO(_hash_key_copy_fn_t)
                                       key_copy_func,
                                       SPECHASH_FOO(_hash_key_free_fn_t)
                                       key_free_func,
                                       SPECHASH_FOO(_hash_data_copy_fn_t)
                                       data_copy_func,
                                       SPECHASH_FOO(_hash_data_free_fn_t)
                                       data_free_func, size_t nentries)
{
  return ((SPECHASH_HASH *)
          genhash_new_nentries_full((genhash_val_fn_t) key_val_func,
                                    (genhash_comp_fn_t) key_comp_func,
                                    (genhash_copy_fn_t) key_copy_func,
                                    (genhash_free_fn_t) key_free_func,
                                    (genhash_copy_fn_t) data_copy_func,
                                    (genhash_free_fn_t) data_free_func,
                                    nentries));
}

/****************************************************************************
  Free a spechash.
****************************************************************************/
static inline void SPECHASH_FOO(_hash_destroy) (SPECHASH_HASH *tthis)
{
  genhash_destroy((struct genhash *) tthis);
}

/****************************************************************************
  Enable/Disable shrinking.
****************************************************************************/
static inline bool SPECHASH_FOO(_hash_set_no_shrink) (SPECHASH_HASH *tthis,
                                                      bool no_shrink)
{
  return genhash_set_no_shrink((struct genhash *) tthis, no_shrink);
}

/****************************************************************************
  Return the number of elements.
****************************************************************************/
static inline size_t SPECHASH_FOO(_hash_size) (const SPECHASH_HASH *tthis)
{
  return genhash_size((const struct genhash *) tthis);
}

/****************************************************************************
  Return the real number of buckets.
****************************************************************************/
static inline size_t
SPECHASH_FOO(_hash_capacity) (const SPECHASH_HASH *tthis)
{
  return genhash_capacity((const struct genhash *) tthis);
}

/****************************************************************************
  Duplicate the spechash.
****************************************************************************/
static inline SPECHASH_HASH *
SPECHASH_FOO(_hash_copy) (const SPECHASH_HASH *tthis)
fc__warn_unused_result;

static inline SPECHASH_HASH *
SPECHASH_FOO(_hash_copy) (const SPECHASH_HASH *tthis)
{
  return (SPECHASH_HASH *) genhash_copy((const struct genhash *) tthis);
}

/****************************************************************************
  Remove all elements of the spechash.
****************************************************************************/
static inline void SPECHASH_FOO(_hash_clear) (SPECHASH_HASH *tthis)
{
  genhash_clear((struct genhash *) tthis);
}

/****************************************************************************
  Insert an element into the spechash. Returns TRUE on success (if no
  collision).
****************************************************************************/
static inline bool SPECHASH_FOO(_hash_insert) (SPECHASH_HASH *tthis,
                                               const SPECHASH_KEY_TYPE key,
                                               const SPECHASH_DATA_TYPE data)
{
  return genhash_insert((struct genhash *) tthis, SPECHASH_KEY_TO_PTR(key),
                        SPECHASH_DATA_TO_PTR(data));
}

/****************************************************************************
  Replace an element into the spechash. Returns TRUE if it replaced an old
  element.
****************************************************************************/
static inline bool
SPECHASH_FOO(_hash_replace) (SPECHASH_HASH *tthis,
                             const SPECHASH_KEY_TYPE key,
                             const SPECHASH_DATA_TYPE data)
{
  return genhash_replace((struct genhash *) tthis, SPECHASH_KEY_TO_PTR(key),
                         SPECHASH_DATA_TO_PTR(data));
}

/****************************************************************************
  Replace an element into the spechash. Returns TRUE if it replaced an old
  element.
****************************************************************************/
static inline bool
SPECHASH_FOO(_hash_replace_full) (SPECHASH_HASH *tthis,
                                  const SPECHASH_KEY_TYPE key,
                                  const SPECHASH_DATA_TYPE data,
                                  SPECHASH_KEY_TYPE *old_pkey,
                                  SPECHASH_DATA_TYPE *old_pdata)
{
  void *key_ptr, *data_ptr;
  bool ret = genhash_replace_full((struct genhash *) tthis,
                                  SPECHASH_KEY_TO_PTR(key),
                                  SPECHASH_DATA_TO_PTR(data),
                                  &key_ptr, &data_ptr);

  if (NULL != old_pkey) {
    *old_pkey = SPECHASH_PTR_TO_KEY(key_ptr);
  }
  if (NULL != old_pdata) {
    *old_pdata = SPECHASH_PTR_TO_DATA(data_ptr);
  }
  return ret;
}

/****************************************************************************
  Lookup an element. Returns TRUE if found.
****************************************************************************/
static inline bool SPECHASH_FOO(_hash_lookup) (const SPECHASH_HASH *tthis,
                                               const SPECHASH_KEY_TYPE key,
                                               SPECHASH_DATA_TYPE *pdata)
{
  void *data_ptr;
  bool ret = genhash_lookup((const struct genhash *) tthis,
                            SPECHASH_KEY_TO_PTR(key), &data_ptr);

  if (NULL != pdata) {
    *pdata = SPECHASH_PTR_TO_DATA(data_ptr);
  }
  return ret;
}

/****************************************************************************
  Remove an element. Returns TRUE on success.
****************************************************************************/
static inline bool SPECHASH_FOO(_hash_remove) (SPECHASH_HASH *tthis,
                                               const SPECHASH_KEY_TYPE key)
{
  return genhash_remove((struct genhash *) tthis, SPECHASH_KEY_TO_PTR(key));
}

/****************************************************************************
  Remove an element. Returns TRUE on success.
****************************************************************************/
static inline bool
SPECHASH_FOO(_hash_remove_full) (SPECHASH_HASH *tthis,
                                 const SPECHASH_KEY_TYPE key,
                                 SPECHASH_KEY_TYPE *deleted_pkey,
                                 SPECHASH_DATA_TYPE *deleted_pdata)
{
  void *key_ptr, *data_ptr;
  bool ret = genhash_remove_full((struct genhash *) tthis,
                                 SPECHASH_KEY_TO_PTR(key),
                                 &key_ptr, &data_ptr);

  if (NULL != deleted_pkey) {
    *deleted_pkey = SPECHASH_PTR_TO_KEY(key_ptr);
  }
  if (NULL != deleted_pdata) {
    *deleted_pdata = SPECHASH_PTR_TO_DATA(data_ptr);
  }
  return ret;
}


/****************************************************************************
  Compare the specific hash tables.
****************************************************************************/
static inline bool
SPECHASH_FOO(_hashs_are_equal) (const SPECHASH_HASH *phash1,
                                const SPECHASH_HASH *phash2)
{
  return genhashs_are_equal_full((const struct genhash *) phash1,
                                 (const struct genhash *) phash2,
                                 (genhash_comp_fn_t) SPECHASH_DATA_COMP);
}

/****************************************************************************
  Compare the specific hash tables.
****************************************************************************/
static inline bool
SPECHASH_FOO(_hashs_are_equal_full) (const SPECHASH_HASH *phash1,
                                     const SPECHASH_HASH *phash2,
                                     SPECHASH_FOO(_hash_data_comp_fn_t)
                                     data_comp_func)
{
  return genhashs_are_equal_full((const struct genhash *) phash1,
                                 (const struct genhash *) phash2,
                                 (genhash_comp_fn_t) data_comp_func);
}


/****************************************************************************
  Remove the size of the iterator type.
****************************************************************************/
static inline size_t SPECHASH_FOO(_hash_iter_sizeof) (void)
{
  return genhash_iter_sizeof();
}

/****************************************************************************
  Initialize an iterator.
****************************************************************************/
static inline struct iterator *
SPECHASH_FOO(_hash_iter_init) (SPECHASH_ITER *iter,
                               const SPECHASH_HASH *tthis)
{
  return genhash_iter_init((struct genhash_iter *) iter,
                           (const struct genhash *) tthis);
}

/****************************************************************************
  Initialize a key iterator.
****************************************************************************/
static inline struct iterator *
SPECHASH_FOO(_hash_key_iter_init) (SPECHASH_ITER *iter,
                                   const SPECHASH_HASH *tthis)
{
  return genhash_key_iter_init((struct genhash_iter *) iter,
                               (const struct genhash *) tthis);
}

/****************************************************************************
  Initialize a value iterator.
****************************************************************************/
static inline struct iterator *
SPECHASH_FOO(_hash_value_iter_init) (SPECHASH_ITER *iter,
                                     const SPECHASH_HASH *tthis)
{
  return genhash_value_iter_init((struct genhash_iter *) iter,
                                 (const struct genhash *) tthis);
}

#undef SPECHASH_TAG
#undef SPECHASH_KEY_TYPE
#undef SPECHASH_DATA_TYPE
#undef SPECHASH_KEY_VAL
#undef SPECHASH_KEY_COMP
#undef SPECHASH_KEY_COPY
#undef SPECHASH_KEY_FREE
#undef SPECHASH_DATA_COMP
#undef SPECHASH_DATA_COPY
#undef SPECHASH_DATA_FREE
#undef SPECHASH_KEY_TO_PTR
#undef SPECHASH_PTR_TO_KEY
#undef SPECHASH_DATA_TO_PTR
#undef SPECHASH_PTR_TO_DATA
#undef SPECHASH_PASTE_
#undef SPECHASH_PASTE
#undef SPECHASH_HASH
#undef SPECHASH_ITER
#undef SPECHASH_FOO


/* Base macros that the users can specialize. */
#ifndef FC__SPECHASH_H  /* Defines this only once, no multiple inclusions. */
#define FC__SPECHASH_H

/* Spechash value iterator.
 *
 * TYPE_data - The real type of the data in the genhash.
 * ARG_ht - The genhash to iterate.
 * NAME_data - The name of the data iterator (defined inside the macro). */
#define TYPED_HASH_DATA_ITERATE(TYPE_data, ARG_ht, NAME_data)               \
  generic_iterate(struct genhash_iter, TYPE_data, NAME_data,                \
                  genhash_iter_sizeof, genhash_value_iter_init,             \
                  (const struct genhash *) (ARG_ht))

/* Balance for above: */ 
#define HASH_DATA_ITERATE_END generic_iterate_end

/* Spechash key iterator.
 *
 * TYPE_key - The real type of the key in the genhash.
 * ARG_ht - The genhash to iterate.
 * NAME_key - The name of the key iterator (defined inside the macro). */
#define TYPED_HASH_KEYS_ITERATE(TYPE_key, ARG_ht, NAME_key)                 \
  generic_iterate(struct genhash_iter, TYPE_key, NAME_key,                  \
                  genhash_iter_sizeof, genhash_key_iter_init,               \
                  (const struct genhash *) (ARG_ht))

/* Balance for above: */ 
#define HASH_KEYS_ITERATE_END                                               \
  }                                                                         \
} while (FALSE);

/* Spechash key and values iterator.
 *
 * TYPE_key - The real type of the key in the genhash.
 * TYPE_data - The real type of the key in the genhash.
 * ARG_ht - The genhash to iterate.
 * NAME_key - The name of the key iterator (defined inside the macro).
 * NAME_data - The name of the data iterator (defined inside the macro). */
#define TYPED_HASH_ITERATE(TYPE_key, TYPE_data, ARG_ht, NAME_key, NAME_data)\
  genhash_iterate((const struct genhash *) (ARG_ht), MY_iter) {             \
    TYPE_key NAME_key = (TYPE_key) genhash_iter_key(MY_iter);               \
    TYPE_data NAME_data = (TYPE_data) genhash_iter_value(MY_iter);

/* Balance for above: */ 
#define HASH_ITERATE_END                                                    \
  } genhash_iterate_end;

#endif /* FC__SPECHASH_H */
