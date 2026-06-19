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

## Requirements
* Linux operating system (relies heavily on Linux-specific APIs: `<sys/epoll.h>` and `<sys/sendfile.h>`)
* GCC compiler
## Build Instructions

Compile the server directly from the source code using the following command:
```bash
gcc -std=c17 -Wall -Wextra -O2 main.c -o http_daemon_server
```

## Usage
To start the server, you must provide a valid port number and a document root directory.
```bash
./http_daemon_server -p <port> -d <document_root> [-a]
```
### Options:
* `-p`: Port to listen on (0- 65535)
* `-d`: Document root directory to serve files from 
* `-a`: *(optional)* Run the server as a background daemon process 

### Examples:
1. **Run the server on port 8080 serving files from the ./test_site directory:**
```bash
./http_daemon_server -p 8080 -d ./test_site
```
To gracefully stop the server running in the foreground, simply press `Ctrl+C`.

2. **Run the server in the background:**
```bash
./http_daemon_server -p 8080 -d ./test_site -a
```
To gracefully stop the server running in the background, run this command:
```bash
pkill -INT -f http_daemon_server
```

### Testing the Server

Once the server is running, you can easily test it using any standard web browser or a command-line HTTP client.

**Via Web Browser:**
Navigate to the local loopback address with the port you specified:
`http://localhost:8080` (or `http://127.0.0.1:8080`)

**Via Terminal:**
To see the raw HTTP response headers along with the content, open a new terminal tab and use `curl`:
```bash
curl --path-as-is http://localhost:8080 -v
```