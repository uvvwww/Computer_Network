// client.cpp
#include "crypto_utils.hpp"
#include "io_utils.hpp"
#include "client.hpp"

#include <thread>
#include <fstream>
#include <sys/stat.h> 
#include <atomic>
#include <arpa/inet.h> 
#include <sstream>
#include <vector>
#include <netdb.h>
#include <limits> 

using namespace std;

// Global variables
int pending_socket = -1;       
string pending_requester = "";
bool online;
int peer_socket = -1;
string my_username; 
unsigned char* session_key = nullptr; 
string current_peer_name = "Peer";    

string recv_until_pattern(int sock, const vector<string>& patterns) {
    string response = "";
    char tmp_buf[4096];
    
    while (true) {
        ssize_t n = recv(sock, tmp_buf, sizeof(tmp_buf) - 1, 0);
        if (n <= 0) {
            return ""; 
        }
        tmp_buf[n] = '\0';
        response += tmp_buf;
        
        for (const auto& pattern : patterns) {
            if (response.find(pattern) != string::npos) {
                return response;
            }
        }
    }
}

string to_hex(const string& input) {
    static const char* const lut = "0123456789ABCDEF";
    size_t len = input.length();
    string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i) {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}

string from_hex(const string& input) {
    if (input.length() & 1) return ""; // 長度必須是偶數
    string output;
    output.reserve(input.length() / 2);
    for (size_t i = 0; i < input.length(); i += 2) {
        string byteString = input.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), nullptr, 16);
        output.push_back(byte);
    }
    return output;
}

int build_connection() {
    char host[256];
    int port;

    std::printf("Enter server address: ");
    if (std::scanf("%255s", host) != 1) {
        std::fprintf(stderr, "ERROR: Invalid input\n");
        std::exit(1);
    }

    std::printf("Enter server port number: ");
    if (std::scanf("%d", &port) != 1) {
        std::fprintf(stderr, "ERROR: Invalid port number\n");
        std::exit(1);
    }
    
    int c;
    while ((c = getchar()) != '\n' && c != EOF);

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) ERR_EXIT("socket");

    struct hostent* server = gethostbyname(host);
    if (server == nullptr) {
        std::fprintf(stderr, "ERROR: no such host %s\n", host);
        std::exit(1);
    }

    struct sockaddr_in servaddr;
    std::memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    std::memcpy(&servaddr.sin_addr.s_addr, server->h_addr, server->h_length);
    servaddr.sin_port = htons(port);

    if (connect(sock_fd, reinterpret_cast<struct sockaddr*>(&servaddr), sizeof(servaddr)) < 0) {
        ERR_EXIT("connect");
    }

    std::fprintf(stderr, "Connected to %s:%d\n", host, port);

    sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock_fd, reinterpret_cast<struct sockaddr*>(&local_addr), &addr_len) == 0) {
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &local_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(local_addr.sin_port);
        std::fprintf(stderr, "Client is using IP %s and port %d\n", client_ip, client_port);
    } else {
        ERR_EXIT("getsockname");
    }

    return sock_fd;
}

void registering(int sock_fd) {
    char send_buf[MAX_MSG_LEN];
    char recv_buf[MAX_MSG_LEN];

    std::strcpy(send_buf, "register\n");
    ssize_t cmd_sent;
    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), cmd_sent);
    if (cmd_sent < 0) return;

    std::fprintf(stdout, "Enter username: ");
    if (std::fgets(send_buf, MAX_MSG_LEN, stdin) == nullptr) return;
    std::string username(send_buf);

    ssize_t sent;
    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), sent);
    if (sent < 0) return;

    std::fprintf(stdout, "Enter password: ");
    if (std::fgets(send_buf, MAX_MSG_LEN, stdin) == nullptr) return;

    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), sent);
    if (sent < 0) return;

    ssize_t received;
    SAFE_READ(sock_fd, recv_buf, sizeof(recv_buf) - 1, received);
    if (received < 0) return;

    std::string repeated_string = "registration failed: user " + username.substr(0, username.size()-1) + " already exists";
    if (std::strncmp(recv_buf, repeated_string.c_str(), repeated_string.size()) == 0) {
        std::fprintf(stderr, "%s\n", recv_buf);
        return;
    }

    std::fprintf(stderr, "Server response: %s\n", recv_buf);
    std::fflush(stderr);
}

