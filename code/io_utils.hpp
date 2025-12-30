#ifndef IO_UTILS_HPP
#define IO_UTILS_HPP

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <string>
#include <sys/types.h>
#include <iostream>

// reuse MAX_MSG_LEN from server/client headers
#ifndef MAX_MSG_LEN
#define MAX_MSG_LEN 512
#endif

// Safe read/write helpers (retry on EINTR)
// 這些 static inline 函式保留在這裡沒問題
static inline ssize_t safe_read(int fd, void* buf, size_t count) {
    ssize_t n;
    do { n = read(fd, buf, count); } while (n < 0 && errno == EINTR);
    return n;
}

static inline ssize_t safe_write(int fd, const void* buf, size_t count) {
    ssize_t n;
    size_t written = 0;
    const char* p = static_cast<const char*>(buf);
    while (written < count) {
        do { n = write(fd, p + written, count - written); } while (n < 0 && errno == EINTR);
        if (n < 0) return n;
        written += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(written);
}

// ==========================================================
// [關鍵修正] 這裡必須要有宣告 (Prototype)，conn_utils.hpp 才看得到
// ==========================================================
int handle_read_fd(int fd, char* out_buf, size_t& out_len);


// Macros
#define SAFE_READ(fd, buf, size, ret) do { \
    size_t len = 0; \
    /* 注意：這裡要轉型成 char* 因為 handle_read_fd 簽名是 char* */ \
    ret = handle_read_fd(fd, (char*)(buf), len); \
    if (ret <= 0) { \
        if (ret == 0) { \
            std::fprintf(stderr, "client %d disconnected\n", fd); \
        } else { \
            std::fprintf(stderr, "bad request from fd=%d\n", fd); \
        } \
        if (fd >= 0) close(fd); \
    } \
} while (0);

#define SAFE_WRITE(fd, buf, size, ret) do { \
    ret = safe_write((fd), (buf), (size)); \
    if (ret < 0) { \
        std::fprintf(stderr, "error writing to fd=%d\n", fd); \
        if (fd >= 0) close(fd); \
    } \
} while (0);

#endif