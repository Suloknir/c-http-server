#define _GNU_SOURCE
#include <dirent.h>
#include <err.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

volatile bool sigint_receaved = false;
DIR *document_root = NULL;
int server_fd = -1;

void clean_exit(int status)
{
    if (document_root != NULL)
        closedir(document_root);
    if (server_fd != -1)
        close(server_fd);
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
    {
        perror("opendir error");
        clean_exit(EXIT_FAILURE);
    }
}

void setup_server_socket(int port)
{
    int ret;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (unlikely(server_fd == -1))
        goto err;
    int optval = 1;
    ret = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (unlikely(ret == -1))
        goto err;
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);
    ret = bind(server_fd, &serv_addr, sizeof(serv_addr));
    if (unlikely(ret == -1))
        goto err;
    ret = listen(server_fd, SOMAXCONN);
    if (unlikely(ret == -1))
        goto err;
    return;
err:
    perror("error");
    clean_exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{

    int port;
    char *document_root_path;
    parse_argv(argc, argv, &port, &document_root_path);
    open_document_root(document_root_path);
    setup_server_socket(port);
    printf("port: %d, document_root: %s\n", port, document_root_path);

    clean_exit(EXIT_SUCCESS);
    return 0;
}
