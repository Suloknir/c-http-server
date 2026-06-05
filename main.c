#define _GNU_SOURCE
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define MAX_EVENTS 10

volatile bool sigint_receaved = false;
DIR *document_root = NULL;
int server_fd = -1;
int epoll_fd = -1;

void print_spinner(int port)
{
    static uint8_t i = 0;
    const char *const dots[] = {
        "|>    |", // 0
        "| >   |", // 1
        "|  >  |", // 2
        "|   > |", // 3
        "|    >|", // 4
        "|    ^|", // 5
        "|    <|", // 6
        "|   < |", // 7
        "|  <  |", // 8
        "| <   |", // 9
        "|<    |", // 10
        "|^    |", // 11
    };
    printf("\rListening on %d %s", port, dots[i % 12]);
    fflush(stdout);
    i++;
}

void sigint_handler(int signum) // NOLINT
{
    sigint_receaved = true;
}

void clean_exit(int status, char *msg)
{
    if (document_root != NULL)
        closedir(document_root);
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

void open_document_root(char *path)
{
    document_root = opendir(path);
    if (document_root == NULL)
        clean_exit(EXIT_FAILURE, "opendir error");
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
    struct epoll_event ev;
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

void handle_http_request(int client_fd, char *buffer)
{
    char method[16];
    char path[256];
    char protocol[16];
    if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) != 3)
        return;
    if (strcmp(method, "TESTING") != 0)
    {
        const char *response_501 = //
            "HTTP/1.1 501 Not implemented\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n"
            "\r\n";
        write(client_fd, response_501, strlen(response_501));
        return;
    }
}

void process_request(int client_fd)
{
    const int buffer_size = 4096;
    char buffer[buffer_size];
    ssize_t bytes_read = read(client_fd, &buffer, buffer_size - 1);
    if (unlikely(bytes_read == -1))
    {
        perror("read error");
        goto close;
    }
    if (bytes_read == 0)
        goto close;
    buffer[bytes_read] = '\0';
    handle_http_request(client_fd, buffer);
close:
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
        if (sigint_receaved)
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
            {
                register_client();
            }
            else
            {
                process_request(events[i].data.fd);
            }
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
    int port;
    char *document_root_path;
    parse_argv(argc, argv, &port, &document_root_path);
    open_document_root(document_root_path);
    setup_server_socket(port);
    setup_epoll(false);

    printf("Port: %d\n", port);
    printf("Document root: %s\n", document_root_path);
    printf("-------------------------\n");
    loop(port);
    clean_exit(EXIT_SUCCESS, NULL);
    return 0;
}
