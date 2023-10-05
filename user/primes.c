#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void primes(int *fd);

int main(int argc, char *argv[])
{
    int p[2];
    pipe(p);

    // Write numbers 2 through 35 to the pipe
    for (int i = 2; i <= 35; ++i)
    {
        write(p[1], &i, sizeof(int));
    }
    close(p[1]);

    // Start the pipeline of prime processes
    primes(p);

    exit(0);
}

void primes(int *fd)
{
    int p_son[2];
    int prime;

    // Read a number from the left neighbor
    if (read(fd[0], &prime, sizeof(int)) == 0)
    {
        close(fd[0]);
        return;
    }
    // Print the prime number
    printf("prime %d\n", prime);

    pipe(p_son);

    int pid = fork();
    if (pid == 0)
    { // Child
        close(fd[0]);
        close(p_son[1]);
        primes(p_son);
        exit(0);
    }
    else
    { // Parent
        close(p_son[0]);
        int num;
        while (read(fd[0], &num, sizeof(int)) > 0)
        {
            if (num % prime != 0)
            {
                write(p_son[1], &num, sizeof(int));
            }
        }
        close(p_son[1]);
        close(fd[0]);        
        wait(0);
    }
}
