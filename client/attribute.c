
#include <string.h>
#include <assert.h>

#include "hash.h"
#include "mem.h"
#include "game.h"
#include "packets.h"
#include "clinet.h"
#include "log.h"

#include "attribute.h"

#define ATTRIBUTE_LOG_LEVEL	LOG_DEBUG

static struct hash_table *attribute_hash = NULL;

struct attr_key {
  int key, id, x, y;
};

/****************************************************************************
 Hash function for attribute_hash.
*****************************************************************************/
static unsigned int attr_hash_val_fn(const void *key,
				     unsigned int num_buckets)
{
  const struct attr_key *pkey = (const struct attr_key *) key;

  return (pkey->id ^ pkey->x ^ pkey->y ^ pkey->key) % num_buckets;
}

/****************************************************************************
 Compare-function for the keys in the hash table.
*****************************************************************************/
static int attr_hash_cmp_fn(const void *key1, const void *key2)
{
  return memcmp(key1, key2, sizeof(struct attr_key));
}

/****************************************************************************
...
*****************************************************************************/
void attribute_init()
{
  assert(attribute_hash == NULL);
  attribute_hash = hash_new(attr_hash_val_fn, attr_hash_cmp_fn);
}

/****************************************************************************
 This method isn't endian safe and there will also be problems if
 sizeof(int) at serialization time is different from sizeof(int) at
 deserialization time.
*****************************************************************************/
static void serialize_hash(struct hash_table *hash, void **pdata,
			   int *pdata_length)
{
  /*
   * Layout:
   *
   * struct {
   *   int entries;
   *   int total_size_in_bytes;
   * } preamble;
   * 
   * struct {
   *   int key_size, key_offset, value_size, value_offset;
   * } header[entries];
   * (offsets are relative to the body start)
   *
   * struct {
   *   char key[], char value[];
   * } body[entries];
   */
  int preamble_length, header_length, body_length, total_length, i,
      current_body_offset, entries = hash_num_entries(hash), key_size =
      sizeof(struct attr_key);
  char *result, *body;
  int *value_lengths, *header, *preamble;

  value_lengths = fc_malloc(sizeof(int) * entries);

  /*
   * Step 1: loop through all keys and fill value_lengths
   */
  for (i = 0; i < entries; i++) {
    const void *pvalue = hash_value_by_number(hash, i);
    value_lengths[i] = ((int *) pvalue)[0] + sizeof(int);
  }

  /*
   * Step 2: calculate the *_length variables
   */
  preamble_length = 2 * sizeof(int);
  header_length = entries * sizeof(int) * 4;
  body_length = entries * key_size;

  for (i = 0; i < entries; i++) {
    body_length += value_lengths[i];
  }

  /*
   * Step 3: allocate memory
   */
  total_length = preamble_length + header_length + body_length;
  result = fc_malloc(total_length);

  /*
   * Step 4: fill out the preamble
   */
  preamble = (int *)result;
  preamble[0] = entries;
  preamble[1] = total_length;

  /*
   * Step 5: fill out the header
   */
  header = (int *)(result + preamble_length);
  current_body_offset = 0;

  for (i = 0; i < entries; i++) {
    header[0] = key_size;
    header[1] = current_body_offset;
    current_body_offset += key_size;
    header[2] = value_lengths[i];
    header[3] = current_body_offset;
    current_body_offset += value_lengths[i];
    freelog(LOG_DEBUG, "serial: [%d] key{size=%d, offset=%d} "
	    "value{size=%d, offset=%d}", i, header[0], header[1],
	    header[2], header[3]);
    header += 4;
  }

  /*
   * Step 6: fill out the body.
   */
  body = result + preamble_length + header_length;
  for (i = 0; i < entries; i++) {
    const void *pkey = hash_key_by_number(hash, i);
    const void *pvalue = hash_value_by_number(hash, i);

    memcpy(body, pkey, key_size);
    body += key_size;
    memcpy(body, pvalue, value_lengths[i]);
    body += value_lengths[i];
  }

  /*
   * Step 7: cleanup
   */
  *pdata = result;
  *pdata_length = total_length;
  free(value_lengths);
  freelog(LOG_DEBUG, "serialized %d entries in %d bytes", entries,
	  total_length);
}

