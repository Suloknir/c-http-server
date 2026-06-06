#define _GNU_SOURCE
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define MAX_EVENTS 10

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

volatile sig_atomic_t sigint_received = false;
char *document_root_path = NULL;

// DIR *document_root = NULL;
int server_fd = -1;
int epoll_fd = -1;

void print_spinner(int port)
{
    static uint8_t i = 0;
    const char *const frames[] = {
        "|>    |", //
        "| >   |", //
        "|  >  |", //
        "|   > |", //
        "|    >|", //
        "|    ^|", //
        "|    <|", //
        "|   < |", //
        "|  <  |", //
        "| <   |", //
        "|<    |", //
        "|^    |", //
    };
    printf("\rListening on %-5d %s", port, frames[i % ARRAY_SIZE(frames)]);
    fflush(stdout);
    i++;
}

void sigint_handler(int signum) // NOLINT
{
    sigint_received = true;
}

void clean_exit(int status, char *msg)
{
    if (server_fd != -1)
        close(server_fd);
    if (epoll_fd != -1)
        close(epoll_fd);

    if (msg != NULL)
        perror(msg);
    exit(status);
}

void parse_argv(int argc, char *const *argv, int *ret_port, char **ret_dir)
{
    if (!ret_port || !ret_dir)
        errx(EXIT_FAILURE, "parse_argv requires non-null arguments");
    bool port_flag = false;
    bool dir_flag = false;
    opterr = 0;
    int ret;
    while ((ret = getopt(argc, argv, "d:p:")) != -1)
    {
        switch (ret)
        {
            case 'd':
                dir_flag = true;
                *ret_dir = optarg;
                break;
            case 'p':
            {
                port_flag = true;
                char *endptr;
                const int val = (int)strtol(optarg, &endptr, 0);
                if (endptr == optarg)
                    errx(EXIT_FAILURE, "option '%c' requires number as an argument", ret);
                if (val < 0 || val > 65535)
                    errx(EXIT_FAILURE, "Port should be in range <0, 65535>");

                *ret_port = val;
                break;
            }
            case '?':
                errx(EXIT_FAILURE, "no such option [-%c]", optopt);
                break;
            default:
                abort();
        }
    }
    if (!port_flag || !dir_flag)
    {
        fprintf(stderr, "Parameters 'd', 'p' are mandatory\n");
        fprintf(stderr, "Usage: %s -p [port] -d [directory]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

void check_document_root(void)
{
    struct stat sb;
    if (stat(document_root_path, &sb) != 0)
        clean_exit(EXIT_FAILURE, "stat error");
    if (!S_ISDIR(sb.st_mode))
    {
        fprintf(stderr, "%s is not a directory\n", document_root_path);
        clean_exit(EXIT_FAILURE, NULL);
    }
}

void normalize_path(void)
{
    size_t len = strlen(document_root_path);
    while (len >= 2 && document_root_path[len - 1] == '/')
    {
        document_root_path[len - 1] = '\0';
        len--;
    }
}

void setup_server_socket(int port)
{
    int ret;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (unlikely(server_fd == -1))
        clean_exit(EXIT_FAILURE, "socket error");
    int optval = 1;
    ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (unlikely(ret == -1))
        clean_exit(EXIT_FAILURE, "setsockopt error");
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    ret = bind(server_fd, &serv_addr, sizeof(serv_addr));
    if (unlikely(ret == -1))
        clean_exit(EXIT_FAILURE, "bind error");
    ret = listen(server_fd, SOMAXCONN);
    if (unlikely(ret == -1))
        clean_exit(EXIT_FAILURE, "listen error");
}

void setup_epoll(bool cloexec)
{
    const int flags = cloexec ? EPOLL_CLOEXEC : 0;
    epoll_fd = epoll_create1(flags);
    if (unlikely(epoll_fd) == -1)
        clean_exit(EXIT_FAILURE, "epoll_create1 error");
    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    const int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);
    if (unlikely(ret == -1))
        clean_exit(EXIT_FAILURE, "(setup) epoll_ctl error");
}

void register_client(void)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(server_fd, &client_addr, &addr_len);
    if (client_fd == -1)
    {
        perror("(register) accept error");
        return;
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = client_fd;
    const int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
    if (unlikely(ret == -1))
        clean_exit(EXIT_FAILURE, "(register client) epoll_ctl error");
}

void send_response_error_code(int client_fd, int response_code)
{
    const char *status_line;
    const char *header_common_content = //
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    char response[512];
    switch (response_code)
    {
        case 400:
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            break;
        case 403:
            status_line = "HTTP/1.1 403 Forbidden\r\n";
            break;
        case 404:
            status_line = "HTTP/1.1 404 Not Found\r\n";
            break;
        case 500:
            status_line = "HTTP/1.1 500 Internal Server Error\r\n";
            break;
        case 501:
            status_line = "HTTP/1.1 501 Not implemented\r\n";
            break;
        default:
            fprintf(stderr, "Wrong reponse code");
            status_line = "HTTP/1.1 400 Bad Request\r\n";
    }
    int response_len = snprintf(response, sizeof(response), "%s%s", status_line, header_common_content);
    printf("Sent error code: %d\n", response_code);
    write(client_fd, response, response_len);
}

const char *get_mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";
    // clang-format off
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".gif") == 0) return "image/gif";
    if (strcmp(dot, ".txt") == 0) return "text/plain; charset=utf-8";
    // clang-format on
    return "application/octet-stream";
}

