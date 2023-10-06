#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mdadm.h"
#include "jbod.h"
#include "cache.h"
#include "net.h"
// uses jbod_client_operaton instead of jbod_operation to send requests over a network
int mounted = -1;

/*
Gives encoded op
Packed bits of cmd, disk_num, block_num
*/
uint32_t encode_op(int cmd, int disk_num, int block_num)
{
  uint32_t cmdShifted, disk_numShifted, block_numShifted = 0;
  // uint32_t blockNum32 = block_num;
  block_numShifted = (block_num);
  disk_numShifted = (disk_num) << 22;
  cmdShifted = (cmd) << 26;

  return (cmdShifted | disk_numShifted | block_numShifted);
}

int mdadm_mount(void)
{

  uint32_t op = encode_op(JBOD_MOUNT, 0, 0);
  int rc = jbod_client_operation(op, NULL);
  if (rc == 0) // checks if mounted succeeds
  {
    //("Mount passed\n");
    mounted = 1;
    return 1;
  }
  else
  {
    //("Mount failed \n");
    return -1;
  }
}

int mdadm_unmount(void)
{

  uint32_t op = encode_op(JBOD_UNMOUNT, 0, 0);
  int rc = jbod_client_operation(op, NULL);
  if (rc == 0) // checks if unmount succeeds
  {
    mounted = -1;
    return 1;
  }
  else
  {
    return -1;
  }
}

// minimum of x & y
int min(int x, int y)
{
  if (x <= y)
  {
    return x;
  }
  return y;
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf)
{
  // //printf("read checkpoint 1: %d", 1);
  //  invalid paramter testing
  if (mounted == -1 || (addr + len > 1048575) || addr < 0 || len < 0 || len > 1024 || sizeof(buf) > 1024 || (buf == NULL && len != 0))
  {
    return -1;
  }

  int diskNum = addr / 65536;
  int blockNum = (addr % 65536) / 256;
  int offset = addr % 256;
  int currDiskNum = diskNum;
  int currBlockNum = blockNum;

  // initializes all operations
  uint32_t opBlockCurr;
  uint32_t opDiskCurr;
  uint32_t opDisk = encode_op(JBOD_SEEK_TO_DISK, diskNum, 0);
  uint32_t opBlock = encode_op(JBOD_SEEK_TO_BLOCK, 0, blockNum);
  uint32_t opREAD = encode_op(JBOD_READ_BLOCK, 0, 0);

  // sets pointer to correct block and disk
  jbod_client_operation(opDisk, NULL);
  jbod_client_operation(opBlock, NULL);

  int numLeft = len;   // num I have left to read
  int numRead = 0;     // num I have read so far
  int currAddr = addr; // will keep track of current address so I can update the current disk

  uint8_t tempbuf[256];

  // handles offset case initially so I don't have to worry about it in the loop
  if (offset != 0)
  {
    // printf("read checkpoint: %d", 1);
    numRead = min(256 - offset, numLeft); // makes sure that if there is less data to read than from offset to the end of block, I am only reading the amount left

    if (cache_enabled())
    {
      int lookup = cache_lookup(currDiskNum, currBlockNum, tempbuf);
      if (lookup == -1)
      {
        jbod_client_operation(opREAD, tempbuf);
        // printf("read checkpoint: %d", 2);

        memcpy(buf, &tempbuf[offset], numRead);
        cache_insert(currDiskNum, currBlockNum, &buf[numRead]);
      }
      else
      {
        memcpy(buf, &tempbuf[offset], numRead);
      }
    }
    else
    {
      jbod_client_operation(opREAD, tempbuf);
      // printf("read checkpoint: %d", 3);
      memcpy(buf, &tempbuf[offset], numRead);
    }

    // updates
    numLeft -= numRead;
    currAddr += numRead;
    currBlockNum = (currAddr % 65536) / 256;
    currDiskNum = currAddr / 65536;
  }
  // iterate while there is still data left to read
  while (numLeft > 0)
  {
    if (currDiskNum != diskNum) // seek to correct disk if current address corresponds to a different disk
    {

      opDiskCurr = encode_op(JBOD_SEEK_TO_DISK, currDiskNum, 0);
      jbod_client_operation(opDiskCurr, NULL);
      opBlockCurr = encode_op(JBOD_SEEK_TO_BLOCK, 0, currBlockNum);
      jbod_client_operation(opBlockCurr, NULL);
    }
    if (numLeft >= 256) // when >= 256 I am reading the entire block anyways so I can directly read into buf
    {
      if (cache_enabled())
      {
        int lookup = cache_lookup(currDiskNum, currBlockNum, &buf[numRead]);
        if (lookup == -1)
        {
          jbod_client_operation(opREAD, &buf[numRead]);
          // printf("read checkpoint: %d", 4);
          cache_insert(currDiskNum, currBlockNum, &buf[numRead]);
        }
      }
      else
      {
        jbod_client_operation(opREAD, &buf[numRead]);
        // printf("read checkpoint: %d", 5);
      }

      // memcpy(&buf[numRead], tempbuf, 256);

      numRead += 256;
      numLeft -= 256;
      currAddr += 256;
    }
    else // when there are less than 256 bytes left jbod_read will give extra data at the end
    {

      if (cache_enabled())
      {
        int lookup = cache_lookup(currDiskNum, currBlockNum, tempbuf);
        if (lookup == -1)
        {
          jbod_client_operation(opREAD, tempbuf);
          // printf("read checkpoint: %d", 6);
          memcpy(&buf[numRead], tempbuf, numLeft);
          cache_insert(currDiskNum, currBlockNum, tempbuf);
        }
        else
        {
          memcpy(&buf[numRead], tempbuf, numLeft);
        }
      }
      else
      {
        jbod_client_operation(opREAD, tempbuf);
        // printf("read checkpoint: %d", 7);
        memcpy(&buf[numRead], tempbuf, numLeft);
      }
      // I copy the amount of data i need from tempbuf into buf

      // updates
      numRead += numLeft;
      currAddr += numLeft;
      numLeft = 0;
    }
    currDiskNum = currAddr / 65536;
    currBlockNum = (currAddr % 65536) / 256;
  }

  return len;
}