/****************************************************************************
...
*****************************************************************************/
static void unserialize_hash(struct hash_table *hash, char *data,
			     int data_length)
{
  int *preamble, *header;
  int entries, i, preamble_length, header_length;
  char *body;

  hash_delete_all_entries(hash);

  preamble = (int *) data;
  entries = preamble[0];
  assert(preamble[1] == data_length);

  freelog(LOG_DEBUG, "try to unserialized %d entries from %d bytes",
	  entries, data_length);
  preamble_length = 2 * sizeof(int);
  header_length = entries * sizeof(int) * 4;

  header = (int *)(data + preamble_length);
  body = data + preamble_length + header_length;

  for (i = 0; i < entries; i++) {
    void *pkey = fc_malloc(header[0]);
    void *pvalue = fc_malloc(header[2]);

    freelog(LOG_DEBUG, "unserial: [%d] key{size=%d, offset=%d} "
	    "value{size=%d, offset=%d}", i, header[0], header[1],
	    header[2], header[3]);

    memcpy(pkey, body + header[1], header[0]);
    memcpy(pvalue, body + header[3], header[2]);

    hash_insert(hash, pkey, pvalue);

    header += 4;
  }
}

/****************************************************************************
 Send current state to the server. Note that the current
 implementation will send all attributes to the server.
*****************************************************************************/
void attribute_flush(void)
{
  struct player *pplayer = game.player_ptr;

  assert(attribute_hash);

  if (hash_num_entries(attribute_hash) == 0)
    return;

  if (pplayer->attribute_block.data != NULL)
    free(pplayer->attribute_block.data);

  serialize_hash(attribute_hash, &(pplayer->attribute_block.data),
		 &(pplayer->attribute_block.length));
  send_attribute_block(pplayer, &aconnection);
}

/****************************************************************************
 Recreate the attribute set from the player's
 attribute_block. Shouldn't be used by normal code.
*****************************************************************************/
void attribute_restore(void)
{
  struct player *pplayer = game.player_ptr;
  assert(attribute_hash);
  unserialize_hash(attribute_hash, pplayer->attribute_block.data,
		   pplayer->attribute_block.length);
}

/****************************************************************************
 Low-level function to set an attribute.  If data_length is zero the
 attribute is removed.
*****************************************************************************/
void attribute_set(int key, int id, int x, int y, int data_length,
		   const void *const data)
{
  struct attr_key *pkey;
  void *pvalue = NULL;

  freelog(ATTRIBUTE_LOG_LEVEL, "attribute_set(key=%d, id=%d, x=%d, y=%d, "
	  "data_length=%d, data=%p)", key, id, x, y, data_length, data);

  assert(attribute_hash);
  assert(data_length >= 0);

  pkey = fc_malloc(sizeof(struct attr_key));
  pkey->key = key;
  pkey->id = id;
  pkey->x = x;
  pkey->y = y;

  if (data_length != 0) {
    pvalue = fc_malloc(data_length + sizeof(int));
    ((int *) pvalue)[0] = data_length;
    memcpy((char *)pvalue + sizeof(int), data, data_length);
  }

  if (hash_key_exists(attribute_hash, pkey)) {
    void *old_value = hash_delete_entry(attribute_hash, pkey);
    free(old_value);
  }

  if (data_length != 0) {
    hash_insert(attribute_hash, pkey, pvalue);
  }
}

