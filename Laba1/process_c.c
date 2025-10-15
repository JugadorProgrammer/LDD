#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

int main() {
    int fd1 = open("/dev/scull_buffer0", O_RDONLY | O_NONBLOCK);
    int fd2 = open("/dev/scull_buffer1", O_RDONLY | O_NONBLOCK);

    if (fd1 < 0 || fd2 < 0) {
        perror("open");
        exit(1);
    }

    int size1, size2;

    while (1) {
        // Используем ioctl для получения размера данных
        if (ioctl(fd1, 0, &size1) == 0) {
            printf("scull1 buffer data size: %d bytes\n", size1);
        }
        if (ioctl(fd2, 0, &size2) == 0) {
            printf("scull2 buffer data size: %d bytes\n", size2);
        }
        printf("---\n");
        sleep(2);
    }

    close(fd1);
    close(fd2);
    return 0;
}
