#ifndef CONN_UTILS_HPP
#define CONN_UTILS_HPP

#include "server.hpp"
#include "io_utils.hpp"
#include <cstdio>

// Helper that wraps handle_read(Request*) and performs common error handling + cleanup.
// Returns 1 on success (data read), -1 on error/EOF (and the request is cleaned up).
static inline int read_or_close(Request* reqP) {
    size_t len = 0;
    int ret = handle_read_fd(reqP->conn_fd, reqP->buf, len);
    if (ret <= 0) {
        if (ret == 0) {
            std::fprintf(stderr, "client %d disconnected\n", reqP->client_id);
        } else {
            std::fprintf(stderr, "bad request from %s\n", reqP->host);
        }
        if (reqP->conn_fd >= 0) close(reqP->conn_fd);
        free_request(reqP);
        return -1;
    }
    return ret;
}

#endif