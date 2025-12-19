# ============================================================================
# Chat Server & Client - Makefile
# ============================================================================
SHELL := /bin/bash

CC = gcc
CFLAGS = -Wall -Wextra -g -I. -Iserver -Icommon -Idatabase
LDFLAGS = -lpq -lssl -lcrypto

# PostgreSQL configuration
# Try pkg-config first, fallback to standard Linux paths
PG_CFLAGS = $(shell pkg-config --cflags libpq 2>/dev/null || echo "-I/usr/include/postgresql")
PG_LDFLAGS = $(shell pkg-config --libs libpq 2>/dev/null || echo "-lpq")

# OpenSSL configuration
SSL_CFLAGS = $(shell pkg-config --cflags openssl 2>/dev/null || echo "")
SSL_LDFLAGS = $(shell pkg-config --libs openssl 2>/dev/null || echo "-lssl -lcrypto")

# Combine flags
CFLAGS += $(PG_CFLAGS) $(SSL_CFLAGS)
LDFLAGS = $(PG_LDFLAGS) $(SSL_LDFLAGS)

# Source files
SERVER_SOURCES = server/server_main.c server/server.c server/auth.c server/friend.c server/message.c server/group.c database/database.c common/protocol.c common/router.c
SERVER_OBJECTS = $(SERVER_SOURCES:.c=.o)
SERVER_TARGET = chat_server

CLIENT_SOURCE = client/client.c common/protocol.c
CLIENT_TARGET = chat_client

DB_MAIN = main.c
DB_SOURCES = database/database.c
DB_OBJECTS = $(DB_MAIN:.c=.o) $(DB_SOURCES:.c=.o)
DB_TARGET = database/db_manager

# ============================================================================
# Main Targets
# ============================================================================

.PHONY: all clean help server client db

all: server client

# Build server
server: $(SERVER_TARGET)

$(SERVER_TARGET): $(SERVER_OBJECTS)
	@echo "Linking server..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Server compiled successfully: ./$(SERVER_TARGET)"

# Build client
client: $(CLIENT_TARGET)

$(CLIENT_TARGET): $(CLIENT_SOURCE)
	@echo "Compiling client..."
	$(CC) $(CFLAGS) -o $@ $^
	@echo "✓ Client compiled successfully: ./$(CLIENT_TARGET)"

# Build database manager
db: $(DB_TARGET)

$(DB_TARGET): $(DB_OBJECTS)
	@echo "Linking database manager..."
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✓ Database manager compiled: ./$(DB_TARGET)"

# ============================================================================
# Object Files
# ============================================================================

%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================================
# Run Commands
# ============================================================================

.PHONY: run-server run-client run-client-custom

# Run server (default port 8888)
run-server: server
	@echo "Starting server..."
	./$(SERVER_TARGET)

# Run server with custom port
# Usage: make run-server-port PORT=9999
run-server-port: server
	@echo "Starting server on port $(PORT)..."
	./$(SERVER_TARGET) $(PORT)

# Run client (connect to localhost:8888)
run-client: client
	@echo "Starting client..."
	./$(CLIENT_TARGET)

# Run client with custom host/port
# Usage: make run-client-custom HOST=192.168.1.100 PORT=9999
run-client-custom: client
	@echo "Starting client ($(HOST):$(PORT))..."
	./$(CLIENT_TARGET) $(HOST) $(PORT)

# ============================================================================
# Database Commands
# ============================================================================

.PHONY: create-tables drop-tables show-users show-friends show-groups show-messages sample-data reset-db

# Create all database tables
create-tables: db
	@echo "Creating database tables..."
	./$(DB_TARGET) create-tables

# Drop all tables
drop-tables: db
	@echo "WARNING: This will delete all data!"
	@read -p "Are you sure? [y/N] " -n 1 -r; \
	echo; \
	if [[ $$REPLY =~ ^[Yy]$$ ]]; then \
		./$(DB_TARGET) drop-tables; \
	fi

# Show database contents
show-users: db
	@./$(DB_TARGET) show-users

show-friends: db
	@./$(DB_TARGET) show-friends

show-groups: db
	@./$(DB_TARGET) show-groups

show-group-members: db
	@./$(DB_TARGET) show-group-members

show-messages: db
	@./$(DB_TARGET) show-messages

show-all: db
	@./$(DB_TARGET) show-users
	@./$(DB_TARGET) show-friends
	@./$(DB_TARGET) show-groups
	@./$(DB_TARGET) show-group-members
	@./$(DB_TARGET) show-messages

# Insert sample data
sample-data:
	@echo "Inserting sample data..."
	psql -U rin -d network -f database/sample_data.sql
	@echo "✓ Sample data inserted"

# Reset database (drop + create + sample data)
reset-db: drop-tables create-tables sample-data
	@echo "✓ Database reset complete"

# ============================================================================
# Testing
# ============================================================================

.PHONY: test test-python test-basic

# Run Python test suite
test-python:
	@echo "Running Python test suite..."
	python3 client/test_client.py

# Run Python interactive test
test-interactive:
	@echo "Starting interactive test client..."
	python3 client/test_client.py -i

# Basic netcat test
test-basic:
	@echo "Test commands:"
	@echo "  REGISTER testuser pass123"
	@echo "  LOGIN testuser pass123"
	@echo "  LOGOUT"
	@echo ""
	@nc localhost 8888

# ============================================================================
# Development
# ============================================================================

.PHONY: debug valgrind-server valgrind-client gdb-server gdb-client

