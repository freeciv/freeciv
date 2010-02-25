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
sbuffer: ("string buffer")

  sbuffer is to be used when one wants to make lots of small
  memory allocations (usually strings, hence the name), which
  will be freed at the same time.  A large "buffer" is allocated,
  and sub-sections are used for individual small mallocs, with
  the aim of being faster and using less memory compared to
  just using malloc.

  Originally written by Trent Piepho <xyzzy@u.washington.edu>
  as strbuffermalloc() et al, as part of registry module.
  Re-organised and modified to use "struct sbuffer" and placed
  in separate sbuffer module by David Pfitzner <dwp@mso.anu.edu.au>.

  Uses fc_malloc() etc for allocations, so malloc failures
  are handled there; makes liberal use of fc_assert_exit().
***************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "log.h"                /* fc_assert. */
#include "mem.h"
#include "shared.h"

#include "sbuffer.h"

/* default buffer size: */
#define SBUF_DEFAULT_SIZE (64*1024)

struct sbuffer {
  void *buffer;
  int size;
  int offset;

  /* 'buffer' is a pointer to the current buffer, out of which new
     memory will be allocated.  The first (char*) worth points to
     the previous buffer (and so on in previous buffers), or is NULL
     for the original buffer.  (These are necessary for free to work.)
     'size' is the buffer size for the current buffer, and for any
     additional buffers.  (size is set when struct is initialized,
     and should not be changed thereafter).
     'offset' is the index to the next unused memory in buffer.
     (Note that offset is always > 0 due to the storage of the pointer
     to the previous buffer.)
  */
};

/* used to try to align returned memory pessimisticly: */
struct aligner {
  union {
    /* double d; */      /* (un)necessary? */
    long l;
    void *v;
  } u;
};
#define SBUF_ALIGN_SIZE (sizeof(struct aligner))

/**************************************************************************
  Internal: malloc an additional buffer; store pointer to previous buffer.
**************************************************************************/
static void sbuf_expand(struct sbuffer *sb)
{
  void *prev_buffer;

  fc_assert_ret(NULL != sb);
  fc_assert_ret(0 < sb->size);

  prev_buffer = sb->buffer;
  sb->buffer = fc_malloc(sb->size);

  /* store pointer to previous buffer: */
  *(char **)(sb->buffer) = (char*)prev_buffer;
  sb->offset = sizeof(char*);
}

/**************************************************************************
  Internal: make sure offset is (pessimisticly) aligned:
**************************************************************************/
static void sbuf_align(struct sbuffer *sb)
{
  int align_offset;

  fc_assert_ret(NULL != sb);
  fc_assert_ret(0 < sb->offset);

  align_offset = sb->offset % SBUF_ALIGN_SIZE;
  if (align_offset != 0) {
    sb->offset += SBUF_ALIGN_SIZE - align_offset;
  }
}

/**************************************************************************
  Get a new initialized sbuffer, specifying buffer size:
**************************************************************************/
static struct sbuffer *sbuf_new_size(size_t size)
{
  struct sbuffer *sb;

  fc_assert_ret_val(size > 2 * SBUF_ALIGN_SIZE, NULL);

  sb = (struct sbuffer *)fc_malloc(sizeof(*sb));
  sb->size = size;
  sb->buffer = NULL;		/* so pointer to prev buffer is NULL */

  /* allocate first buffer: */
  sbuf_expand(sb);

  return sb;
}

/**************************************************************************
  Get a new initialized sbuffer, using default buffer size: 
**************************************************************************/
struct sbuffer *sbuf_new(void)
{
  return sbuf_new_size(SBUF_DEFAULT_SIZE);
}

/**************************************************************************
  malloc: size here must be less than sb->size.
**************************************************************************/
void *sbuf_malloc(struct sbuffer *sb, size_t size)
{
  void *ret;

  fc_assert_exit(NULL != sb);
  fc_assert_exit(NULL != sb->buffer);
  fc_assert_exit(0 < sb->size);
  fc_assert_exit(0 < sb->offset);
  fc_assert_ret_val(0 < size, NULL);
  fc_assert_exit(sb->size - SBUF_ALIGN_SIZE >= size);

  sbuf_align(sb);

  /* check for space: */
  if (size > (sb->size - sb->offset)) {
    sbuf_expand(sb);
    sbuf_align(sb);
    fc_assert_exit(sb->size - sb->offset >= size);
  }

  ret = ADD_TO_POINTER(sb->buffer, sb->offset);
  sb->offset += size;
  return ret;
}

/**************************************************************************
  strdup: no alignment concerns here.
**************************************************************************/
char *sbuf_strdup(struct sbuffer *sb, const char *str)
{
  size_t size = strlen(str)+1;
  char *ret;

  fc_assert_exit(NULL != sb);
  fc_assert_exit(NULL != sb->buffer);
  fc_assert_exit(0 < sb->size);
  fc_assert_exit(0 < sb->offset);
  fc_assert_exit(sb->size - sizeof(char *) >= size);

  /* check for space: */
  if (size > (sb->size - sb->offset)) {
    sbuf_expand(sb);
    fc_assert_exit(sb->size - sb->offset >= size);
  }
  ret = ADD_TO_POINTER(sb->buffer, sb->offset);
  memcpy(ret, str, size);	/* includes null-terminator */
  sb->offset += size;
  return ret;
}

/**************************************************************************
  Free all memory associated with sb; after this sb itself points
  to deallocated memory, so should not be used.
**************************************************************************/
void sbuf_free(struct sbuffer *sb)
{
  fc_assert_ret(NULL != sb);
  fc_assert_ret(NULL != sb->buffer);

  do {
    void *next = *(char **)sb->buffer;
    free(sb->buffer);
    sb->buffer = next;
  } while(sb->buffer);

  free(sb);
}
