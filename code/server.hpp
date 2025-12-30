#ifndef __SERVER_HPP
#define __SERVER_HPP

// C++ headers
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>

// POSIX headers
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/time.h>
constexpr int MAX_MSG_LEN = 512;

#define ERR_EXIT(a) do { perror(a); std::exit(1); } while(0)
struct Server {
    char hostname[512];      // server's hostname
    unsigned short port;     // port to listen
    int listen_fd;           // fd to wait for a new connection
};

struct Request {
    char host[512];             // client's host
    int conn_fd;                // fd to talk with client
    int client_id;              // client's id
    int listening_port;        // client's listening port
    char buf[MAX_MSG_LEN];      // data sent by/to client
    size_t buf_len;             // bytes used by buf
    struct timeval remaining_time; // connection remaining time

    char username[512];          // client's username (changed)
};

// Global variables
extern Server svr;                           // server
extern Request* requestP;                    // point to a list of requests
extern int maxfd;                            // size of open file descriptor table, size of request list
extern int num_conn;
extern int alive_conn;

// Helpers (some are implemented in server.cpp)
void free_request(Request* reqP);

#endif
