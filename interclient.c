#include <stdio.h>
#include <readline/readline.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include "cbnotif.h"

/**
 *  This is an interactive client of cbnotif kernel module.
 *  Converts commands to binary format and
 *  decode binary output.
 */
static int process_command(char * command);
static void help_cmd(void);
static void monitored_files_cmd(void);
static void monitor_file_cmd(const char * args);
static void forget_file_cmd(const char * args);
static void get_changed_blocks_cmd(const char * args);
static void modify_file_cmd(const char * args);
static int pack_send_monitor(const char * file_path, int block_size);
static int insert_file_id(const char * file_path, ssize_t path_size, int file_id);
static struct monitored_file * mf_lookup_by_id(int file_id);

#define MAX_MONITORED_FILES 10

struct monitored_file {
  int mf_handler;
  char mf_name[1];
};

struct monitored_file * monitored_files[MAX_MONITORED_FILES];
static int number_monitored_files = 0;
static int dfile;
int main(int argc, char ** argv) {
  for (int i = 0; i < MAX_MONITORED_FILES; ++i)
    monitored_files[i] = 0;

  dfile = open("/dev/cbnotif", O_RDWR | O_NONBLOCK);
  if (dfile < 0) {
    perror("cannot open file '/dev/cbnotif'\n");
    return -1;
  }
  printf("type 'help' to get list of available commands\n");
  for (;;) {
    char * user_command = readline("$ ");
    if (!user_command)
      continue;
    if (strlen(user_command))
      if (process_command(user_command))
        return 0;
    free(user_command);
  }
}

/**
 *  interpret user command.
 *  if it's a module command then convert it, and send,
 *  and read answer, and decode, and print it.
 *  return 1 for exit
 */
static int process_command(char * command) {
  char * command_name = command;
  while (' ' == *command) ++command;
  command_name = command;
  while (*command >= 'a' && *command <= 'z') ++command;
  if (*command) {
    *command++ = '\0';
    while (' ' == *command) ++command;    
  }
  if (!strcmp("exit", command_name))
    return 1;
  if (!strcmp("help", command_name)) {
    help_cmd();
  } else if (!strcmp("list", command_name)) {
    monitored_files_cmd();
  } else if (!strcmp("monitor", command_name)) {
    monitor_file_cmd(command);
  } else if (!strcmp("forget", command_name)) {
    forget_file_cmd(command);
  } else if (!strcmp("changes", command_name)) {
    get_changed_blocks_cmd(command);
  } else if (!strcmp("modify", command_name)) {
    modify_file_cmd(command);
  } else {
    printf("command '%s' is unknown. type 'help' to get list available commands.'\n",
           command_name);
  }
  return 0;
}

static void help_cmd(void) {
  printf("help                    - print this list\n"
         "exit                    - exit from the program\n"
         "list                    - print list of ids of monitored files\n"
         "monitor <block-size> <path-to-file>\n"
         "                        - start monitoring of the file for changed blocks\n"
         "forget  <id-of-file>    - stop monioring of the file\n"
         "changes <id-of-file>    - get list changed blocks since previous call of changes or start monitoring\n"
         "                          it's a synchronous operation\n"
         "modify  <id-of-file> <offset> <writing-word>\n"
         "                        - write <word> at the specified with given offset\n"
         );
}

/**
 * print list of ids of monitored files to stdout.
 */
static void monitored_files_cmd(void) {
  if (!number_monitored_files) {
    printf("there isn't any monitored file. use command 'monitore'.\n");
  } else {
    printf("ids of monitored files are: \n");
    printf("id  %14s filename\n", "handler");    
    for (int i = 0; i < MAX_MONITORED_FILES; ++i) {
      struct monitored_file * mf = monitored_files[i];      
      if (!mf)
        continue;      
      printf("%3d %14d %s\n", i, mf->mf_handler, mf->mf_name);
    }
  }  
}

/**
 *  add new file to monitoring for changing its blocks
 *  @args has format "<block-size:long> <path-to-file>"
 */
static void monitor_file_cmd(const char * args) {
  if (number_monitored_files >= MAX_MONITORED_FILES) {
    printf("you cannot monitored more than %d files\n", MAX_MONITORED_FILES);
    return;
  }
  {
    char file_path[256];
    int  block_size;
    if (2 != sscanf(args, "%d %256s\n", &block_size, file_path)) {
      printf("invalid arguments. use: <block-size> <path-to-monitored-file>\n");
      return;
    }
    pack_send_monitor(file_path, block_size);
  }  
}

