#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#define PORT 8808
#define BUFFER_SIZE 1024

int server_socket = -1;

void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down server...\n", sig);
    if (server_socket != -1) {
        close(server_socket);
    }
    exit(0);
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    // Регистрируем обработчик сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Создаем UDP сокет
    server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_socket == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Настраиваем адрес сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(PORT);

    // Привязываем сокет к адресу
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("UDP Server listening on 127.0.0.1:%d\n", PORT);
    printf("Press Ctrl+C to stop the server\n");

    // Основной цикл сервера
    while (1) {
        // Получаем данные
        ssize_t bytes_received = recvfrom(server_socket, buffer, BUFFER_SIZE - 1, 0,
                                          (struct sockaddr*)&client_addr, &client_len);

        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("Received %zd bytes from %s:%d: %s\n",
                   bytes_received,
                   inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port),
                   buffer);

            // Эхо-ответ
            sendto(server_socket, buffer, bytes_received, 0,
                   (struct sockaddr*)&client_addr, client_len);
            printf("Sent echo response\n");
        } else if (bytes_received == -1) {
            perror("recvfrom failed");
        }
    }

    close(server_socket);
    return 0;
}
