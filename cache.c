#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

// Use malloc and calloc
int cache_create(int num_entries) {
  if (cache != NULL)
    return -1;

  if (num_entries < 2 || num_entries > 4096)
    return -1;

  cache = calloc(num_entries, sizeof(cache_entry_t));
  cache_size = num_entries;
  return 1;
}

int cache_destroy(void) {
  if (cache == NULL)
    return -1;

  free(cache);
  cache = NULL;
  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  if (cache == NULL)
    return -1;
  if (buf == NULL)
    return -1;

  ++clock;
  ++num_queries;

  for (int i = 0; i < cache_size; ++i)
    if(cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num){
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      ++num_hits;
      cache[i].access_time = clock;
      return 1;
    }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  ++clock;

  for (int i = 0; i < cache_size; ++i)
    if(cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num){
      cache[i].access_time = clock;
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
      return;
    }

}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  if (cache == NULL)
    return -1;
  if (buf == NULL)
    return -1;
  if (disk_num > JBOD_NUM_DISKS || disk_num < 0)
    return -1;
  if (block_num > JBOD_NUM_BLOCKS_PER_DISK || block_num < 0)
    return -1;

  // Check if block is already in cache
  for (int i = 0; i < cache_size; ++i)
    if(cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num){
      return -1;
    }

  ++clock;

  int min_index = 0;
  for(int i = 0; i < cache_size; i++){
    if (cache[i].valid == false){
      min_index = i;
      break;
    }
    if(cache[i].access_time < cache[min_index].access_time)
      min_index = i;
  }
  cache[min_index].access_time = clock;
  cache[min_index].disk_num = disk_num;
  cache[min_index].block_num = block_num;
  cache[min_index].valid = true;
  memcpy(cache[min_index].block, buf, JBOD_BLOCK_SIZE);

  return 1;
}

bool cache_enabled(void) {
  if (cache == NULL)
    return false;
  return true;
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
