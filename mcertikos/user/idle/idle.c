#include <proc.h>
#include <stdio.h>
#include <syscall.h>
#include <x86.h>

int main(int argc, char **argv)
{
    printf("idle\n");

#ifdef TEST
    pid_t fstest_pid;
    if ((fstest_pid = spawn(4, 1000)) != -1)
        printf("fstest in process %d.\n", fstest_pid);
    else
        printf("Failed to launch fstest.\n");
#else
    pid_t shell_pid;
    if ((shell_pid = spawn(5, 1000)) != -1)
        printf("shell in process %d.\n", shell_pid);
    else
        printf("Failed to launch shell.\n");
#endif

    return 0;
}
