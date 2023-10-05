#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char* argv[])
{
    int pipe_1[2], pipe_2[2];
    char ping = 'p';
    char pong;
    if(pipe(pipe_1) < 0 || pipe(pipe_2) < 0)
    {
        printf("pingpong error\n");
        exit(1);
    }
    write(pipe_2[1], &ping, 1);
    int pid = fork();
    if(pid == 0)
    {
        close(pipe_2[1]);
        close(pipe_1[0]);
        int id = getpid();
        read(pipe_2[0], &pong, 1);
        printf("%d: received ping\n", id);
        write(pipe_1[1], &ping, 1);
        exit(0);
    }
    else {
        close(pipe_2[0]);
        close(pipe_1[1]);
        read(pipe_1[0], &ping, 1);
        printf("%d: received pong\n", pid);
        exit(0);
    }
}