#ifndef __CLIENT_HPP
#define __CLIENT_HPP

// C++ headers
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>

// POSIX headers
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define ERR_EXIT(a) do { perror(a); std::exit(1); } while(0)

// Constants
// [Fix] 使用 #ifndef 防止與 io_utils.hpp 的定義衝突
#ifndef MAX_MSG_LEN
#define MAX_MSG_LEN 512
#endif

#endif