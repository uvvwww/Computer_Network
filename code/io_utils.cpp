#include "io_utils.hpp" // 必須引入 hpp 以便連結宣告與實作
#include <cstring>
#include <cstdio>
#include <iostream>

// [關鍵修正] 你的程式碼用到了 IAC_IP_LOCAL，必須在這裡定義
// 這是 Telnet 的 Interrupt Process (Ctrl+C) 指令: 0xFF 0xF4
const unsigned char IAC_IP_LOCAL[] = "\xff\xf4";

int handle_read_fd(int fd, char* out_buf, size_t& out_len) {
    int r;
    char buf[MAX_MSG_LEN];
    size_t len = 0;

    std::memset(buf, 0, sizeof(buf));

    // safe_read 是 static inline，因為 include 了 io_utils.hpp 所以這裡可以用
    r = safe_read(fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;

    char* p1 = std::strstr(buf, "\015\012"); // \r\n
    if (p1 == nullptr) {
        p1 = std::strstr(buf, "\012");   // \n
        if (p1 == nullptr) {
            if (!std::strncmp(buf, reinterpret_cast<const char*>(IAC_IP_LOCAL), 2)) {
                // Client presses ctrl+C, regard as disconnection
                std::fprintf(stderr, "Client presses ctrl+C....\n");
                return 0;
            }
            // No newline found, treat whole buffer as a line
            p1 = buf + r - 1;
        }
    }

    len = p1 - buf + 1;
    if (len > MAX_MSG_LEN - 1) len = MAX_MSG_LEN - 1;
    std::memmove(out_buf, buf, len);
    out_buf[len - 1] = '\0';
    out_len = len - 1;
    return 1;
}