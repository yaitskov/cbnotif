#ifndef __CB_NOTIF__
#define __CB_NOTIF__

//  head to be used as in user programs as in kernel module

//  operation codes ----------------------------------------------

// start monitoring the specified file - returns id of file for ok
#define CBN_MONITOR 1
// stop monitoring the specified file  - returns 0 for ok
#define CBN_FORGET 2          
// get list of changed blocks since previous call
// - returns the number of changed blocks
#define CBN_CHANGED_BLOCKS 3  

// structures of operation argument that are passed
// with optional argument of ioctl()

struct cbn_monitor {
  int cbn_size;  // cmd size
  int cbn_block_size;
  // 1 - for terminated zero char
  // then malloc(sizeof(cbn_monitor) + strlen(path) /* + sizeof(char)*/)
  char cbn_path[1];
};

// cbn_forget is int (id of the file)

struct cbn_changed_blocks {
  int cbn_size;  // cmd size
  int cbn_max;   // length of cbn_blocks
  int cbn_count; // elements in cbn_blocks
  int cbn_blocks[0]; // block numbers or ranges
};

#define CBN_IS_RANGE(blk_num)  ((blk_num) < 0)
#define CBN_RANGE_START(blk_num) (-(blk_num))
#define CBN_RANGE_LENGTH(blk_num) (*(&(blk_num) + 1))
#define CBN_AFTER_RANGE(index) ((index) += 2);
#define CBN_AFTER_BLOCK(index) (++(index));

#endif