/****************************************************************************
 Low-level function to get an attribute. If data hasn't enough space
 to hold the attribute attribute_get aborts. Returns the actual size
 of the attribute. Can be zero if the attribute is unset.
*****************************************************************************/
int attribute_get(int key, int id, int x, int y, int max_data_length,
		  void *data)
{

  struct attr_key pkey;
  void *pvalue;
  int length;

  freelog(ATTRIBUTE_LOG_LEVEL, "attribute_get(key=%d, id=%d, x=%d, y=%d, "
	  "max_data_length=%d, data=%p)", key, id, x, y, max_data_length,
	  data);

  assert(attribute_hash);

  pkey.key = key;
  pkey.id = id;
  pkey.x = x;
  pkey.y = y;

  pvalue = hash_lookup_data(attribute_hash, &key);

  if (pvalue == NULL) {
    freelog(ATTRIBUTE_LOG_LEVEL, "  not found");
    return 0;
  }

  length = ((int *) pvalue)[0];

  if(max_data_length < length){
    freelog(LOG_FATAL, "attribute: max_data_length=%d, length found=%d (!)\n"
          "It is quite possible that the server (this client was attached to) "
          "loaded an old savegame that was created prior to "
          "certain interface changes in your client. If you have access to "
          "the savegame, editing the file and removing entries beginning with "
          "\"attribute_block_\" may alleviate the problem (though you will " 
          "lose some non-critical client data). If you still encounter this, "
          "submit a bug report to <freeciv-dev@freeciv.org>", 
          max_data_length, length);

    exit(1);
  }

  memcpy(data, (char *)pvalue + sizeof(int), length);

  freelog(ATTRIBUTE_LOG_LEVEL, "  found length=%d", length);
  return length;
}

/****************************************************************************
...
*****************************************************************************/
void attr_unit_set(enum attr_unit what, int unit_id, int data_length,
		   const void *const data)
{
  attribute_set(what, unit_id, -1, -2, data_length, data);
}

/****************************************************************************
...
*****************************************************************************/
int attr_unit_get(enum attr_unit what, int unit_id, int max_data_length,
		  void *data)
{
  return attribute_get(what, unit_id, -1, -2, max_data_length, data);
}

/****************************************************************************
...
*****************************************************************************/
void attr_unit_set_int(enum attr_unit what, int unit_id, int data)
{
  attr_unit_set(what, unit_id, sizeof(int), &data);
}

/****************************************************************************
...
*****************************************************************************/
int attr_unit_get_int(enum attr_unit what, int unit_id, int *data)
{
  return attr_unit_get(what, unit_id, sizeof(int), data);
}

/****************************************************************************
...
*****************************************************************************/
void attr_city_set(enum attr_city what, int city_id, int data_length,
		   const void *const data)
{
  attribute_set(what, city_id, -1, -1, data_length, data);
}

/****************************************************************************
...
*****************************************************************************/
int attr_city_get(enum attr_city what, int city_id, int max_data_length,
		  void *data)
{
  return attribute_get(what, city_id, -1, -1, max_data_length, data);
}

/****************************************************************************
...
*****************************************************************************/
void attr_city_set_int(enum attr_city what, int city_id, int data)
{
  attr_city_set(what, city_id, sizeof(int), &data);
}

/****************************************************************************
...
*****************************************************************************/
int attr_city_get_int(enum attr_city what, int city_id, int *data)
{
  return attr_city_get(what, city_id, sizeof(int), data);
}

/****************************************************************************
...
*****************************************************************************/
void attr_player_set(enum attr_player what, int player_id, int data_length,
		     const void *const data)
{
  attribute_set(what, player_id, -1, -1, data_length, data);
}

/****************************************************************************
...
*****************************************************************************/
int attr_player_get(enum attr_player what, int player_id,
		    int max_data_length, void *data)
{
  return attribute_get(what, player_id, -1, -1, max_data_length, data);
}

/****************************************************************************
...
*****************************************************************************/
void attr_tile_set(enum attr_tile what, int x, int y, int data_length,
		   const void *const data)
{
  attribute_set(what, -1, x, y, data_length, data);
}

/****************************************************************************
...
*****************************************************************************/
int attr_tile_get(enum attr_tile what, int x, int y, int max_data_length,
		  void *data)
{
  return attribute_get(what, -1, x, y, max_data_length, data);
}
