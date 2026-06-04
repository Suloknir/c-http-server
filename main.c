#define _GNU_SOURCE
#include <dirent.h>
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

void parse_argv(int argc, char *const *argv, int *ret_port, char **ret_dir)
{
    if (!ret_port || !ret_dir)
        err(EXIT_FAILURE, "parse_argv requires non-null arguments\n");
    bool port = false;
    bool dir = false;
    opterr = 0;
    int ret;
    while ((ret = getopt(argc, argv, "d:p:")) != -1)
    {
        switch (ret)
        {
            case 'd':
                dir = true;
                *ret_dir = optarg;
                break;
            case 'p':
            {
                port = true;
                char *endptr;
                const int val = (int)strtol(optarg, &endptr, 0);
                if (endptr == optarg)
                    err(EXIT_FAILURE, "%s: option '%c' requires number as an argument\n", argv[0], ret);
                *ret_port = val;
                break;
            }
            case '?':
                err(EXIT_FAILURE, "%s: Option '%c' requires argument\n", argv[0], optopt);
                break;
            default:
                abort();
        }
    }
    if (!port || !dir)
    {
        fprintf(stderr, "Parameters 'd', 'p' are mandatory\n");
        fprintf(stderr, "Usage: %s -p [port] -d [directory]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
}

DIR *open_document_root(char *path)
{
    DIR *result = opendir(path);
    if (result == NULL)
    {
        perror("opendir error");
        exit(EXIT_FAILURE);
    }
    return result;
}

int main(int argc, char *argv[])
{
    int port;
    char *document_root_path;
    parse_argv(argc, argv, &port, &document_root_path);
    DIR *document_root = open_document_root(document_root_path);
    printf("port: %d, document_root: %s\n", port, document_root_path);
    closedir(document_root);
    return 0;
}