int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf)
{
  // invalid param testing
  if (mounted == -1 || (addr + len > 1048576) || addr < 0 || len < 0 || len > 1024 || sizeof(buf) > 1024 || (buf == NULL && len != 0))
  {
    return -1;
  }

  int diskNum = addr / 65536;
  int blockNum = (addr % 65536) / 256;
  int offset = addr % 256;

  // initialize all operations
  uint32_t opDiskCurr;
  uint32_t opRead;
  uint32_t opBlockCurr;
  uint32_t opDisk = encode_op(JBOD_SEEK_TO_DISK, diskNum, 0);
  uint32_t opBlock = encode_op(JBOD_SEEK_TO_BLOCK, 0, blockNum);
  uint32_t opWrite = encode_op(JBOD_WRITE_BLOCK, diskNum, blockNum);

  uint8_t tempbuf[256];

  // sets internal pointer to correct disk and block
  jbod_client_operation(opDisk, NULL);
  jbod_client_operation(opBlock, NULL);
  // printf("write checkpoint: %d", 1);

  int numLeft = len; // amount of data left to read
  int numWrote = 0;  // amount of data I have written so far

  int currAddr = addr; // I will keep track of current address (in JBOD) as I write
  int currDiskNum = diskNum;
  int currBlockNum = blockNum;

  if (offset != 0) // handle offset initially so I don't have to worry about it in while loop
  {

    // mdadm_read(addr - offset, 256, tempbuf);
    opRead = encode_op(JBOD_READ_BLOCK, diskNum, blockNum);
    jbod_client_operation(opRead, tempbuf);
    // printf("write checkpoint: %d", 2);

    opDiskCurr = encode_op(JBOD_SEEK_TO_DISK, currDiskNum, 0);
    jbod_client_operation(opDiskCurr, NULL);
    // printf("write checkpoint: %d", 3);

    opBlockCurr = encode_op(JBOD_SEEK_TO_BLOCK, 0, currBlockNum);
    jbod_client_operation(opBlockCurr, NULL);
    // printf("write checkpoint: %d", 4);

    numWrote = min(256 - offset, numLeft); // if there are less num left than from offset to end of block, I only write those

    memcpy(&tempbuf[offset], buf, numWrote);
    jbod_client_operation(opWrite, tempbuf);
    // printf("write checkpoint: %d", 5);

    if (cache_enabled())
    {
      int insert = cache_insert(diskNum, blockNum, tempbuf);
      if (insert == -1)
      {
        cache_update(diskNum, blockNum, tempbuf);
      }
    }

    // updates
    numLeft -= (numWrote);
    currAddr += numWrote;
    currDiskNum = currAddr / 65536;
    currBlockNum = (currAddr % 65536) / 256;
  }

  while (numLeft > 0)
  {
    if (currDiskNum != diskNum)
    {
      // currBlockNum = (currAddr % 65536) / 256;
      opDiskCurr = encode_op(JBOD_SEEK_TO_DISK, currDiskNum, 0);
      jbod_client_operation(opDiskCurr, NULL);
      // printf("write checkpoint: %d", 6);
      opBlockCurr = encode_op(JBOD_SEEK_TO_BLOCK, 0, currBlockNum);
      jbod_client_operation(opBlockCurr, NULL);
      // printf("write checkpoint: %d", 7);
    }
    if (numLeft >= 256) // when amt of data left is >= 256 I don't need to call read because I am overriding the entire block
    {

      memcpy(tempbuf, &buf[numWrote], 256);
      jbod_client_operation(opWrite, tempbuf);

      if (cache_enabled())
      {
        int insert = cache_insert(currDiskNum, currBlockNum, tempbuf);
        if (insert == -1)
        {
          cache_update(currDiskNum, currBlockNum, tempbuf);
        }
      }

      numLeft -= 256;
      numWrote += 256;
      currAddr += 256;
    }
    else // last iteration of while loop (all data will be written after this)
    {    // The amount of data left is less than an entire block so I have to fill tempbuf with its initial values being the amt of data left
      // and the rest with the current contents of the block which I get from read

      // currBlockNum = (currAddr % 65536) / 256;

      opRead = encode_op(JBOD_READ_BLOCK, currDiskNum, currBlockNum);
      /*
      if (cache_enabled())
      {
        int lookup = cache_lookup(currDiskNum, currBlockNum, tempbuf);
        if (lookup == -1)
        {
          jbod_operation(opRead, tempbuf);
          opDiskCurr = encode_op(JBOD_SEEK_TO_DISK, currDiskNum, 0);
          jbod_operation(opDiskCurr, NULL);
          opBlockCurr = encode_op(JBOD_SEEK_TO_BLOCK, 0, currBlockNum);
          jbod_operation(opBlockCurr, NULL);
        }
      }
      else
      {
      */
      jbod_client_operation(opRead, tempbuf);
      // printf("write checkpoint: %d", 8);

      opDiskCurr = encode_op(JBOD_SEEK_TO_DISK, currDiskNum, 0);
      jbod_client_operation(opDiskCurr, NULL);

      opBlockCurr = encode_op(JBOD_SEEK_TO_BLOCK, 0, currBlockNum);
      jbod_client_operation(opBlockCurr, NULL);
      // printf("write checkpoint: %d", 9);
      // }

      memcpy(tempbuf, &buf[numWrote], numLeft); // I read entire block into tempbuf and then override tempbuf[0-numLeft] with contents of buf
                                                //  this way the data I am not overwriting is preserved
                                                // This operation can potentially shift the disk so I make sure to seek to the correct disk and block

      jbod_client_operation(opWrite, tempbuf); // after seeking to the correct disk and block I write contents of tempbuf into the JBOD system
      //("write checkpoint: %d", 10);
      if (cache_enabled())
      {
        int insert = cache_insert(currDiskNum, currBlockNum, tempbuf);
        if (insert == -1)
        {
          cache_update(currDiskNum, currBlockNum, tempbuf);
        }
      }

      // updates
      numWrote += numLeft;
      currAddr += numLeft;
      numLeft = 0;
    }
    currDiskNum = currAddr / 65536;
    currBlockNum = (currAddr % 65536) / 256;
  }

  return len;
}