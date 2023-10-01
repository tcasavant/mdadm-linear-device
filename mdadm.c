#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"

// Turn command and appropriate parameters into operation as integer
uint32_t encode_op(int cmd, int disk_num, int block_num){
  uint32_t op = 0;
  op |= cmd << 26;
  op |= disk_num << 22;
  op |= block_num;

  return op;
}

int mdadm_mount(void) {
  uint32_t op = encode_op(JBOD_MOUNT, 0, 0);
  int rc = jbod_client_operation(op, NULL);

  if (rc == 0)
    return 1;
  else
    return -1;
}

int mdadm_unmount(void) {
  uint32_t op = encode_op(JBOD_UNMOUNT, 0, 0);
  int rc = jbod_client_operation(op, NULL);

  if (rc == 0)
    return 1;
  else
    return -1;
}

// Translate address into disk num, block num, and offset on block
void translate_address(uint32_t addr, int *disk_num, int *block_num, int *offset){
  *disk_num = addr / JBOD_DISK_SIZE;
  int offset_on_disk = addr % JBOD_DISK_SIZE;
  *block_num = offset_on_disk / JBOD_BLOCK_SIZE;
  *offset = offset_on_disk % JBOD_BLOCK_SIZE;
}

int mdadm_seek(int disk_num, int block_num){
  uint32_t op = encode_op(JBOD_SEEK_TO_DISK, disk_num, 0);
  if(jbod_client_operation(op, NULL) == -1)
    return -1;

  op = encode_op(JBOD_SEEK_TO_BLOCK, 0, block_num);
  if(jbod_client_operation(op, NULL) == -1)
    return -1;

  return 1;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  if (len > 1024)
    return -1;
  if (addr + len > JBOD_DISK_SIZE * JBOD_NUM_DISKS)
    return -1;
  if (buf == NULL && len > 0)
    return -1;

  int disk_num;
  int block_num;
  int offset;
  translate_address(addr, &disk_num, &block_num, &offset);

  bool use_cache = cache_enabled();

  uint32_t bytesCopied = 0;
  uint8_t mybuf[JBOD_BLOCK_SIZE];

  uint32_t op;

  if(mdadm_seek(disk_num, block_num) == -1)
    return -1;

  int cache_hit = -1;

  // Get first block
  if(use_cache){
    cache_hit = cache_lookup(disk_num, block_num, mybuf);
  }
  if (cache_hit == -1){
    op = encode_op(JBOD_READ_BLOCK, 0, 0);
    jbod_client_operation(op, mybuf);

    cache_insert(disk_num, block_num, mybuf);
  }
  cache_hit = -1;

  if(len + offset <= JBOD_BLOCK_SIZE){
    memcpy(buf, mybuf + offset, len);
    bytesCopied += len;
  }
  else{
    memcpy(buf, mybuf + offset, JBOD_BLOCK_SIZE - offset);
    bytesCopied += JBOD_BLOCK_SIZE - offset;
  }
  ++block_num;


  // Get remaining blocks
  while(bytesCopied < len){
    if(block_num == JBOD_NUM_BLOCKS_PER_DISK){
      block_num = 0;
      ++disk_num;

      mdadm_seek(disk_num, block_num);

      op = encode_op(JBOD_READ_BLOCK, 0, 0);
    }

    if(use_cache){
      cache_hit = cache_lookup(disk_num, block_num, mybuf);
    }
    if (cache_hit == -1){
      mdadm_seek(disk_num, block_num);
      jbod_client_operation(op, mybuf);
      cache_insert(disk_num, block_num, mybuf);
    }
    ++block_num;
    cache_hit = -1;

    if(len - bytesCopied < JBOD_BLOCK_SIZE){
      memcpy(buf + bytesCopied, mybuf, len-bytesCopied);
      bytesCopied = len;
    }
    else{
      memcpy(buf + bytesCopied, mybuf, JBOD_BLOCK_SIZE);
      bytesCopied += JBOD_BLOCK_SIZE;
    }
  }

  return len;
}


// Update cache entry if present, insert it if not
void cache_write(uint32_t disk_num, uint32_t block_num, uint8_t *buf){
  cache_update(disk_num, block_num, buf);
  cache_insert(disk_num, block_num, buf);
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
  if(len > 1024)
    return -1;
  if (addr + len > JBOD_DISK_SIZE * JBOD_NUM_DISKS)
    return -1;
  if (buf == NULL && len > 0)
    return -1;

  int disk_num;
  int block_num;
  int offset;
  translate_address(addr, &disk_num, &block_num, &offset);

  if(mdadm_seek(disk_num, block_num) == -1)
    return -1;

  uint8_t mybuf[JBOD_BLOCK_SIZE];
  uint32_t bytesCopied = 0;

  uint32_t read_op = encode_op(JBOD_READ_BLOCK, 0, 0);

  int cache_hit = -1;
  bool use_cache = cache_enabled();
  if(use_cache){
    cache_hit = cache_lookup(disk_num, block_num, mybuf);
  }
  if (cache_hit == -1){
    /* mdadm_seek(disk_num, block_num); */
    jbod_client_operation(read_op, mybuf);
    mdadm_seek(disk_num, block_num);
    cache_insert(disk_num, block_num, mybuf);
  }
  cache_hit = -1;
  /* jbod_client_operation(read_op, mybuf); */
  /* mdadm_seek(disk_num, block_num); */


  // First write - start after offset and go to len of input of end of block if necessary
  if(offset + len < JBOD_BLOCK_SIZE){
    memcpy(mybuf+offset, buf, len);
    bytesCopied = len;
  }
  else{
    memcpy(mybuf+offset, buf, JBOD_BLOCK_SIZE - offset);
    bytesCopied += JBOD_BLOCK_SIZE - offset;
  }

  if(use_cache){
    cache_write(disk_num, block_num, mybuf);
  }

  uint32_t write_op = encode_op(JBOD_WRITE_BLOCK, 0, 0);
  jbod_client_operation(write_op, mybuf);
  ++ block_num;

  // While we can write entire blocks, do so
  while(len - bytesCopied >= JBOD_BLOCK_SIZE){
    if(block_num == JBOD_NUM_BLOCKS_PER_DISK){
      block_num = 0;
      ++disk_num;

      mdadm_seek(disk_num, block_num);
    }

    memcpy(mybuf, buf + bytesCopied, JBOD_BLOCK_SIZE);
    if(use_cache){
      cache_write(disk_num, block_num, mybuf);
    }
    jbod_client_operation(write_op, mybuf);
    bytesCopied += JBOD_BLOCK_SIZE;
    ++ block_num;
  }

  // Final write - only write bytes from start of block until total write length is satisfied
  if(block_num == JBOD_NUM_BLOCKS_PER_DISK){
    block_num = 0;
    ++disk_num;

    mdadm_seek(disk_num, block_num);
  }

  if(use_cache){
    cache_hit = cache_lookup(disk_num, block_num, mybuf);
  }
  if (cache_hit == -1){
    /* mdadm_seek(disk_num, block_num); */
    jbod_client_operation(read_op, mybuf);
    mdadm_seek(disk_num, block_num);
    cache_insert(disk_num, block_num, mybuf);
  }
  cache_hit = -1;
  /* jbod_client_operation(read_op, mybuf); */
  /* mdadm_seek(disk_num, block_num); */

  memcpy(mybuf, buf + bytesCopied, len - bytesCopied);
  if(use_cache){
      cache_write(disk_num, block_num, mybuf);
  }

  jbod_client_operation(write_op, mybuf);



  return len;
}