void handle_get_request(const int client_fd, char *path, const size_t path_max_len)
{
    if (strstr(path, "..") != NULL)
    {
        send_response_error_code(client_fd, 403);
        return;
    }
    if (strcmp(path, "/") == 0)
        snprintf(path, path_max_len, "%s", "/index.html");
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s%s", document_root_path, path);
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd == -1)
    {
        printf("File %s not found\n", file_path);
        send_response_error_code(client_fd, 404);
        return;
    }
    struct stat file_stat;
    if (fstat(file_fd, &file_stat) == -1)
    {
        send_response_error_code(client_fd, 500);
        goto close_file;
    }
    if (S_ISDIR(file_stat.st_mode))
    {
        printf("File %s is a directory\n", file_path);
        send_response_error_code(client_fd, 403);
        goto close_file;
    }

    char header_buffer[512];
    const char *mime_type = get_mime_type(file_path);
    int header_len = snprintf(header_buffer, sizeof(header_buffer),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %ld\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              mime_type, file_stat.st_size);
    write(client_fd, header_buffer, header_len);
    sendfile(client_fd, file_fd, NULL, file_stat.st_size);
close_file:
    close(file_fd);
}

void process_request(int client_fd)
{
    const int buffer_size = 4096;
    char request_buffer[buffer_size];
    char method[16];
    const size_t path_max_len = 256;
    char path[path_max_len];
    char protocol[16];
    ssize_t bytes_read = read(client_fd, request_buffer, buffer_size - 1);
    if (unlikely(bytes_read == -1))
    {
        perror("read error");
        send_response_error_code(client_fd, 500);
        goto close_client;
    }
    if (bytes_read == 0)
    {
        printf("Client disconnected\n");
        goto close_client;
    }
    request_buffer[bytes_read] = '\0';
    if (sscanf(request_buffer, "%15s %255s %15s", method, path, protocol) != 3)
    {
        send_response_error_code(client_fd, 400);
        goto close_client;
    }
    if (strcmp(method, "GET") == 0)
        handle_get_request(client_fd, path, path_max_len);
    else
        send_response_error_code(client_fd, 501);
close_client:
    // epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL); // not needed - close will do the job
    close(client_fd);
}

void loop(int port)
{
    struct epoll_event events[MAX_EVENTS];
    int nfds;
    for (;;)
    {
        print_spinner(port);
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 300);
        if (sigint_received)
            break;
        if (nfds == -1)
        {
            if (errno == EINTR)
                continue;
            clean_exit(EXIT_FAILURE, "epoll_wait error");
        }
        else if (nfds == 0)
            continue;
        printf("\r\033[K");
        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == server_fd)
                register_client();
            else
                process_request(events[i].data.fd);
        }
    }
}

int main(int argc, char *argv[])
{
    struct sigaction sa = {0};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = sigint_handler;
    sigaction(SIGINT, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
    int port;
    parse_argv(argc, argv, &port, &document_root_path);
    normalize_path();
    check_document_root();
    setup_server_socket(port);
    setup_epoll(false);

    printf("Port: %d\n", port);
    printf("Document root: %s\n", document_root_path);
    loop(port);
    clean_exit(EXIT_SUCCESS, NULL);
    return 0;
}
