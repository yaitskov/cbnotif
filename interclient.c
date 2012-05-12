#include <stdio.h>
#include <readline/readline.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

/**
 *  This is an interactive client of cbnotif kernel module.
 *  Converts commands to binary format and
 *  decode binary output.
 */
int process_command(char * command);
void help_cmd();
void monitored_files_cmd(const char * args);
void monitore_file_cmd(const char * args);
void forget_file_cmd(const char * args);
void get_changed_blocks_cmd(const char * args);
void modify_file_cmd(const char * args);

#define MAX_MONITORED_FILES 10

long monitored_files[MAX_MONITORED_FILES];
int number_monitored_files = 0;
int dfile;
int main(int argc, char ** argv) {
  dfile = open("/dev/cbnotif", O_RDWR);
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
int process_command(char * command) {
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
    monitored_files_cmd(command);
  } else if (!strcmp("monitore", command_name)) {
    monitore_file_cmd(command);
  } else if (!strcmp("forget", command_name)) {
    forget_file_cmd(command);
  } else if (!strcmp("changes", command_name)) {
    get_changed_blocks_cmd(command);
  } else if (!strcmp("modify", command_name)) {
    modify_file_cmd(command);
  } else {
    printf("command '%s' is unknown. type 'help' to get list available commands.'\n", command_name);
  }
  return 0;
}

void help_cmd() {
  printf("help                    - print this list\n"
         "exit                    - exit from the program\n"
         "list                    - print list of ids of monitored files\n"
         "monitore <block-size> <path-to-file>\n"
         "                        - start monitoring of the file for changed blocks\n"
         "forget   <id-of-file>   - stop monioring of the file\n"
         "changes <id-of-file>    - get list changed blocks since previous call of changes or start monitoring\n"
         "                          it's a synchronous operation\n"
         "modify  <offset> <word> <path-to-file>\n"
         "                        - write <word> at the specified with given offset\n"
         );
}
void monitored_files_cmd(const char * args) {
  if (!number_monitored_files) {
    printf("there isn't any monitored file. use command 'monitore'.\n");
  } else {
    printf("ids of monitored files are: ");
    for (int i = 0; i < number_monitored_files; ++i) {
      printf("%20ld", monitored_files[i]);
    }
    printf("\n");
  }  
}
void monitore_file_cmd(const char * args) {
  if (number_monitored_files >= MAX_MONITORED_FILES) {
    printf("you cannot monitored more than %d files\n", MAX_MONITORED_FILES);
    return;
  }
  // get block size and path to file
  {
    char file_path[256];
    long block_size;
    if (2 != sscanf(args, "%ld %256s\n", &block_size, file_path)) {
      printf("invalid arguments. use: <block-size> <path-to-monitored-file>\n");
      return;
    }
    {
      int command_size = sizeof(long) + strlen(file_path) + 2 * sizeof(char);
      char * command = (char*)malloc(command_size);
      if (!command) {
        printf("no memory to send command\n");
        return;
      }
      *command = 'm';
      *(long*)(command+sizeof(char)) = block_size;
      strcpy(command+sizeof(char)+sizeof(long), file_path);      
      {
        int written = write(dfile, command, command_size);
        free(command);
        if (written < 0) {
          perror("sending command finished with error\n");
          return;
        }
        if (written < command_size) {
          printf("command has size %d bytes but sent only %d\n", command_size, written);
          return;
        }
      }
    }
    printf("file '%s' is added for monitoring\n", file_path);
  }  
}
void forget_file_cmd(const char * args) {
  
}
void get_changed_blocks_cmd(const char * args) {
}
/**
 * simulate file modification 
 */
void modify_file_cmd(const char * args) {
  long offset;
  char word[40];
  char file_path[256];
  if (3 != sscanf(args, "%ld %40s %256s", &offset, word, file_path)) {
    printf("invalid arguments. usage: <offset> <writing-word> <path-to-file>\n");
    return;
  }
  {
    int fd = open(file_path, O_WRONLY);
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
        perror("errror at writing to the file\n");
      } else if (written < strlen(word)) {
        printf("%d bytes has been written\n", written);
      } else {
        printf("ok. %d bytes has been written\n", written);
      }
    }
    close(fd);
  }
}