void receive_handler(int sock_fd, std::atomic<bool>& chatting) {
    char recv_buf[4096]; 
    string pending_data = "";
    
    std::ofstream outfile;
    long file_total_size = 0;
    long file_received_size = 0;
    string current_filename = "";
    bool receiving_file = false;

    while (chatting) {
        ssize_t n = recv(sock_fd, recv_buf, sizeof(recv_buf) - 1, 0);
        if (n <= 0) {
            if (chatting) std::cout << "\n[" << current_peer_name << " disconnected]\n";
            chatting = false;
            break;
        }
        recv_buf[n] = '\0';
        pending_data += recv_buf;

        size_t pos = 0;
        while ((pos = pending_data.find('\n')) != string::npos) {
            string line = pending_data.substr(0, pos);
            pending_data.erase(0, pos + 1);

            if (line.empty()) continue;

            // 1. Hex 轉 Binary
            string enc_bin = from_hex(line);
            if (enc_bin.empty()) continue;

            // 2. 解密
            string decrypted_msg = aes_decrypt(enc_bin, session_key);
            if (decrypted_msg.empty()) continue;

            // 3. 處理邏輯
            if (receiving_file) {
                outfile.write(decrypted_msg.data(), decrypted_msg.size());
                file_received_size += decrypted_msg.size();

                if (file_received_size >= file_total_size) {
                    outfile.close();
                    receiving_file = false;
                    std::cout << "\n[System] File received: recv_" << current_filename << "\n";
                }
            } 
            else if (decrypted_msg.find("__FILE_START__|") == 0) {
                string content = decrypted_msg.substr(15);
                size_t delimiter_pos = content.find('|');
                
                if (delimiter_pos != string::npos) {
                    current_filename = content.substr(0, delimiter_pos);
                    try { file_total_size = stol(content.substr(delimiter_pos + 1)); } catch (...) { file_total_size = 0; }

                    if (file_total_size > 0) {
                        outfile.open("recv_" + current_filename, std::ios::binary);
                        if (outfile.is_open()) {
                            receiving_file = true;
                            file_received_size = 0;
                            std::cout << "\n[System] Incoming file: " << current_filename << " (" << file_total_size << " bytes)...\n";
                        }
                    }
                }
            } 
            else {
                std::cout << current_peer_name << ": " << decrypted_msg << std::endl; 
            }
        }
    }
}

void send_file_core(int sock, string filepath, unsigned char* key) {
    std::ifstream infile(filepath, std::ios::binary | std::ios::ate);
    if (!infile) {
        std::cout << "[Error] File not found: " << filepath << "\n";
        return;
    }

    long filesize = infile.tellg();
    infile.seekg(0, std::ios::beg);

    string filename = filepath;
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != string::npos) filename = filename.substr(last_slash + 1);

    std::cout << "[System] Sending file: " << filename << " (" << filesize << " bytes)...\n";

    // 1. Metadata (先加密 -> 再轉 Hex -> 加換行)
    string meta = "__FILE_START__|" + filename + "|" + std::to_string(filesize);
    string encrypted_meta = aes_encrypt(meta, key);
    string hex_meta = to_hex(encrypted_meta);
    hex_meta += "\n"; 

    ssize_t sent;
    SAFE_WRITE(sock, hex_meta.data(), hex_meta.size(), sent);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // 2. Content
    char buffer[2048]; 
    long total_sent = 0;
    
    while (total_sent < filesize) {
        infile.read(buffer, sizeof(buffer));
        std::streamsize bytes_read = infile.gcount();
        if (bytes_read <= 0) break;

        string chunk(buffer, bytes_read);
        string encrypted_chunk = aes_encrypt(chunk, key);
        
        string hex_chunk = to_hex(encrypted_chunk);
        hex_chunk += "\n"; 

        SAFE_WRITE(sock, hex_chunk.data(), hex_chunk.size(), sent);
        total_sent += bytes_read;
    }
    infile.close();
    std::cout << "\n[System] File sent successfully.\n";
}

void recv_file_core(int sock, unsigned char* key) {
    char recv_buf[4096]; 
    string pending_data = "";
    
    std::ofstream outfile;
    long file_total_size = 0;
    long file_received_size = 0;
    string current_filename = "";
    bool metadata_parsed = false;

    cout << "Ready to receive file...\n";

    while (true) {
        ssize_t n = recv(sock, recv_buf, sizeof(recv_buf)-1, 0);
        if (n <= 0) break;
        
        recv_buf[n] = '\0';
        pending_data += recv_buf;

        size_t pos = 0;
        while ((pos = pending_data.find('\n')) != string::npos) {
            string line = pending_data.substr(0, pos);
            pending_data.erase(0, pos + 1);

            if (line.empty()) continue;

            // 1. Hex Decode
            string enc_bin = from_hex(line);
            
            // 2. Decrypt
            string decrypted = aes_decrypt(enc_bin, key);
            if (decrypted.empty()) continue;

            if (!metadata_parsed) {
                if (decrypted.find("__FILE_START__|") == 0) {
                    string content = decrypted.substr(15);
                    size_t sep = content.find('|');
                    if (sep != string::npos) {
                        current_filename = content.substr(0, sep);
                        file_total_size = stol(content.substr(sep + 1));
                        
                        outfile.open("recv_" + current_filename, std::ios::binary);
                        metadata_parsed = true;
                        cout << "Receiving " << current_filename << " (" << file_total_size << " bytes)...\n";
                    }
                }
            } else {
                outfile.write(decrypted.data(), decrypted.size());
                file_received_size += decrypted.size();
                
                if (file_received_size >= file_total_size) {
                    cout << "\nFile received successfully: recv_" << current_filename << "\n";
                    outfile.close();
                    return; 
                }
            }
        }
    }
}

