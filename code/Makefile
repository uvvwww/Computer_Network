# Compiler settings
CXX = g++
CXXFLAGS = -std=c++17 -Wall -g

# Libraries
LDFLAGS = -lssl -lcrypto -lpthread

# Detect OS to handle OpenSSL paths on macOS (Apple Silicon / Homebrew)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	# Path for Homebrew OpenSSL on macOS
	OPENSSL_DIR = /opt/homebrew/opt/openssl@3
	
	# Add Include and Linker paths
	CXXFLAGS += -I$(OPENSSL_DIR)/include
	LDFLAGS += -L$(OPENSSL_DIR)/lib
endif

# Targets
TARGETS = server client

# Object files definition
# Server needs: server logic + thread pool + io utils
SERVER_OBJS = server.o ThreadPool.o io_utils.o

# Client needs: client logic + io utils (crypto is header-only)
CLIENT_OBJS = client.o io_utils.o

# Default target
all: $(TARGETS)

# Link server
server: $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Link client
client: $(CLIENT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Compile source files into object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Helper to generate SSL certificates
cert:
	openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -days 365 -nodes -subj "/C=TW/ST=Taipei/L=Taipei/O=NTU/OU=CS/CN=localhost"

# Clean build files
clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean cert