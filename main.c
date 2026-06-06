#define _GNU_SOURCE
#include <arpa/inet.h>
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
#include <time.h>
#include <unistd.h>

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

#define MAX_EVENTS 10

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

volatile sig_atomic_t sigint_received = false;
char *document_root_path = NULL;
const char *log_path = "server.log";

int server_fd = -1;
int epoll_fd = -1;
FILE *log_file = NULL;

void print_spinner(const int port)
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

void clean_exit(const int status, const char *msg)
{
    if (server_fd != -1)
        close(server_fd);
    if (epoll_fd != -1)
        close(epoll_fd);
    if (log_file != NULL)
        fclose(log_file);

    if (msg != NULL)
        perror(msg);
    exit(status);
}

void parse_argv(const int argc, char *const *argv, int *ret_port, char **ret_dir)
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

void setup_server_socket(const int port)
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

void setup_epoll(const bool cloexec)
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
        clean_exit(EXIT_FAILURE, "epoll_ctl error");
}

void open_or_create_log_file(void)
{
    log_file = fopen(log_path, "a");
    if (unlikely(log_file == NULL))
        clean_exit(EXIT_FAILURE, "fopen error");
}

void ensure_log_file_integrity(void)
{
    struct stat fd_stat;
    struct stat path_stat;
    if (fstat(fileno(log_file), &fd_stat) == -1)
    {
        perror("fstat error");
        return;
    }
    if (stat(log_path, &path_stat) == -1 || fd_stat.st_ino != path_stat.st_ino)
    {
        printf("Reopening log file\n");
        fclose(log_file);
        open_or_create_log_file();
    }
}

void write_to_log(const int client_fd, const char *method, const char *path, const int status_code)
{
    static size_t log_id = 1;
    const time_t raw_time = time(NULL);
    struct tm *time_info = localtime(&raw_time);
    char date_buffer[11];
    char time_buffer[9];
    strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d", time_info);
    strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", time_info);
    char ip_buffer[INET_ADDRSTRLEN] = "unknown";
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);
    if (getpeername(client_fd, &peer_addr, &peer_len) == 0)
        inet_ntop(AF_INET, &peer_addr.sin_addr, ip_buffer, sizeof(ip_buffer));

    flockfile(log_file);
    ensure_log_file_integrity();
    fprintf(log_file, "%-6zu | %-10s | %-8s | %-15s | %-5.10s | %-30.255s | %d\n", //
            log_id++, //
            date_buffer, //
            time_buffer, //
            ip_buffer, //
            method ? method : "", //
            path ? path : "", //
            status_code);
    fflush(log_file);
    funlockfile(log_file);
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

void send_response_error_code(const int client_fd, const int response_code, const char *method, const char *path)
{
    const char *status_line;
    const char *header_common_content = //
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";
    char response[512];
    // clang-format off
    switch (response_code)
    {
        case 400: status_line = "HTTP/1.1 400 Bad Request\r\n"; break;
        case 403: status_line = "HTTP/1.1 403 Forbidden\r\n"; break;
        case 404: status_line = "HTTP/1.1 404 Not Found\r\n"; break;
        case 500: status_line = "HTTP/1.1 500 Internal Server Error\r\n"; break;
        case 501: status_line = "HTTP/1.1 501 Not implemented\r\n"; break;
        case 505: status_line = "HTTP/1.1 505 HTTP Version Not Supported\r\n"; break;
        default:
            fprintf(stderr, "Wrong reponse code\n");
            status_line = "HTTP/1.1 500 Internal Server Error\r\n";
    }
    // clang-format on
    int response_len = snprintf(response, sizeof(response), "%s%s", status_line, header_common_content);
    printf("Sent error code: %d\n", response_code);
    write(client_fd, response, response_len);
    write_to_log(client_fd, method, path, response_code);
}

const char *get_mime_type(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL)
        return "text/plain; charset=utf-8";
    // clang-format off
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcmp(dot, ".css") == 0)                              return "text/css; charset=utf-8";
    if (strcmp(dot, ".js") == 0)                               return "application/javascript; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".png") == 0)                              return "image/png";
    if (strcmp(dot, ".gif") == 0)                              return "image/gif";
    if (strcmp(dot, ".txt") == 0)                              return "text/plain; charset=utf-8";
    // clang-format on
    return "application/octet-stream";
}

void handle_get_request(const int client_fd, const char *method, char *path, const size_t path_max_len)
{
    if (strstr(path, "..") != NULL)
    {
        send_response_error_code(client_fd, 403, method, path);
        return;
    }
    if (strcmp(path, "/") == 0)
        snprintf(path, path_max_len, "%s", "/index.html");
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s%s", document_root_path, path);
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd == -1)
    {
        // printf("File %s not found\n", file_path);
        send_response_error_code(client_fd, 404, method, path);
        return;
    }
    struct stat file_stat;
    if (fstat(file_fd, &file_stat) == -1)
    {
        send_response_error_code(client_fd, 500, method, path);
        goto close_file;
    }
    if (S_ISDIR(file_stat.st_mode))
    {
        // printf("File %s is a directory\n", file_path);
        send_response_error_code(client_fd, 403, method, path);
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
    // todo: write to log
    write_to_log(client_fd, method, path, 200);
    sendfile(client_fd, file_fd, NULL, file_stat.st_size);
close_file:
    close(file_fd);
}

void process_request(const int client_fd)
{
    const int buffer_size = 4096;
    char request_buffer[buffer_size];
    char method[16];
    const size_t path_max_len = 256;
    char path[path_max_len];
    char protocol[16] = {0};
    ssize_t bytes_read = read(client_fd, request_buffer, buffer_size - 1);
    if (unlikely(bytes_read == -1))
    {
        perror("read error");
        send_response_error_code(client_fd, 500, NULL, NULL);
        goto close_client;
    }
    if (bytes_read == 0)
        goto close_client;
    request_buffer[bytes_read] = '\0';
    if (sscanf(request_buffer, "%15s %255s %15s", method, path, protocol) != 3)
    {
        send_response_error_code(client_fd, 400, NULL, NULL);
        goto close_client;
    }
    if (strcmp(protocol, "HTTP/1.1") != 0)
        send_response_error_code(client_fd, 505, method, path);
    if (strcmp(method, "GET") == 0)
        handle_get_request(client_fd, method, path, path_max_len);
    else
        send_response_error_code(client_fd, 501, method, path);
close_client:
    // epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL); // not needed - close will do the job
    close(client_fd);
}

void loop(const int port)
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
    open_or_create_log_file();

    printf("Port: %d\n", port);
    printf("Document root: %s\n", document_root_path);
    loop(port);
    clean_exit(EXIT_SUCCESS, NULL);
    return 0;
}