void start_p2p_file_transfer(int server_fd, string target_user, string filepath) {
    if (target_user == my_username) {
        std::cout << "Cannot send file to yourself.\n";
        return;
    }
    
    // 檢查檔案是否存在
    std::ifstream fcheck(filepath);
    if (!fcheck.good()) {
        std::cout << "File does not exist: " << filepath << "\n";
        return;
    }
    fcheck.close();

    // 1. Get IP
    char send_buf[MAX_MSG_LEN];
    char recv_buf[MAX_MSG_LEN];
    ssize_t sent, received;

    strcpy(send_buf, "get-ip\n");
    SAFE_WRITE(server_fd, send_buf, strlen(send_buf), sent);
    
    snprintf(send_buf, MAX_MSG_LEN, "%s\n", target_user.c_str());
    SAFE_WRITE(server_fd, send_buf, strlen(send_buf), sent);

    SAFE_READ(server_fd, recv_buf, sizeof(recv_buf) - 1, received);
    string resp(recv_buf);
    if (resp.find("NOT_FOUND") != string::npos) {
        std::cout << "User not found or offline.\n";
        return;
    }

    size_t colon_pos = resp.find(':');
    string ip_str = resp.substr(0, colon_pos);
    int port = stoi(resp.substr(colon_pos + 1));

    // 2. Connect
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip_str.c_str(), &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cout << "Connection Failed.\n";
        return;
    }

    // 3. Send FILE_REQ
    // Protocol: FILE_REQ <sender> <filename>
    string filename = filepath;
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != string::npos) filename = filename.substr(last_slash + 1);

    string req = "FILE_REQ " + my_username + " " + filename + "\n";
    SAFE_WRITE(sock, req.c_str(), req.size(), sent);

    cout << "Waiting for " << target_user << " to accept file transfer...\n";

    // 4. Wait for Acceptance & Handshake
    string response = recv_until_pattern(sock, {"REJECT", "ACC_FILE"});
    
    if (response.find("REJECT") != string::npos) {
        cout << "User rejected the file.\n";
        close(sock);
        return;
    }

    if (response.find("ACC_FILE") == 0) {
        // format: ACC_FILE <user> <PEM...>
        size_t pem_pos = response.find("-----BEGIN PUBLIC KEY-----");
        if (pem_pos == string::npos) { close(sock); return; }
        
        string peer_pem = response.substr(pem_pos);
        
        // Generate My Key
        EVP_PKEY* my_dh_key = generate_dh_key();
        string my_pub_pem = get_public_key_pem(my_dh_key);
        
        // Send My Key back
        SAFE_WRITE(sock, my_pub_pem.c_str(), my_pub_pem.size(), sent);

        // Derive Secret
        EVP_PKEY* peer_pub_key = load_public_key_pem(peer_pem);
        size_t secret_len;
        unsigned char* file_session_key = derive_secret(my_dh_key, peer_pub_key, secret_len);
        
        EVP_PKEY_free(my_dh_key);
        EVP_PKEY_free(peer_pub_key);

        if (file_session_key) {
            // 5. Start Transfer
            send_file_core(sock, filepath, file_session_key);
            OPENSSL_free(file_session_key);
        }
    }
    
    close(sock);
}

void enter_chat_room(int sock) {
    std::cout << "--- Secure Chat with " << current_peer_name << " ---\n";
    std::cout << "Commands: Type 'exit' to quit, 'sendfile <path>' to send a file.\n";
    
    std::atomic<bool> chatting(true);
    std::thread receiver(receive_handler, sock, std::ref(chatting));

    string input_line;
    while (chatting) {
        if (!std::getline(std::cin, input_line)) break;
        if (input_line.empty()) continue;

        if (input_line == "exit") {
            chatting = false;
            break;
        }
        
        if (input_line.substr(0, 9) == "sendfile ") {
            string path = input_line.substr(9);
            path.erase(0, path.find_first_not_of(" \t"));
            path.erase(path.find_last_not_of(" \t") + 1);
            if (!path.empty()) {
                send_file_core(sock, path, session_key);
            } else {
                std::cout << "Usage: sendfile <filepath>\n";
            }
            continue; 
        }

        // 一般訊息：加密 -> Hex -> 換行
        string encrypted = aes_encrypt(input_line, session_key);
        string hex_msg = to_hex(encrypted);
        hex_msg += "\n";

        ssize_t sent;
        SAFE_WRITE(sock, hex_msg.data(), hex_msg.size(), sent);
        
        if (sent < 0) {
            std::cout << "[Connection Error]\n";
            chatting = false;
        }
    }

    shutdown(sock, SHUT_RDWR);
    close(sock);
    if (receiver.joinable()) receiver.join();
    
    if (session_key) { OPENSSL_free(session_key); session_key = nullptr; }
    peer_socket = -1;
    std::cout << "--- Left Chat Room ---\n";
}

