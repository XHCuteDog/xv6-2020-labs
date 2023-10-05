#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

int main(int argc, char* argv[]) {
  char buf[512];
  int status;
  int byte_read = read(0, buf, 512);
  // printf("xargs called read is %s\n", buf);
  char* line = buf;
  for (int i = 0; i < byte_read; i++) {
    if (buf[i] == '\n' || buf[i] == '\0') {
      buf[i] = '\0';
      int pid = fork();
      if (pid) {
        wait(&status);
      } else {
        char* new_argv[argc + 1];
        for (int j = 0; j < (argc - 1); j++) {
          new_argv[j] = argv[j + 1];
        }
        new_argv[argc - 1] = line;
        new_argv[argc] = '\0';
        exec(argv[1], new_argv);
        exit(1);
      }
      line = &buf[i + 1];
    }
  }
  exit(0);
}