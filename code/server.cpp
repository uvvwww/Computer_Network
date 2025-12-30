#include "server.hpp"
#include "ThreadPool.hpp"
#include "io_utils.hpp"
#include "conn_utils.hpp"
#include <fcntl.h>
#include <mutex> 
#include <set>

using namespace std;

// Global variable definitions
Server svr;
Request* requestP = nullptr;
int maxfd;
int num_conn = 1;
int alive_conn = 0;
std::set<int> group_fds; // Track FDs in the group chat
std::mutex group_mutex;  // Protect the set
std::mutex requests_mutex; // Global mutex to protect requestP and shared states

const unsigned char IAC_IP[3] = "\xff\xf4";

static void init_server(unsigned short port);
static void init_request(Request* reqP);
int accept_conn();

#include "conn_utils.hpp"

int registering(int conn_fd) {
    string username;
    string password;

    char buf[MAX_MSG_LEN*2];   
    // Read username
    if (read_or_close(&requestP[conn_fd]) < 0) return -1;
    username = requestP[conn_fd].buf;
    
    // Read password
    if (read_or_close(&requestP[conn_fd]) < 0) return -1;
    password = requestP[conn_fd].buf;

    // build backend registration information
    string request_msg = password + "\n";
    int file_fd;
    
    if ((file_fd = open(("user_"+username+".txt").c_str(), O_CREAT | O_EXCL | O_WRONLY, 0644)) < 0) {
        // [Fix] Use snprintf instead of sprintf
        std::snprintf(buf, sizeof(buf), "registration failed: user %s already exists\n", username.c_str());
        ssize_t sent;
        SAFE_WRITE(requestP[conn_fd].conn_fd, buf, std::strlen(buf), sent);
        if (sent < 0) return -1; 
        return 0;
    }
    
    ssize_t fw;
    SAFE_WRITE(file_fd, request_msg.c_str(), request_msg.size(), fw);
    if (fw < 0) return -1;

    close(file_fd);

    // [Fix] Use snprintf
    std::snprintf(buf, sizeof(buf), "registration successful for user: %s\n", username.c_str());
    ssize_t sent;
    SAFE_WRITE(requestP[conn_fd].conn_fd, buf, std::strlen(buf), sent);
    if (sent < 0) return -1;

    return 0;
}

