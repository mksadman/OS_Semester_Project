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
        printf("ding: open flock.prio.lock failed\n");
        return 0;
    }

    if (flock(fd, LOCK_SH | LOCK_NB) == 0) {
        marker = open("flock.reader.success", O_CREATE | O_RDWR);
        if (marker >= 0) {
            close(marker);
        }
        flock(fd, LOCK_UN);
    } else {
        marker = open("flock.reader.fail", O_CREATE | O_RDWR);
        if (marker >= 0) {
            close(marker);
        }
    }

    close(fd);
    return 0;
}
