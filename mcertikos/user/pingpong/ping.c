#include <proc.h>
#include <stdio.h>
#include <syscall.h>
#include <file.h>

#define HOLD_ITERS 3000

int main(int argc, char **argv)
{
    int fd;
    int marker;
    int i;

    fd = open("flock.shared.lock", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("ping: open flock.shared.lock failed\n");
        return 0;
    }

    if (flock(fd, LOCK_EX) < 0) {
        printf("ping: flock LOCK_EX failed\n");
        close(fd);
        return 0;
    }

    marker = open("flock.shared.holding", O_CREATE | O_RDWR);
    if (marker >= 0) {
        close(marker);
    }

    for (i = 0; i < HOLD_ITERS; i++) {
        yield();
    }

    flock(fd, LOCK_UN);
    close(fd);

    marker = open("flock.shared.done", O_CREATE | O_RDWR);
    if (marker >= 0) {
        close(marker);
    }

    return 0;
}