void listener_thread(int port) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Socket creation failed");
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind failed");
        close(listen_fd);
        return;
    }

    if (listen(listen_fd, 5) < 0) {
        perror("Listen failed");
        close(listen_fd);
        return;
    }

    while (online) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &len);
        if (conn_fd < 0) continue;

        string msg = recv_until_pattern(conn_fd, {"\n"});
        if (msg.empty()) { close(conn_fd); continue; }

        // --- Case 1: Chat Request ---
        if (msg.find("REQ") == 0) {
            string requester = msg.substr(4);
            requester.erase(requester.find_last_not_of("\n\r") + 1);
            // ... pending_socket 處理 ...
            if (pending_socket != -1) {
                 string reply = "REJECT";
                 send(conn_fd, reply.c_str(), reply.size(), 0);
                 close(conn_fd);
            } else {
                 pending_socket = conn_fd;
                 pending_requester = requester;
                 cout << "\n\n*** Incoming Chat Request from " << requester << " ***\n";
                 cout << "Type 'y' to accept, 'n' to reject: ";
                 cout.flush();
            }
        } 
        // --- Case 2: File Transfer Request  ---
        else if (msg.find("FILE_REQ") == 0) {
            // Format: FILE_REQ <sender> <filename>
            stringstream ss(msg);
            string cmd, sender, filename;
            ss >> cmd >> sender >> filename;
            
            cout << "\n\n*** Incoming File Transfer from " << sender << ": " << filename << " ***\n";
            
            // 由於 main loop 卡在 getline，我們使用 pending 變數通知主執行緒處理
            if (pending_socket != -1) {
                // Busy
                string reply = "REJECT";
                send(conn_fd, reply.c_str(), reply.size(), 0);
                close(conn_fd);
            } else {
                pending_socket = conn_fd;
                pending_requester = "FILE:" + sender + ":" + filename; // 特殊標記
                cout << "Do you want to accept? (y/n): ";
                cout.flush();
            }
        }
        else {
            close(conn_fd);
        }
    }
    close(listen_fd);
}

int start_listening(int listening_port) {
    std::thread(listener_thread, listening_port).detach();
    return listening_port;
}

void start_chat(int server_fd, string target_user) {
    if (target_user == my_username) {
        std::cout << "You cannot chat with yourself.\n";
        return;
    }
    char send_buf[MAX_MSG_LEN];
    char recv_buf[MAX_MSG_LEN];
    //std::cout << "[DEBUG] Requesting IP for " << target_user << " from server...\n";
    
    strcpy(send_buf, "get-ip\n");
    ssize_t sent;
    SAFE_WRITE(server_fd, send_buf, strlen(send_buf), sent);
    
    snprintf(send_buf, MAX_MSG_LEN, "%s\n", target_user.c_str());
    SAFE_WRITE(server_fd, send_buf, strlen(send_buf), sent);

    ssize_t received;
    SAFE_READ(server_fd, recv_buf, sizeof(recv_buf) - 1, received);
    
    string resp(recv_buf);
    //std::cout << "[DEBUG] Server replied: " << resp;
    if (resp.find("NOT_FOUND") != string::npos) {
        std::cout << "User not found or not online.\n";
        return;
    }

    size_t colon_pos = resp.find(':');
    string ip_str = resp.substr(0, colon_pos);
    int port = stoi(resp.substr(colon_pos + 1));
    //std::cout << "[DEBUG] Parsed Address: " << ip_str << ":" << port << "\n";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip_str.c_str(), &serv_addr.sin_addr) <= 0) {
        std::cout << "Invalid address/ Address not supported \n";
        return;
    }
    //std::cout << "[DEBUG] Connecting to peer...\n";
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cout << "Connection Failed \n";
        return;
    }
    //std::cout << "[DEBUG] Connected to peer! Sending Handshake...\n";

    string req = "REQ " + my_username + "\n";
    SAFE_WRITE(sock, req.c_str(), req.size(), sent);

    std::cout << "Waiting for " << target_user << " to accept...\n";

    string response = recv_until_pattern(sock, {"REJECT", "-----END PUBLIC KEY-----"});
    if (response.empty()) {
        std::cout << "Connection closed by peer.\n";
        close(sock);
        return;
    }

    if (response.find("REJECT") != string::npos) {
        std::cout << "User " << target_user << " rejected your chat request.\n";
        close(sock);
        return;
    }

    if (response.find("ACC") == 0) {
        stringstream ss(response);
        string cmd;
        ss >> cmd >> current_peer_name; 
        
        size_t pem_pos = response.find("-----BEGIN PUBLIC KEY-----");
        if (pem_pos == string::npos) {
            std::cerr << "Error: Peer accepted but sent no valid key.\n";
            close(sock);
            return;
        }
        
        string full_peer_pem = response.substr(pem_pos);

        EVP_PKEY* my_dh_key = generate_dh_key();
        if (!my_dh_key) {
            std::cerr << "Error: Failed to generate my DH key.\n";
            close(sock);
            return;
        }
        
        string my_pub_pem = get_public_key_pem(my_dh_key);
        SAFE_WRITE(sock, my_pub_pem.c_str(), my_pub_pem.size(), sent);

        EVP_PKEY* peer_pub_key = load_public_key_pem(full_peer_pem);
        if (peer_pub_key == nullptr) {
            std::cerr << "Error: Failed to parse peer's public key (OpenSSL error).\n";
            EVP_PKEY_free(my_dh_key);
            close(sock);
            return;
        }

        size_t secret_len;
        session_key = derive_secret(my_dh_key, peer_pub_key, secret_len);

        EVP_PKEY_free(my_dh_key);
        EVP_PKEY_free(peer_pub_key);

        if (session_key == nullptr) {
             std::cerr << "Error: Failed to derive shared secret.\n";
             close(sock);
             return;
        }

        std::cout << "Secure connection established with " << current_peer_name << "!\n";
        peer_socket = sock;
        enter_chat_room(sock);
    } else {
        std::cout << "Unknown response during handshake.\n";
        close(sock);
    }
}