static int pack_send_monitor(const char * file_path, int block_size) {
  ssize_t path_size = strlen(file_path);
  ssize_t cmd_size = sizeof(struct cbn_monitor) + path_size;  
  struct cbn_monitor * cmd = (struct cbn_monitor*)malloc(cmd_size);
  if (!cmd) {
    printf("no memory to send command\n");
    return -1;
  }
  cmd->cbn_size = cmd_size;
  cmd->cbn_block_size = block_size;
  strcpy(cmd->cbn_path, file_path);      
  {
    int file_id = ioctl(dfile, CBN_MONITOR, cmd);
    free(cmd);
    if (file_id < 0) {
      perror("cannot monitor file\n");
      return -1;
    }
    return insert_file_id(file_path, path_size, file_id);
  }  
}

static int insert_file_id(const char * file_path, ssize_t path_size, int file_id) {
  for (int i = 0; i < MAX_MONITORED_FILES; ++i) {
    if (!monitored_files[i]) {
      monitored_files[i] = (struct monitored_file *)malloc(
                             sizeof(struct monitored_file) + path_size);
      if (!monitored_files[i]) {
        printf("no memory for struct monitored_file\n");
        return -1;
      }
      ++number_monitored_files;        
      monitored_files[i]->mf_handler = file_id;
      strcpy(monitored_files[i]->mf_name, file_path);
      printf("file '%s' is added for monitoring with id %d(%d)\n",
             file_path, i, file_id);
      break;
    }
  }
  return 0;  
}

/**
 *  stop monitoring specified file
 *  @args has format "<monitored-file-id:long>"
 */
static void forget_file_cmd(const char * args) {
  int file_id;
  struct monitored_file * mf;
  if (sscanf(args, "%d", &file_id) != 1) {
    printf("id of monitored file expected\n");
    return;
  }
  mf = mf_lookup_by_id(file_id);
  if (!mf) {
    printf("invalid file id %d\n", file_id);
    return;
  }
  if (ioctl(dfile, CBN_FORGET, mf->mf_handler)) {
    perror("cannot forget about monitored file");
    return;
  }
  printf("file %s is removed from monitoring\n", mf->mf_name);
  free(mf);
  monitored_files[file_id] = 0;
  --number_monitored_files;  
}

/**
 *  get list of changed block numbers.
 *  format: <id-of-file>
 */
static void get_changed_blocks_cmd(const char * args) {
  const ssize_t max_elems = 200;
  int file_id, r;
  struct monitored_file * mf;
  int * blocks;
  ssize_t cmd_size = sizeof(struct cbn_changed_blocks) + sizeof(int);
  struct cbn_changed_blocks * cmd;
  if (sscanf(args, "%d", &file_id) != 1) {
    printf("id of monitored file expected\n");
    return;
  }  
  mf = mf_lookup_by_id(file_id);
  if (!mf) {
    printf("invalid file id %d\n", file_id);
    return;
  }
  cmd = (struct cbn_changed_blocks*)malloc(cmd_size);
  if (!cmd) {
    printf("no memory for buffer\n");
    return;
  }
  cmd->cbn_size = cmd_size;
  cmd->cbn_max = max_elems;
  blocks = cmd->cbn_blocks;
  while ((r = ioctl(dfile, CBN_CHANGED_BLOCKS, cmd))) {
    if (r < 0) {
      perror("cannot get changed block numbers");
      break;
    }
    printf("changed blocks:");
    for (int i = 0; i < r;) 
      if (CBN_IS_RANGE(blocks[i])) {
        printf(" %d[%d]", CBN_RANGE_START(blocks[i]), CBN_RANGE_LENGTH(blocks[i]));
        CBN_AFTER_RANGE(i);
      } else {
        printf(" %d", blocks[i]);
        ++i;
      }
  }
  free(cmd);
}

/**
 * simulate file modification 
 */
static void modify_file_cmd(const char * args) {
  int file_id;
  long offset;
  char word[40];
  struct monitored_file * mf;
  if (3 != sscanf(args, "%d %ld %40s", &file_id, &offset, word)) {
    printf("invalid arguments. usage: <id-of-file> <offset> <writing-word>\n");
    return;
  }
  {
    mf = mf_lookup_by_id(file_id);
    if (!mf) {
      printf("invalid file id %d\n", file_id);
      return;
    }
    int fd = open(mf->mf_name, O_WRONLY);
    if (fd < 0) {
      perror("cannot open file\n");
      return;
    }
    if (lseek(fd, offset, SEEK_SET) < 0) {
      perror("cannot seek\n");
      return;
    }
    {
      int written = write(fd, word, strlen(word));
      if (written < 0) {
        perror("error at writing to the file\n");
      } else if (written < strlen(word)) {
        printf("%d bytes has been written\n", written);
      } else {
        printf("ok. %d bytes has been written\n", written);
      }
    }
    close(fd);
  }
}

static struct monitored_file * mf_lookup_by_id(int file_id) {
  if (file_id < 0 || file_id >= MAX_MONITORED_FILES) {
    return 0;
  }
  return monitored_files[file_id];
}
