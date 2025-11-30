# Chat Server & Client - Network Programming Project

## 📋 Overview

A TCP-based chat server and client implementation with:
- Stream processing for TCP packet fragmentation
- Multi-client socket I/O using `select()`
- User authentication with PostgreSQL database
- Menu-driven client interface

**Currently Implemented:** REGISTER, LOGIN, LOGOUT

---

## 📁 Project Structure

```
chat-server/
├── common/
│   ├── protocol.h          # Protocol definitions & status codes
│   └── protocol.c          # Stream buffer & message parsing
├── database/
│   ├── database.h          # Database interface
│   └── database.c          # PostgreSQL operations
├── server/
│   ├── server.h            # Server structures
│   ├── server.c            # Socket I/O with select()
│   ├── auth.c              # Authentication handlers
│   └── server_main.c       # Entry point
├── client/
│   └── client.c            # Menu-driven client
├── main.c                  # Database manager tool
├── Makefile                # Build system
├── sample_data.sql         # Sample data
├── test_client.py          # Python test suite
└── README.md               # This file
```

---

## 🚀 Quick Start

### 1. Install Dependencies

```bash
# Ubuntu/Debian
sudo apt-get install gcc make postgresql libpq-dev libssl-dev python3

# Or use makefile
make install-deps
```

### 2. Setup Database

```bash
# Create PostgreSQL database
make setup-db

# This will:
# - Create user 'rin' with password 'admin'
# - Create database 'network'
# - Create all tables
```

### 3. Build Everything

```bash
make all

# Or build individually:
make server    # Build chat_server
make client    # Build chat_client
make db        # Build db_manager
```

### 4. Run Server

```bash
# Terminal 1
make run-server

# Or custom port
./chat_server 9999
```

### 5. Run Client

```bash
# Terminal 2
make run-client

# Or custom server
./chat_client 192.168.1.100 8888
```

---

## 💻 Usage

### Client Menu Navigation

```
========================================
           CHAT CLIENT MENU             
========================================
1. Authentication
2. Friend Management (Not implemented)
3. Send Message (Not implemented)
4. Group Chat (Not implemented)
5. Exit
========================================
Your choice: 1

=== AUTHENTICATION ===
1. Register
2. Login
3. Logout
4. Back to main menu
======================
Your choice: 1

--- REGISTER ---
Enter username: alice
Enter password: password123
[Server] 101 Registration successful for alice
```

### Example Session

```bash
# Start server
$ make run-server

# In another terminal, start client
$ make run-client

========================================
       Chat Client - Network Project
========================================
Connected to server 127.0.0.1:8888
[Server] 100 Welcome to chat server

# Choose option 1 (Authentication)
Your choice: 1

# Register new user
Your choice: 1
Enter username: alice
Enter password: pass123
[Server] 101 Registration successful for alice

# Login
Your choice: 2
Enter username: alice
Enter password: pass123
[Server] 102 Welcome alice

# Logout
Your choice: 3
[Server] 103 Goodbye alice

# Back to main menu
Your choice: 4

# Exit
Your choice: 5
Closing connection...
```

---

## 📡 Protocol Specification

### Message Format
```
Client → Server:  COMMAND <param1> <param2> ...\r\n
Server → Client:  <STATUS_CODE> <MESSAGE>\r\n
```

### Implemented Commands

| Command | Format | Description |
|---------|--------|-------------|
| REGISTER | `REGISTER <username> <password>` | Register new account |
| LOGIN | `LOGIN <username> <password>` | Login to account |
| LOGOUT | `LOGOUT` | Logout from account |

### Status Codes

**Success (1xx):**
- `100` - Welcome message
- `101` - Registration successful
- `102` - Login successful
- `103` - Logout successful

**Client Errors (2xx):**
- `201` - Username already exists
- `202` - Wrong password

**Auth Errors (3xx):**
- `301` - Invalid username (3-50 chars, alphanumeric + underscore)
- `302` - Invalid password (6-100 chars)
- `303` - User not found
- `304` - Already logged in
- `305` - Not logged in

**Server Errors (4xx-5xx):**
- `400` - Database error
- `500` - Undefined error

---

## 🗄️ Database Schema

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password_hash VARCHAR(128) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    is_online BOOLEAN DEFAULT FALSE
);

CREATE TABLE friends (
    id SERIAL PRIMARY KEY,
    user_id INTEGER REFERENCES users(id),
    friend_id INTEGER REFERENCES users(id),
    status VARCHAR(20) DEFAULT 'pending',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, friend_id)
);