int login(int sock_fd) {
    if (online){
        cout << "You are already logged in.\nPlease logout first to login again.\n";
        return 0;
    }
    char send_buf[MAX_MSG_LEN];
    char recv_buf[MAX_MSG_LEN];

    std::strcpy(send_buf, "login\n");
    ssize_t cmd_sent;
    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), cmd_sent);
    if (cmd_sent < 0) ERR_EXIT("write");
    string username;
    while(1){
        std::fprintf(stdout, "Enter username: ");
        memset(send_buf, 0, sizeof(send_buf));
        if (std::fgets(send_buf, MAX_MSG_LEN, stdin) == nullptr) return -1;
        username = send_buf;
        username.erase(username.find_last_not_of(" \n\r\t") + 1);

        ssize_t wret;
        SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), wret);
        if (wret < 0) ERR_EXIT("write");
        memset(send_buf, 0, sizeof(send_buf));

        std::fprintf(stdout, "Enter password: ");
        memset(send_buf, 0, sizeof(send_buf));
        if (std::fgets(send_buf, MAX_MSG_LEN, stdin) == nullptr) return -1;

        SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), wret);
        if (wret < 0) ERR_EXIT("write");
        memset(send_buf, 0, sizeof(send_buf));

        ssize_t received;
        SAFE_READ(sock_fd, recv_buf, sizeof(recv_buf) - 1, received);
        if (received < 0) return -1;

        string nouser_string = "login failed: user " + username + " not found";
        if (std::strncmp(recv_buf, nouser_string.c_str(), nouser_string.size()) == 0) { 
            std::cout << nouser_string << std::endl;
            return 0;
        }
        else if (std::strncmp(recv_buf, "login successful", 16) == 0) {
            break; 
        } else if (std::strncmp(recv_buf, "login failed: the user is already logged in by another client", 61) == 0) {
            std::cout << "You are already logged in by another client.\n";
            return 0;
        } else {
            std::fprintf(stderr, "%s\n", recv_buf);
        }
    }
    cout << "Login successful!\nplease enter your listening port:\n";

    int listening_port = 0;
    while (true) {
        std::cin >> listening_port;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        while (listening_port < 1024 || listening_port > 32767) {
            cout << "Invalid port number. Please enter a port between 1024 and 32767: \n";
            cin >> listening_port;
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        snprintf(send_buf, sizeof(send_buf), "%d\n", listening_port);
        ssize_t wr;
        SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), wr);
        if (wr < 0) {
            std::fprintf(stderr, "ERROR: Failed to send listening port to server\n");
            return -1;
        }
        ssize_t rd;
        SAFE_READ(sock_fd, recv_buf, sizeof(recv_buf) - 1, rd);
        if (rd <= 0) {
            std::fprintf(stderr, "ERROR: Failed to read server response or connection closed\n");
            return -1;
        }

        if (std::strncmp(recv_buf, "Listening port set successfully", 32) == 0) {
            break; 
        } else {
            cout << "Repeated listening port. Please enter a different port:\n";
        }
    }

    std::fprintf(stderr, "Server response: %s\n", recv_buf);
    std::fflush(stderr);
    start_listening(listening_port);  // 啟動 Listener

    online = true;
    my_username = username;
    return listening_port;
}

