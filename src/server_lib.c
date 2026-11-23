#define _GNU_SOURCE

#include "server_lib.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

static int send_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int header_has_token(const char *headers, const char *header_name, const char *token) {
    const char *h = strcasestr(headers, header_name);
    if (h == NULL) {
        return 0;
    }
    const char *val = h + strlen(header_name);
    const char *line_end = strstr(val, "\r\n");
    const char *match = strcasestr(val, token);
    if (match == NULL) {
        return 0;
    }
    return (line_end == NULL || match < line_end);
}

/* Returns: 1 = complete, 0 = incomplete, -1 = malformed */
static int decode_chunked_body(const char *src, size_t src_len, char *dst, size_t dst_size) {
    size_t pos = 0;
    size_t out = 0;

    while (1) {
        const char *line_end = strstr(src + pos, "\r\n");
        if (line_end == NULL) {
            return 0;
        }

        errno = 0;
        char *endptr = NULL;
        unsigned long chunk_size = strtoul(src + pos, &endptr, 16);
        if (errno == ERANGE || endptr == src + pos) {
            return -1;
        }

        pos = (size_t)(line_end - src) + 2;

        if (chunk_size == 0) {
            if (src_len - pos < 2) {
                return 0;
            }
            if (src[pos] == '\r' && src[pos + 1] == '\n') {
                dst[out] = '\0';
                return 1;
            }
            if (strstr(src + pos, "\r\n\r\n") != NULL) {
                dst[out] = '\0';
                return 1;
            }
            return 0;
        }

        if (chunk_size > src_len - pos || (src_len - pos - chunk_size) < 2) {
            return 0;
        }

        if (src[pos + chunk_size] != '\r' || src[pos + chunk_size + 1] != '\n') {
            return -1;
        }

        if (chunk_size >= dst_size - out) {
            return -1;
        }

        memcpy(dst + out, src + pos, chunk_size);
        out += chunk_size;
        pos += chunk_size + 2;
    }
}

int start_server(int port) {
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port: %d\n", port);
        return -1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    printf("Server listening on port %d...\n", port);
    fflush(stdout);

    char buffer[BUFFER_SIZE];
    char chunked_body[BUFFER_SIZE];

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip)) == NULL) {
            strncpy(client_ip, "unknown", sizeof(client_ip));
            client_ip[sizeof(client_ip) - 1] = '\0';
        }

        size_t total = 0;
        size_t header_len = 0;
        size_t content_len = 0;
        char *header_end = NULL;
        int is_chunked = 0;
        int chunked_status = 0;
        int recv_error = 0;
        int bad_request = 0;
        int payload_too_large = 0;

        while (total < BUFFER_SIZE - 1) {
            ssize_t n = recv(client_fd, buffer + total, (BUFFER_SIZE - 1) - total, 0);
            if (n < 0) {
                perror("recv");
                recv_error = 1;
                break;
            }
            if (n == 0) {
                break;
            }

            total += (size_t)n;
            buffer[total] = '\0';

            if (header_end == NULL) {
                header_end = strstr(buffer, "\r\n\r\n");
                if (header_end != NULL) {
                    header_len = (size_t)(header_end - buffer) + 4;

                    /* Temporarily terminate headers so header lookups cannot scan into the body */
                    *header_end = '\0';

                    is_chunked = header_has_token(buffer, "\r\ntransfer-encoding:", "chunked");

                    if (!is_chunked) {
                        const char *cl = strcasestr(buffer, "\r\ncontent-length:");
                        if (cl != NULL) {
                            errno = 0;
                            char *endptr = NULL;
                            long val = strtol(cl + 17, &endptr, 10);
                            if (errno == ERANGE || endptr == cl + 17 || val < 0) {
                                bad_request = 1;
                                *header_end = '\r';
                                break;
                            }
                            if ((size_t)val > (BUFFER_SIZE - 1) - header_len) {
                                payload_too_large = 1;
                                *header_end = '\r';
                                break;
                            }
                            content_len = (size_t)val;
                        }
                    }

                    int expect_continue = header_has_token(buffer, "\r\nexpect:", "100-continue");
                    *header_end = '\r';

                    if (expect_continue) {
                        const char *resp_100 = "HTTP/1.1 100 Continue\r\n\r\n";
                        if (send_all(client_fd, resp_100, strlen(resp_100)) < 0) {
                            recv_error = 1;
                            break;
                        }
                    }
                }
            }

            if (header_end != NULL) {
                if (is_chunked) {
                    chunked_status = decode_chunked_body(
                        buffer + header_len,
                        total - header_len,
                        chunked_body,
                        sizeof(chunked_body)
                    );
                    if (chunked_status == 1) {
                        break;
                    }
                    if (chunked_status < 0) {
                        bad_request = 1;
                        break;
                    }
                } else if (total - header_len >= content_len) {
                    break;
                }
            }
        }

        if (recv_error) {
            close(client_fd);
            continue;
        }

        if (payload_too_large || (header_end == NULL && total == BUFFER_SIZE - 1) ||
            (is_chunked && chunked_status == 0 && total == BUFFER_SIZE - 1)) {
            const char *resp_413 =
                "HTTP/1.1 413 Payload Too Large\r\n"
                "Content-Length: 17\r\n"
                "Connection: close\r\n\r\n"
                "Payload Too Large";
            send_all(client_fd, resp_413, strlen(resp_413));
            close(client_fd);
            continue;
        }

        if (bad_request) {
            const char *resp_400 =
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Length: 11\r\n"
                "Connection: close\r\n\r\n"
                "Bad Request";
            send_all(client_fd, resp_400, strlen(resp_400));
            close(client_fd);
            continue;
        }

        /* Client disconnected before sending complete headers or body */
        if (header_end == NULL ||
            (is_chunked && chunked_status != 1) ||
            (!is_chunked && total - header_len < content_len)) {
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
            const char *resp_405 =
                "HTTP/1.1 405 Method Not Allowed\r\n"
                "Content-Length: 18\r\n"
                "Connection: close\r\n\r\n"
                "Method Not Allowed";
            send_all(client_fd, resp_405, strlen(resp_405));
            close(client_fd);
            continue;
        }

        char *body;
        if (is_chunked) {
            body = chunked_body;
        } else {
            buffer[header_len + content_len] = '\0';
            body = buffer + header_len;
        }

        if (body[0] != '\0') {
            printf("%s request from %s with body %s\n", method, client_ip, body);
        } else {
            printf("%s request from %s\n", method, client_ip);
        }
        fflush(stdout);

        const char *resp_200 =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 2\r\n"
            "Connection: close\r\n\r\n"
            "OK";
        send_all(client_fd, resp_200, strlen(resp_200));

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
