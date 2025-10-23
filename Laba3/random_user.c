#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>

struct result {

    long value;
    long seed;
};

int main()
{
    long number;
    int fd1 = open("/dev/rand", O_RDONLY | O_NONBLOCK);
    if (fd1 == -1) {
        perror("Ошибка доступа к устройству /dev/rand");
        return 1;
    }

    struct result rand_res;

    while(1) {
        if (ioctl(fd1, 0, &rand_res)) {
            perror("\nОшибка чтения\n");
            goto _exit;
        }

        printf("Number = %ld; Seed = %ld\n", rand_res.value, rand_res.seed);
        sleep(1);
    }

_exit:
    close(fd1);
    return 0;
}
