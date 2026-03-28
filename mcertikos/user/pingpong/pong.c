#include <proc.h>
#include <stdio.h>
#include <syscall.h>
#include <file.h>

int main(int argc, char **argv)
{
    int fd;
    int marker;

    fd = open("flock.prio.lock", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("pong: open flock.prio.lock failed\n");
        return 0;
    }

    if (flock(fd, LOCK_EX) < 0) {
        printf("pong: flock LOCK_EX failed\n");
        close(fd);
        return 0;
    }

    marker = open("flock.writer.acquired", O_CREATE | O_RDWR);
    if (marker >= 0) {
        close(marker);
    }

    flock(fd, LOCK_UN);
    close(fd);

    return 0;
}