void login_bad_port(int sock_fd, string arg_username) {
    if (online) {
        cout << "You are already logged in.\n";
        return;
    }
    char send_buf[MAX_MSG_LEN];
    char recv_buf[MAX_MSG_LEN];

    // 1. 傳送 Login 指令
    std::strcpy(send_buf, "login\n");
    ssize_t cmd_sent;
    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), cmd_sent);
    usleep(100000); 

    // 2. 處理 Username
    string username = arg_username;
    if (username.empty()) {
        std::cout << "Enter username: ";
        if (!std::getline(std::cin, username)) return;
    }
    username.erase(username.find_last_not_of(" \n\r\t") + 1);
    
    snprintf(send_buf, MAX_MSG_LEN, "%s\n", username.c_str());
    ssize_t sent;
    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), sent);

    // 3. 輸入密碼
    std::cout << "Enter password: ";
    string password;
    if (!std::getline(std::cin, password)) return;
    snprintf(send_buf, MAX_MSG_LEN, "%s\n", password.c_str());
    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), sent);

    // 4. 檢查帳密驗證
    ssize_t received;
    SAFE_READ(sock_fd, recv_buf, sizeof(recv_buf) - 1, received);
    
    if (string(recv_buf).find("login successful") == string::npos) {
        std::cout << "Server response: " << recv_buf;
        return;
    }
    std::cout << recv_buf; 
    usleep(100000); 

    // 5. [測試] 傳送無效 Port
    std::cout << "[Test] Sending invalid port 99999 to server...\n";
    std::strcpy(send_buf, "99999\n");
    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), sent);

    // 6. 接收錯誤訊息
    SAFE_READ(sock_fd, recv_buf, sizeof(recv_buf) - 1, received);
    std::cout << "Server response: " << recv_buf << std::endl;

    // ==========================================
    // [FIX] 新增：讓使用者輸入正確 Port 以完成登入，同步狀態
    // ==========================================
    std::cout << "Now, please enter a valid port to complete login: ";
    int listening_port = 0;
    while (true) {
        std::cin >> listening_port;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        
        // 傳送輸入的 Port
        snprintf(send_buf, sizeof(send_buf), "%d\n", listening_port);
        SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), sent);

        // 讀取 Server 回應
        SAFE_READ(sock_fd, recv_buf, sizeof(recv_buf) - 1, received);
        
        // 判斷是否成功
        if (std::strncmp(recv_buf, "Listening port set successfully", 31) == 0) {
            std::cout << "Server response: " << recv_buf << "\n";
            break; 
        } else {
            // 可能是 Port 重複或無效，印出錯誤並重試
            std::cout << "Server response: " << recv_buf; 
        }
    }

    // 啟動 Listener 並更新 Client 狀態
    start_listening(listening_port);
    online = true;           // <--- 關鍵：更新狀態
    my_username = username;  // <--- 關鍵：更新名字
    std::cout << "Login fully completed. You are now online.\n";
}

int logout(int sock_fd) {
    if (!online) {
        std::fprintf(stderr, "You are not logged in.\n");
        return -1;
    }
    char send_buf[MAX_MSG_LEN];
    char recv_buf[MAX_MSG_LEN];

    std::strcpy(send_buf, "logout\n");
    ssize_t cmd_sent;
    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), cmd_sent);
    if (cmd_sent < 0) return -1;

    std::memset(recv_buf, 0, sizeof(recv_buf));
    ssize_t received;
    SAFE_READ(sock_fd, recv_buf, sizeof(recv_buf) - 1, received);
    if (received < 0) ERR_EXIT("read");
    else if (received == 0) { std::fprintf(stderr, "Server closed connection\n"); return -1; }

    std::fprintf(stderr, "Server response: %s\n", recv_buf);
    std::fflush(stderr);
    online = false;

    return 0;
}

void list(int sock_fd) {
    char send_buf[MAX_MSG_LEN];
    char recv_buf[MAX_MSG_LEN];

    std::strcpy(send_buf, "list\n");
    ssize_t cmd_sent;
    SAFE_WRITE(sock_fd, send_buf, std::strlen(send_buf), cmd_sent);
    if (cmd_sent < 0) ERR_EXIT("write");

    std::memset(recv_buf, 0, sizeof(recv_buf));
    ssize_t received;
    SAFE_READ(sock_fd, recv_buf, sizeof(recv_buf) - 1, received);
    if (received < 0) {
        ERR_EXIT("read");
    } else if (received == 0) {
        std::fprintf(stderr, "Server closed connection\n");
        return;
    }

    std::fprintf(stderr, "Online users:\n%s\n", recv_buf);
    std::fflush(stderr);
}

