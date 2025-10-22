#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define DEST_IP "192.168.1.11"  // Адрес eth1
#define DEST_PORT 8888

int main() {
    int sockfd;
    struct sockaddr_in dest_addr;
    char buffer[BUFFER_SIZE];
    int bytes_sent;

    // Создаем UDP сокет
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Ошибка создания сокета");
        exit(EXIT_FAILURE);
    }

    // Настраиваем адрес получателя
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(DEST_PORT);
    inet_pton(AF_INET, DEST_IP, &dest_addr.sin_addr);

    printf("Отправитель запущен. Вводите строки для отправки на %s:%d\n", DEST_IP, DEST_PORT);

    while (1) {
        printf("Введите строку (или 'quit' для выхода): ");
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            break;
        }

        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "quit") == 0) {
            break;
        }

        // Отправляем данные
        bytes_sent = sendto(sockfd, buffer, strlen(buffer), 0,
                            (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        if (bytes_sent < 0) {
            perror("Ошибка отправки");
        } else {
            printf("Отправлено %d байт\n", bytes_sent);
        }
    }

    close(sockfd);
    return 0;
}