# Compile with debug symbols
debug: CFLAGS += -DDEBUG -O0
debug: clean all
	@echo "✓ Built with debug symbols"

# Run server with valgrind
valgrind-server: server
	valgrind --leak-check=full --show-leak-kinds=all \
		--track-origins=yes --verbose \
		./$(SERVER_TARGET)

# Run client with valgrind
valgrind-client: client
	valgrind --leak-check=full --show-leak-kinds=all \
		--track-origins=yes --verbose \
		./$(CLIENT_TARGET)

# Debug server with gdb
gdb-server: server
	gdb ./$(SERVER_TARGET)

# Debug client with gdb
gdb-client: client
	gdb ./$(CLIENT_TARGET)

# ============================================================================
# Installation & Setup
# ============================================================================

.PHONY: install-deps setup-db check-deps

# Install system dependencies (Ubuntu/Debian)
install-deps:
	@echo "Installing dependencies..."
	sudo apt-get update
	sudo apt-get install -y \
		gcc make \
		postgresql postgresql-contrib \
		libpq-dev libssl-dev \
		python3 netcat
	@echo "✓ Dependencies installed"

# Setup PostgreSQL database
setup-db:
	@echo "Setting up PostgreSQL database..."
	@echo "Creating user and database..."
	sudo -u postgres psql -c "CREATE USER rin WITH PASSWORD 'admin';" 2>/dev/null || true
	sudo -u postgres psql -c "CREATE DATABASE network OWNER rin;" 2>/dev/null || true
	@echo "✓ Database setup complete"
	@$(MAKE) create-tables

# Check if dependencies are installed
check-deps:
	@echo "Checking dependencies..."
	@command -v gcc >/dev/null 2>&1 || { echo "✗ gcc not found"; exit 1; }
	@command -v psql >/dev/null 2>&1 || { echo "✗ postgresql not found"; exit 1; }
	@pkg-config --exists libpq || { echo "✗ libpq-dev not found"; exit 1; }
	@pkg-config --exists openssl || { echo "✗ libssl-dev not found"; exit 1; }
	@echo "✓ All dependencies installed"

# ============================================================================
# Cleanup
# ============================================================================

.PHONY: clean clean-server clean-client clean-db clean-all

# Clean all build files
clean:
	@echo "Cleaning build files..."
	rm -f $(SERVER_OBJECTS) $(CLIENT_TARGET) $(DB_OBJECTS)
	rm -f *.o
	@echo "✓ Cleaned"

# Clean server binary
clean-server:
	rm -f $(SERVER_TARGET) $(SERVER_OBJECTS)

# Clean client binary
clean-client:
	rm -f $(CLIENT_TARGET)

# Clean database manager
clean-db:
	rm -f $(DB_TARGET) $(DB_OBJECTS)

# Clean everything including binaries
clean-all: clean
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET) $(DB_TARGET)
	@echo "✓ All binaries removed"

# ============================================================================
# Help
# ============================================================================

.PHONY: help

help:
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║         Chat Server & Client - Makefile Help               ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "BUILD TARGETS:"
	@echo "  make all              - Build server and client"
	@echo "  make server           - Build server only"
	@echo "  make client           - Build client only"
	@echo "  make db               - Build database manager"
	@echo ""
	@echo "RUN COMMANDS:"
	@echo "  make run-server       - Run server (port 8888)"
	@echo "  make run-client       - Run client (localhost:8888)"
	@echo "  make run-server-port PORT=9999    - Run server on custom port"
	@echo "  make run-client-custom HOST=<ip> PORT=<port> - Connect to custom server"
	@echo ""
	@echo "DATABASE COMMANDS:"
	@echo "  make create-tables    - Create database schema"
	@echo "  make drop-tables      - Drop all tables (with confirmation)"
	@echo "  make show-users       - Display users table"
	@echo "  make show-friends     - Display friends table"
	@echo "  make show-groups      - Display groups table"
	@echo "  make show-messages    - Display messages table"
	@echo "  make show-all         - Display all tables"
	@echo "  make sample-data      - Insert sample data"
	@echo "  make reset-db         - Reset database (drop + create + sample)"
	@echo ""
	@echo "TESTING:"
	@echo "  make test-python      - Run Python test suite"
	@echo "  make test-interactive - Run interactive Python client"
	@echo "  make test-basic       - Test with netcat"
	@echo ""
	@echo "DEVELOPMENT:"
	@echo "  make debug            - Build with debug symbols"
	@echo "  make valgrind-server  - Run server with valgrind"
	@echo "  make valgrind-client  - Run client with valgrind"
	@echo "  make gdb-server       - Debug server with gdb"
	@echo "  make gdb-client       - Debug client with gdb"
	@echo ""
	@echo "SETUP:"
	@echo "  make install-deps     - Install system dependencies"
	@echo "  make setup-db         - Setup PostgreSQL database"
	@echo "  make check-deps       - Check if dependencies are installed"
	@echo ""
	@echo "CLEANUP:"
	@echo "  make clean            - Remove object files"
	@echo "  make clean-all        - Remove all build files and binaries"
	@echo ""
	@echo "EXAMPLES:"
	@echo "  make all && make create-tables    - Full build + setup"
	@echo "  make run-server                   - Start server"
	@echo "  make run-client                   - Start client (new terminal)"
	@echo "  make show-users                   - View registered users"
	@echo ""

# Default target
.DEFAULT_GOAL := help