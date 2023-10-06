#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;
// dynamically allocates cache (size is specified in tester)
int cache_create(int num_entries)
{
  if (cache == NULL && num_entries >= 2 && num_entries < 4097)
  {
    cache = calloc(num_entries, sizeof(cache_entry_t));
    cache_size = num_entries;
    clock = 0;
    // clock = 0;
    //  num_queries = 0;
    //  num_hits = 0;
    return 1;
  }
  return -1;
}
// free the cache and set pointer to null to avoid dangling pointer
// don't want to reset queries and hits on destroy
int cache_destroy(void)
{
  if (cache != NULL)
  {
    free(cache);
    cache = NULL;
    cache_size = 0;
    // clock = 0;
    //  num_queries = 0;
    //  num_hits = 0;
    return 1;
  }
  return -1;
}
// cache look up (no eviction)
int cache_lookup(int disk_num, int block_num, uint8_t *buf)
{

  if (cache == NULL || buf == NULL || cache_size == 0 || disk_num > 15 || disk_num < 0 || block_num > 255 || block_num < 0)
  {
    return -1;
  }
  ++num_queries;
  ++clock;
  // printf("\nqueries: %d", num_queries);
  for (int i = 0; i < cache_size; ++i)
  {
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid)
    {
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      if (buf == NULL)
      {
        return -1;
      }
      cache[i].access_time = clock;
      ++num_hits;
      // printf("\nhits: %d", num_hits);
      return 1;
    }
  }
  return -1;
}

// updates existing entry
void cache_update(int disk_num, int block_num, const uint8_t *buf)
{
  if (cache == NULL || buf == NULL || disk_num > 15 || disk_num < 0 || block_num > 255 || block_num < 0)
  {
    return;
  }

  ++clock;
  for (int i = 0; i < cache_size; ++i)
  {
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid)
    {
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      cache[i].access_time = clock;
      break;
    }
  }
}

// inserts entry based on LRU policy
int cache_insert(int disk_num, int block_num, const uint8_t *buf)
{
  // return -1;
  // printf("%p\n", (void *)cache);
  // return -1;
  if (cache == NULL || cache_size < 2 || buf == NULL || disk_num > 15 || disk_num < 0 || block_num > 255 || block_num < 0)
  {
    // printf("%d Leaving Insert, ", -1);
    return -1;
  }
  ++clock;
  int minIndex = -1;
  // find first valid minimum
  for (int i = 0; i < cache_size; ++i)
  {
    if (cache[i].valid)
    {
      minIndex = i;
      break;
    }
  }
  // attempts to add entry (shouldn't already exist in cache) at first empty cache line
  for (int i = 0; i < cache_size; ++i)
  {
    // this means the entry already exists, so we should not insert anything
    if (cache[i].disk_num == disk_num && cache[i].block_num == block_num && cache[i].valid)
    {
      return -1;
    }
    // updates minimum index (minimum index is the one with the smallest access time because this is the least recently used entry)
    if (cache[i].valid && minIndex != -1 && cache[i].access_time < cache[minIndex].access_time)
    {
      minIndex = i;
    }
    // this means the entry is empty and we can insert here
    if (!(cache[i].valid))
    {

      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      cache[i].disk_num = disk_num;
      cache[i].block_num = block_num;
      cache[i].access_time = clock;
      cache[i].valid = true;
      return 1;
    }
  }
  if (minIndex != -1)
  {
    memcpy(cache[minIndex].block, buf, JBOD_BLOCK_SIZE);
    cache[minIndex].disk_num = disk_num;
    cache[minIndex].block_num = block_num;
    cache[minIndex].access_time = clock;
    cache[minIndex].valid = true;
    return 1;
  }
  return -1;
}
// cache is enabled when it does not point to NULL
bool cache_enabled(void)
{
  return cache != NULL;
}

void cache_print_hit_rate(void)
{
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float)num_hits / num_queries);
}