int login(Request* reqP) {
    string username;
    string password;
    while(1) {
        if (read_or_close(reqP) < 0) return -1;
        username = reqP->buf;
        if (read_or_close(reqP) < 0) return -1;
        password = reqP->buf;

        bool success = true;
        char buf[MAX_MSG_LEN*2];
        
        // Check if user file exists
        ifstream user_file("user_" + username + ".txt");
        if (!user_file) {
            // [Fix] Use snprintf
            std::snprintf(buf, sizeof(buf), "login failed: user %s not found\n", username.c_str());
            ssize_t wr;
            SAFE_WRITE(reqP->conn_fd, buf, std::strlen(buf), wr);
            if (wr < 0) return -1;
            success = false;
            return 0; 
        }

        if (success) {
            // Read password from user file
            string stored_password;
            getline(user_file, stored_password);
            user_file.close();

            // Check if password matches
            if (stored_password != password) {
                // [Fix] Use snprintf
                std::snprintf(buf, sizeof(buf), "login failed: incorrect password for user %s\n", username.c_str());
                ssize_t wr;
                SAFE_WRITE(reqP->conn_fd, buf, std::strlen(buf), wr);
                if (wr < 0) return -1;
                success = false;
            }
        }

        if (success) {
            // [Thread Safety] Check if user is already logged in
            std::lock_guard<std::mutex> lock(requests_mutex);
            for (int i = 0; i < maxfd; ++i) {
                if (requestP[i].conn_fd != -1 && 
                    i != reqP->conn_fd && 
                    std::strcmp(requestP[i].username, username.c_str()) == 0) {
                    
                    // [Fix] Use snprintf
                    std::snprintf(buf, sizeof(buf), "login failed: the user is already logged in by another client\n");
                    ssize_t wr;
                    SAFE_WRITE(reqP->conn_fd, buf, std::strlen(buf), wr);
                    if (wr < 0) return -1;
                    success = false;
                    return 0;
                }
            }
        }

        if (success) break;
    }

    ssize_t wr;
    SAFE_WRITE(reqP->conn_fd, "login successful\n", 17, wr);
    if (wr < 0) return -1;


    int listening_port;
    while(1){
        if (read_or_close(reqP) < 0) return -1;
        listening_port = std::atoi(reqP->buf);

        if (listening_port < 0 || listening_port > 65535) {
            char range_err[128];
            std::snprintf(range_err, sizeof(range_err), "Error: Port %d is out of range (0-65535). Enter a valid port:\n", listening_port);
            ssize_t wr;
            SAFE_WRITE(reqP->conn_fd, range_err, std::strlen(range_err), wr);
            if (wr < 0) return -1;
            continue; // 重新等待輸入
        }

        bool repeated = false;
        
        // [Thread Safety] Check for duplicate port AND update user status atomically
        {
            std::lock_guard<std::mutex> lock(requests_mutex);
            
            for (int i = 0; i < maxfd; ++i) {
                if (requestP[i].conn_fd != -1 && i != reqP->conn_fd) {
                    if (requestP[i].listening_port == listening_port) {
                        repeated = true;
                        break;
                    }
                }
            }
            
            if (!repeated) {
                reqP->listening_port = listening_port;
                std::strcpy(reqP->username, username.c_str());
            }
        }

        if (repeated) {
            SAFE_WRITE(reqP->conn_fd, "listening port already in use, please enter a different port:\n", 63, wr);
            if (wr < 0) return -1;
        } else {
            SAFE_WRITE(reqP->conn_fd, "Listening port set successfully\n", 33, wr);
            if (wr < 0) return -1;
            break;
        }
    }

    // write it to username.txt
    string user_name = reqP->username;
    std::ofstream user_file("user_" + user_name + ".txt", std::ios::app);
    if (!user_file) return -1;
    user_file << listening_port << std::endl;
    user_file.close();
    
    return 0;
}

int logout(Request* reqP){
    string username = reqP->username;
    if (username.empty()) return -1; 

    string filename = "user_" + username + ".txt";

    std::ifstream infile(filename);
    if (!infile) return -1;
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(infile, line)) {
        lines.push_back(line);
    }
    infile.close();
    
    std::ofstream outfile(filename);
    if (!outfile) return -1;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i == 0 || lines[i] != std::to_string(reqP->listening_port)) {
            outfile << lines[i] << std::endl;
        }
    }
    outfile.close();

    // [Thread Safety] Clear global state
    {
        std::lock_guard<std::mutex> lock(requests_mutex);
        reqP->listening_port = -1;
        memset(reqP->username, 0, sizeof(reqP->username));
    }

    ssize_t wr;
    SAFE_WRITE(reqP->conn_fd, "logout successful\n", 19, wr);
    if (wr < 0) return -1;

    return 0;
}

int list(Request* reqP) {
    string user_list;
    
    // [Thread Safety] Lock while iterating global requests
    {
        std::lock_guard<std::mutex> lock(requests_mutex);
        for (int i = 0; i < maxfd; ++i) {
            if (requestP[i].conn_fd != -1 && 
                requestP[i].listening_port != -1 && 
                std::strlen(requestP[i].username) > 0) {
                user_list += string(requestP[i].username) + ", ";
            }
        }
    }
    
    if (user_list.size() >= 2)
        user_list.erase(user_list.size() - 2, 2);
    user_list += "\n";

    ssize_t wr;
    SAFE_WRITE(reqP->conn_fd, user_list.c_str(), user_list.size(), wr);
    if (wr < 0) return -1;

    return 0;
}