unsigned char* get_group_key() {
    string salt = "CS_NET_FINAL_PROJECT_2025_KEY"; 
    unsigned char* key = (unsigned char*)malloc(32); 
    // 簡單填滿 32 bytes
    for(int i=0; i<32; i++) {
        key[i] = (unsigned char)salt[i % salt.length()];
    }
    return key;
}

void group_receive_handler(int sock_fd, std::atomic<bool>& chatting, unsigned char* group_key) {
    char recv_buf[4096];
    
    while (chatting) {
        memset(recv_buf, 0, sizeof(recv_buf));
        ssize_t n = recv(sock_fd, recv_buf, sizeof(recv_buf) - 1, 0);
        if (n <= 0) {
            if (chatting) cout << "\n[System] Disconnected from server.\n";
            chatting = false;
            break;
        }

        string raw_msg(recv_buf, n);
        
        // 檢查是否為群組訊息
        string prefix = "[Group] ";
        size_t prefix_pos = raw_msg.find(prefix);
        
        if (prefix_pos != string::npos) {
            // 找到分隔符號 ": "
            // 格式: [Group] Username: <Cipher>
            size_t colon_pos = raw_msg.find(": ", prefix_pos + prefix.size());
            
            if (colon_pos != string::npos) {
                string sender = raw_msg.substr(prefix_pos + prefix.size(), colon_pos - (prefix_pos + prefix.size()));
                
                // 取得 Hex String
                string content_hex = raw_msg.substr(colon_pos + 2);
                
                // 去除尾端換行
                while (!content_hex.empty() && (content_hex.back() == '\n' || content_hex.back() == '\r')) {
                    content_hex.pop_back();
                }

                // 1. Hex 轉回 Binary
                string encrypted_bin = from_hex(content_hex);

                // 2. 解密
                string decrypted = aes_decrypt(encrypted_bin, group_key);
                
                if (decrypted.empty()) {
                     // cout << "[" << sender << "]: (Decryption Error)" << endl;
                } else {
                     cout << "[" << sender << "]: " << decrypted << endl;
                }
            } else {
                // 格式不對，直接印出
                cout << raw_msg;
            }
        } else {
            // 系統訊息 (如 "User joined")，直接印出
            cout << raw_msg; 
            if (raw_msg.find("exited group chat") != string::npos) {
                chatting = false;
            }
        }
    }
}

void enter_group_mode(int sock_fd) {
    char send_buf[MAX_MSG_LEN];
    // 傳送指令給 Server
    strcpy(send_buf, "group-chat\n");
    ssize_t sent;
    SAFE_WRITE(sock_fd, send_buf, strlen(send_buf), sent);

    cout << "--- Entering Group Chat (Relay Mode + Encrypted) ---\n";
    cout << "Type 'exit' to leave.\n";

    unsigned char* group_key = get_group_key();
    std::atomic<bool> chatting(true);
    std::thread receiver(group_receive_handler, sock_fd, std::ref(chatting), group_key);

    string input;
    while (chatting) {
        if (!getline(cin, input)) break;
        if (input.empty()) continue;

        if (input == "exit") {
            string exit_cmd = "exit\n";
            SAFE_WRITE(sock_fd, exit_cmd.data(), exit_cmd.size(), sent);
            chatting = false;
            break;
        }

        // 1. 加密 (變成二進位亂碼)
        string encrypted = aes_encrypt(input, group_key);
        
        // 2. [新增] 轉成 Hex String (變成安全純文字 0-9, A-F)
        string hex_msg = to_hex(encrypted);

        // 3. 加上換行符號
        hex_msg += "\n";
        
        // 4. 傳送
        SAFE_WRITE(sock_fd, hex_msg.data(), hex_msg.size(), sent);
    }

    if (receiver.joinable()) receiver.join();
    free(group_key);
    cout << "--- Left Group Chat ---\n";
}

