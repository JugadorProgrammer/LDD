#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>

int main() {
    struct termios old, newSettings;
    struct timespec first, second;
    int flag = 1;
    long elapsed = 0;
    char c;

    int fd1 = open("/dev/rand", O_RDONLY | O_NONBLOCK);
    if (fd1 == -1) {
        perror("Ошибка доступа к устройству /dev/rand");
        return 1;
    }

    printf("Введите строку (Enter для выхода):\n");
    // Сохраняем текущие настройки
    tcgetattr(STDIN_FILENO, &old);
    newSettings = old;

    // Меняем настройки
    newSettings.c_lflag &= ~(ICANON | ECHO);
    newSettings.c_cc[VMIN] = 0;
    newSettings.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);

    clock_gettime(CLOCK_MONOTONIC, &first);
    clock_gettime(CLOCK_MONOTONIC, &second);
    while(1) {
        if (read(STDIN_FILENO, &c, 1) > 0) {
            if(flag) {
                clock_gettime(CLOCK_MONOTONIC, &first);
                // Вычисление времени в наносекундах
                elapsed = labs((long)(second.tv_nsec - first.tv_nsec));
            }
            else {
                clock_gettime(CLOCK_MONOTONIC, &second);
                // Вычисление времени в наносекундах
                elapsed = labs((long)(first.tv_nsec - second.tv_nsec));
            }

            flag = ~flag;
            if (ioctl(fd1, 1, &elapsed)) {
                perror("\nОшибка добавления энтропии\n");
                goto _exit;
            }

            printf("%c", c);
            fflush(stdout);
            if (c == '\n') {
                goto _exit;
            }
        }
    }

_exit:
    // Восстанавливаем настройки
    tcsetattr(STDIN_FILENO, TCSANOW, &old);

    close(fd1);
    return 0;
}