int example_usage(Request* reqP) {
    int ret;
    int wr;
    
    while (1) {
        ret = read_or_close(reqP); 
        if (ret < 0) return -1;
        
        if (strcmp(reqP->buf, "quit") == 0) {
            break;
        }

        char* buffer = reqP->buf;
        SAFE_WRITE(reqP->conn_fd, buffer, ret, wr); 
        if (wr < 0) return -1;
    }
    
    return 0;
}

int get_ip(Request* reqP) {
    if (read_or_close(reqP) < 0) return -1;
    string target_user = reqP->buf;
    
    if (!target_user.empty() && target_user.back() == '\n') {
        target_user.pop_back();
    }

    string result = "NOT_FOUND";
    
    // [Thread Safety] Lock while searching
    {
        std::lock_guard<std::mutex> lock(requests_mutex);
        for (int i = 0; i < maxfd; ++i) {
            if (requestP[i].conn_fd != -1 && 
                requestP[i].listening_port != -1 && 
                std::strcmp(requestP[i].username, target_user.c_str()) == 0) {
                
                result = string(requestP[i].host) + ":" + std::to_string(requestP[i].listening_port);
                break;
            }
        }
    }
    
    result += "\n";
    ssize_t wr;
    SAFE_WRITE(reqP->conn_fd, result.c_str(), result.size(), wr);
    if (wr < 0) return -1;
    
    return 0;
}

int group_chat_handler(Request* reqP) {
    // 1. 加入群組
    {
        std::lock_guard<std::mutex> lock(group_mutex);
        group_fds.insert(reqP->conn_fd);
    }

    // 通知其他人
    string enter_msg = "User " + string(reqP->username) + " has entered the group chat.\n";
    {
        std::lock_guard<std::mutex> lock(group_mutex);
        for (int fd : group_fds) {
            if (fd != reqP->conn_fd) {
                ssize_t sent;
                SAFE_WRITE(fd, enter_msg.data(), enter_msg.size(), sent);
            }
        }
    }
    
    // 歡迎訊息
    string welcome = "You have entered the group chat. Type 'exit' to leave.\n";
    ssize_t wr;
    SAFE_WRITE(reqP->conn_fd, welcome.data(), welcome.size(), wr);

    // 2. 聊天迴圈
    while (true) {
        if (read_or_close(reqP) < 0) break; 
        
        string raw_msg(reqP->buf);
        if (raw_msg.find("exit") == 0) {
            break;
        }

        while (!raw_msg.empty() && (raw_msg.back() == '\n' || raw_msg.back() == '\r')) {
            raw_msg.pop_back();
        }
        
        if (raw_msg.empty()) continue;

        // 3. 廣播 (Relay)
        // 格式: [Group] Username: <EncryptedPayload>
        string prefix = "[Group] " + string(reqP->username) + ": ";
        string packet = prefix + raw_msg + "\n";
        
        {
            std::lock_guard<std::mutex> lock(group_mutex);
            for (int fd : group_fds) {
                if (fd != reqP->conn_fd) { // 不回傳給自己
                    ssize_t sent;
                    SAFE_WRITE(fd, packet.data(), packet.size(), sent);
                }
            }
        }
        memset(reqP->buf, 0, sizeof(reqP->buf));
        reqP->buf_len = 0;
    }

    // 4. 移除使用者
    {
        std::lock_guard<std::mutex> lock(group_mutex);
        group_fds.erase(reqP->conn_fd);
    }
    
    string leave_msg = "User " + string(reqP->username) + " has left the group chat.\n";
    {
        std::lock_guard<std::mutex> lock(group_mutex);
        for (int fd : group_fds) {
            ssize_t sent;
            SAFE_WRITE(fd, leave_msg.data(), leave_msg.size(), sent);
        }
    }
    
    SAFE_WRITE(reqP->conn_fd, "exited group chat\n", 18, wr);
    return 0;
}

