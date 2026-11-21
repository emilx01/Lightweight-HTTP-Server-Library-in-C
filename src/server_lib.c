#include "server_lib.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

int start_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    printf("Server listening on port %d...\n", port);
    fflush(stdout);

    char buffer[BUFFER_SIZE];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

        int total = 0;
        int header_len = 0;
        int content_len = 0;
        char *header_end = NULL;

        while (total < BUFFER_SIZE - 1) {
            ssize_t n = recv(client_fd, buffer + total, BUFFER_SIZE - 1 - total, 0);
            if (n <= 0) {
                break;
            }
            total += (int)n;
            buffer[total] = '\0';

            if (header_end == NULL) {
                header_end = strstr(buffer, "\r\n\r\n");
                if (header_end != NULL) {
                    header_len = (int)(header_end - buffer) + 4;

                    char *cl = strstr(buffer, "Content-Length:");
                    if (cl == NULL) {
                        cl = strstr(buffer, "content-length:");
                    }
                    if (cl != NULL && cl < header_end) {
                        content_len = atoi(cl + 15);
                        if (content_len < 0) {
                            content_len = 0;
                        }
                    }
                }
            }

            if (header_end != NULL && total >= header_len + content_len) {
                break;
            }
        }

        if (header_end == NULL) {
            close(client_fd);
            continue;
        }

        char method[16];
        if (sscanf(buffer, "%15s", method) != 1) {
            close(client_fd);
            continue;
        }

        if (strcmp(method, "GET") != 0 &&
            strcmp(method, "POST") != 0 &&
            strcmp(method, "UPDATE") != 0 &&
            strcmp(method, "DELETE") != 0) {
            const char *resp =
                "HTTP/1.1 405 Method Not Allowed\r\n"
                "Content-Length: 18\r\n"
                "Connection: close\r\n\r\n"
                "Method Not Allowed";
            send(client_fd, resp, strlen(resp), 0);
            close(client_fd);
            continue;
        }

        if (header_len + content_len < BUFFER_SIZE) {
            buffer[header_len + content_len] = '\0';
        }
        char *body = buffer + header_len;

        if (strlen(body) > 0) {
            printf("%s request from %s with body %s\n", method, client_ip, body);
        } else {
            printf("%s request from %s\n", method, client_ip);
        }
        fflush(stdout);

        const char *resp =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n\r\n"
            "OK";
        send(client_fd, resp, strlen(resp), 0);

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
