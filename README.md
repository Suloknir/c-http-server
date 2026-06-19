# Epoll HTTP Server in C
![C](https://img.shields.io/badge/language-C17-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)

A fast, single-threaded, event-driven HTTP/1.1 server written in pure C. This project demonstrates low-level Linux system programming, asynchronous I/O, and zero-copy file transmission.

## Features
* **Event-driven Architecture:** Utilizes `epoll` for handling multiple concurrent connections efficiently within a single thread.
* **Zero-copy Data Transfer:** Implements the `sendfile()` system call to bypass user-space buffering, maximizing file transmission throughput.
* **Daemonization:** Can be detached from the terminal to run entirely in the background using the `-a` flag.
* **Logging:** Logs requests (IP, method, path, status) to `server.log` with automatic log file integrity checks (reopens the file if it is moved or deleted during runtime).
* **MIME Type Resolution:** Automatically serves correct `Content-Type` headers for standard web assets (HTML, CSS, JS, TXT, and images).
* **Security Measures:** Prevents directory traversal attacks by filtering `..` in paths and safely blocks attempts to serve raw directories.
* **Graceful Shutdown:** Intercepts `SIGINT` signals to properly clean up and close all active file descriptors before exiting.
* **CLI Spinner:** Provides visual feedback in the terminal when running in the foreground.

## Prerequisites
* Linux operating system (relies heavily on Linux-specific APIs: `<sys/epoll.h>` and `<sys/sendfile.h>`)
* GCC compiler
## Build Instructions

Compile the server directly from the source code using the following command:

```
gcc -std=c17 -Wall -Wextra -O2 main.c -o http_daemon_server
```