void thread_handle_connection(int conn_fd) {
    while (1) {
        if (read_or_close(&requestP[conn_fd]) < 0) return;

        string request_msg(requestP[conn_fd].buf);
        if (!request_msg.empty() && request_msg.back() == '\n') request_msg.pop_back();

        if (request_msg == "register") {
            if (registering(conn_fd) < 0) return;
        } else if (request_msg == "login") {
            if (login(&requestP[conn_fd]) < 0) return;
        } else if (request_msg == "logout") {
            if (logout(&requestP[conn_fd]) < 0) return;
        } else if (request_msg == "list") {
            if (list(&requestP[conn_fd]) < 0) return;
        } else if (request_msg == "get-ip") { 
            if (get_ip(&requestP[conn_fd]) < 0) return;
        } else if (request_msg == "group-chat") {
             if (group_chat_handler(&requestP[conn_fd]) < 0) return;
        } else {
            if (example_usage(&requestP[conn_fd]) < 0) return;
        }
        
        std::memset(requestP[conn_fd].buf, 0, sizeof(requestP[conn_fd].buf));
        requestP[conn_fd].buf_len = 0;
    }
    return;
}

int main(int argc, char** argv) {

    if (argc != 2) {
        std::fprintf(stderr, "usage: %s [port]\n", argv[0]);
        std::exit(1);
    }

    int conn_fd;
    init_server(static_cast<unsigned short>(std::atoi(argv[1])));
    std::fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);
    int num_threads = 10;
    ThreadPool pool(num_threads);

    while (1) {
        conn_fd = accept_conn();
        if (conn_fd < 0)
            continue;
        
        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        if (getpeername(conn_fd, reinterpret_cast<struct sockaddr*>(&peer), &peer_len) == 0) {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &peer.sin_addr, client_ip, sizeof(client_ip));
            int client_port = ntohs(peer.sin_port);
            std::fprintf(stderr, "Client connected from IP %s and port %d\n", client_ip, client_port);
        } else {
            ERR_EXIT("getpeername");
        }
        
        pool.enqueue([=]{ thread_handle_connection(conn_fd); });
    }      

    delete[] requestP;
    close(svr.listen_fd);

    return 0;
}

int accept_conn() {

    struct sockaddr_in cliaddr;
    socklen_t clilen;
    int conn_fd;

    clilen = sizeof(cliaddr);
    conn_fd = accept(svr.listen_fd, reinterpret_cast<struct sockaddr*>(&cliaddr), &clilen);
    if (conn_fd < 0) {
        if (errno == EINTR || errno == EAGAIN) return -1;
        if (errno == ENFILE) {
            std::fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
                return -1;
        }
        ERR_EXIT("accept");
    }
    
    // [Thread Safety] Lock when modifying global requestP table
    {
        std::lock_guard<std::mutex> lock(requests_mutex);
        requestP[conn_fd].conn_fd = conn_fd;
        std::strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
        std::fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);
        requestP[conn_fd].client_id = (svr.port * 1000) + num_conn;
        num_conn++;
    }
    
    return conn_fd;
}

static void init_request(Request* reqP) {
    reqP->conn_fd = -1;
    reqP->client_id = -1;
    reqP->listening_port = -1;
    reqP->buf_len = 0;
    reqP->remaining_time.tv_sec = 5;
    reqP->remaining_time.tv_usec = 0;
}

void free_request(Request* reqP) {
    std::printf("closing fd %d\n", reqP->conn_fd);
    {
        std::lock_guard<std::mutex> lock(requests_mutex);
        std::memset(reqP, 0, sizeof(Request));
        init_request(reqP);
    }
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    std::memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, static_cast<void*>(&tmp), sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    // [FIX] Use ::bind to avoid conflict with std::bind
    if (::bind(svr.listen_fd, reinterpret_cast<struct sockaddr*>(&servaddr), sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }

    maxfd = getdtablesize();
    requestP = new Request[maxfd];
    if (requestP == nullptr) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    std::strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}