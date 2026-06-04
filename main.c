#define _GNU_SOURCE
#include <err.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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
                if (endptr == optarg || val < 1)
                    err(EXIT_FAILURE, "%s: option '%c' requires number >= 1 as an argument\n", argv[0], ret);
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

int main(int argc, char *argv[])
{
    int port;
    char* dir;
    parse_argv(argc, argv, &port, &dir);
    printf("port: %d, dir: %s\n", port, dir);
    return 0;
}