int main() {
    int sock_fd = build_connection();
    if (sock_fd < 0) return 1;

    online = false;
    std::string user_input;
    
    while(true){
        std::cout << "register | login | logout | list |\n"
                  <<  "chat <user> | sendfile <user> <path> | group-chat:\n";
        
        if (!std::getline(std::cin, user_input)) break;
        if (user_input.empty()) continue;
        // --- 處理 Pending Request (Chat 或 File) ---
        if (pending_socket != -1) {
            if (user_input == "y" || user_input == "Y") {
                std::cout << "Accepting request from " << pending_requester << "...\n";
                int conn_fd = pending_socket;
                
                // 檢查是 Chat 還是 File
                if (pending_requester.find("FILE:") == 0) {
                    // === 處理檔案接收 (File Transfer) ===
                    // Parse: FILE:sender:filename
                    size_t first_colon = pending_requester.find(':');
                    size_t second_colon = pending_requester.find(':', first_colon + 1);
                    string sender = pending_requester.substr(first_colon + 1, second_colon - (first_colon + 1));

                    cout << "Starting file transfer with " << sender << "...\n";

                    // 1. Generate Key & Send ACC_FILE
                    EVP_PKEY* my_dh_key = generate_dh_key();
                    string my_pub_pem = get_public_key_pem(my_dh_key);
                    string reply = "ACC_FILE " + my_username + " " + my_pub_pem;
                    send(conn_fd, reply.c_str(), reply.size(), 0);

                    // 2. Receive Peer Key
                    string peer_pem = recv_until_pattern(conn_fd, {"-----END PUBLIC KEY-----"});
                    if (!peer_pem.empty()) {
                        EVP_PKEY* peer_pub_key = load_public_key_pem(peer_pem);
                        size_t secret_len;
                        unsigned char* f_key = derive_secret(my_dh_key, peer_pub_key, secret_len);
                        
                        EVP_PKEY_free(peer_pub_key);
                        EVP_PKEY_free(my_dh_key);
                        
                        // 3. Receive File
                        if (f_key) {
                            recv_file_core(conn_fd, f_key);
                            OPENSSL_free(f_key);
                        }
                    }
                    close(conn_fd); // 傳完即斷線
                    pending_socket = -1;
                    pending_requester = "";
                    continue; // 結束這一輪 loop

                } 
                // ===  處理一般聊天 (Chat) ===
                else {
                    // 1. Generate My Key
                    EVP_PKEY* my_dh_key = generate_dh_key();
                    string my_pub_pem = get_public_key_pem(my_dh_key);

                    // 2. Send Accept Message (ACC <username> <pubkey>)
                    string reply = "ACC " + my_username + " " + my_pub_pem;
                    send(conn_fd, reply.c_str(), reply.size(), 0);

                    // 3. Receive Peer's Key
                    string peer_pem = recv_until_pattern(conn_fd, {"-----END PUBLIC KEY-----"});
                    if (!peer_pem.empty()) {
                        EVP_PKEY* peer_pub_key = load_public_key_pem(peer_pem);
                        size_t secret_len;
                        session_key = derive_secret(my_dh_key, peer_pub_key, secret_len);
                        EVP_PKEY_free(peer_pub_key);
                        EVP_PKEY_free(my_dh_key);

                        // 4. Update Global State
                        current_peer_name = pending_requester;
                        peer_socket = conn_fd;
                        
                        pending_socket = -1;
                        pending_requester = "";
                        
                        // 5. Enter Chat Room
                        enter_chat_room(conn_fd);
                    } else {
                        // Key Exchange Failed
                        close(conn_fd);
                        pending_socket = -1;
                    }
                    continue; // 結束這一輪 loop
                }

            } else if (user_input == "n" || user_input == "N") {
                // Reject
                std::cout << "Rejected request from " << pending_requester << ".\n";
                string reply = "REJECT";
                send(pending_socket, reply.c_str(), reply.size(), 0);
                close(pending_socket);
                
                pending_socket = -1;
                pending_requester = "";
                continue; 
            }
        }

        // --- Command Parsing ---
        string command;
        string argument; // 用來存 arg1 (e.g. username)
        string argument2; // 用來存 arg2 (e.g. filepath)

        stringstream ss(user_input);
        ss >> command >> argument >> argument2;

        if (command == "sendfile") {
             // Usage: sendfile <username> <filepath>
            if (!online) {
                cout << "Please login first.\n";
            } else if (argument.empty() || argument2.empty()) {
                cout << "Usage: sendfile <username> <filepath>\n";
            } else {
                start_p2p_file_transfer(sock_fd, argument, argument2);
            }
        } else if (command == "register") {
            registering(sock_fd);
        } else if (command == "login") {
            login(sock_fd);
        } else if (command == "login_bad_port") {
            login_bad_port(sock_fd, argument);
        } else if (command == "logout") {
            logout(sock_fd);
        } else if (command == "list") {
            list(sock_fd);
        } else if (command == "group-chat") {
            if (!online) {
                cout << "Please login first.\n";
            } else {
                enter_group_mode(sock_fd);
            }
        } else if (command == "chat") {
            if (!online) {
                std::cout << "Please login first.\n";
                continue;
            }
            
            if (argument.empty()) {
                if (peer_socket != -1) {
                    enter_chat_room(peer_socket);
                } else {
                    std::cout << "Usage: chat <username>\n";
                }
            } else {
                start_chat(sock_fd, argument);
            }
        } 
    }
    close(sock_fd);
    return 0;
}