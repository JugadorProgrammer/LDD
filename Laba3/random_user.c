#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>

int main()
{
    long number;
    int fd1 = open("/dev/rand", O_RDONLY | O_NONBLOCK);
    if (fd1 == -1) {
        perror("Ошибка доступа к устройству /dev/rand");
        return 1;
    }

    while(1) {
        if (ioctl(fd1, 0, &number)) {
            perror("\nОшибка чтения\n");
            goto _exit;
        }
        printf("Next random number: %ld\n", number);
        sleep(1);
    }

_exit:
    close(fd1);
    return 0;
}