CREATE TABLE groups (
    id SERIAL PRIMARY KEY,
    group_name VARCHAR(100) NOT NULL,
    creator_id INTEGER REFERENCES users(id),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE group_members (
    id SERIAL PRIMARY KEY,
    group_id INTEGER REFERENCES groups(id),
    user_id INTEGER REFERENCES users(id),
    role VARCHAR(20) DEFAULT 'member',
    joined_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(group_id, user_id)
);

CREATE TABLE messages (
    id SERIAL PRIMARY KEY,
    sender_id INTEGER REFERENCES users(id),
    receiver_id INTEGER REFERENCES users(id),
    group_id INTEGER REFERENCES groups(id),
    content TEXT NOT NULL,
    is_delivered BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

### Database Management

```bash
# View data
make show-users           # Display users
make show-friends         # Display friends
make show-groups          # Display groups
make show-messages        # Display messages
make show-all             # Display all tables

# Manage database
make create-tables        # Create schema
make drop-tables          # Drop all tables (with confirmation)
make sample-data          # Insert sample data
make reset-db             # Reset everything
```

---

## 🧪 Testing

### Python Test Suite

```bash
# Automated tests
make test-python

# Interactive mode
make test-interactive
```

### Manual Testing

```bash
# Test with netcat
nc localhost 8888

REGISTER testuser pass123
LOGIN testuser pass123
LOGOUT
```

### Test Multiple Clients

```bash
# Terminal 1: Server
make run-server

# Terminal 2: Client A
./chat_client 127.0.0.1 8888

# Terminal 3: Client B
./chat_client 127.0.0.1 8888

# Both clients can register/login simultaneously
```

---

## 🔧 Build System

### Makefile Targets

**Build:**
```bash
make all              # Build everything
make server           # Build server only
make client           # Build client only
make db               # Build database manager
```

**Run:**
```bash
make run-server       # Run server (port 8888)
make run-client       # Run client (localhost:8888)
make run-server-port PORT=9999
make run-client-custom HOST=192.168.1.100 PORT=9999
```

**Database:**
```bash
make create-tables    # Create tables
make drop-tables      # Drop tables
make show-users       # View users
make show-all         # View all data
make reset-db         # Reset database
```

**Cleanup:**
```bash
make clean            # Remove object files
make clean-all        # Remove all binaries
```

**Help:**
```bash
make help            # Show all commands
make                 # Show help (default)
```

---

## 🏗️ Architecture

### Stream Processing (Task 1)

```
TCP Socket → Buffer → Find "\r\n" → Extract Message → Process
                ↑                         ↓
                └─────── Leftover ────────┘
```

**Implementation:**
- `stream_buffer_create()` - Initialize buffer
- `stream_buffer_append()` - Add received data
- `stream_buffer_extract_message()` - Extract complete message
- Handles TCP fragmentation automatically

### Socket I/O (Task 2)

```
Server Loop:
  select() on all sockets
     ↓
  listen_fd ready? → Accept new client
     ↓
  client_fd ready? → Receive data → Process message
```

**Implementation:**
- `select()` multiplexing for multiple clients
- Non-blocking I/O
- Max 100 concurrent clients
- Graceful disconnect handling

### Authentication (Task 3)

```
Client Request → Parse Command → Validate → Database → Response
                                     ↓
                              SHA256 Hash Password
```

**Implementation:**
- Password hashing with SHA256
- Username validation (3-50 chars, alphanumeric + `_`)
- Password validation (6-100 chars)
- Session management
- Online status tracking

---

## 🔒 Security Notes

⚠️ **This is an educational project. For production use:**

- Upgrade SHA256 to bcrypt/argon2 for passwords
- Implement SQL injection protection (parameterized queries)
- Add TLS/SSL encryption
- Implement rate limiting
- Add session tokens instead of username-based auth
- Add input sanitization

---

## 🐛 Troubleshooting

### Database Connection Failed

```bash
# Check PostgreSQL is running
sudo service postgresql status

# Start if not running
sudo service postgresql start

# Check credentials in database/database.h
#define PG_USER "rin"
#define PG_PASS "admin"
#define PG_DBNAME "network"
```

### Port Already in Use

```bash
# Find process
sudo lsof -i :8888

# Kill it
sudo kill -9 <PID>
```

### Compilation Errors

```bash
# Check dependencies
make check-deps

# Install missing dependencies
make install-deps
```

### Client Can't Connect

```bash
# Check server is running
ps aux | grep chat_server

# Check firewall
sudo ufw status

# Test with netcat
nc localhost 8888
```

---

## 📊 Performance

- **Max clients:** 100 (configurable via `MAX_CLIENTS`)
- **Max message size:** 4096 bytes
- **I/O model:** `select()` (suitable for < 1000 clients)
- **Database:** PostgreSQL with connection pooling ready

### Optimization Tips

For > 1000 clients:
- Switch to `epoll()` on Linux
- Implement thread pool
- Add connection pooling
- Use Redis for session cache

---

## 🔮 Future Features (Not Yet Implemented)

- Friend management (send/accept/remove)
- Private messaging
- Group chat (create/join/invite/kick)
- Offline message delivery
- Message history
- File transfer
- Voice/video chat

---

## 📝 Developer Notes

### Adding New Commands

1. Add command type to `common/protocol.h`:
```c
typedef enum {
    CMD_REGISTER,
    CMD_LOGIN,
    CMD_YOUR_COMMAND,  // Add here
    CMD_UNKNOWN
} CommandType;
```

2. Add parser case in `common/protocol.c`:
```c
if (strcmp(cmd_str, "YOUR_COMMAND") == 0) return CMD_YOUR_COMMAND;
```

3. Implement handler in `server/auth.c` or create new file:
```c
void handle_your_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    // Your implementation
}
```

4. Add to router in `server/auth.c`:
```c
case CMD_YOUR_COMMAND:
    handle_your_command(server, client, cmd);
    break;
```

5. Update client menu in `client/client.c`

---

## 📚 References

- [TCP Socket Programming](https://beej.us/guide/bgnet/)
- [PostgreSQL C API](https://www.postgresql.org/docs/current/libpq.html)
- [select() Documentation](https://man7.org/linux/man-pages/man2/select.2.html)

---

## 📄 License

Educational project for Network Programming course.

---

**Built with love for learning network programming**