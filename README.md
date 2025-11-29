# Chat Server - Tasks 1, 2, 3 Implementation

## 📋 Tổng Quan

Implementation của 3 task đầu tiên cho **Người A** trong dự án chat server:

### ✅ Task 1: Xử lý truyền dòng (Stream Processing) - 1 điểm
- Xử lý phân mảnh gói tin TCP
- Ghép buffer với delimiter `\r\n`
- Build protocol message

### ✅ Task 2: Cài đặt Socket I/O trên Server - 2 điểm
- Sử dụng `select()` để xử lý multiple clients
- Accept connections, handle read/write
- Non-blocking I/O multiplexing

### ✅ Task 3: Đăng ký & Quản lý tài khoản - 2 điểm
- Lưu user vào database PostgreSQL
- Kiểm tra trùng tên
- Hash mật khẩu bằng SHA256
- Validate username/password

---

## 🏗️ Kiến Trúc Tổng Quan

```
┌─────────────────────────────────────────────┐
│            Chat Server Architecture         │
├─────────────────────────────────────────────┤
│                                             │
│  [Client 1] [Client 2] ... [Client N]       │
│       │         │              │            │
│       └─────────┴──────────────┘            │
│                 │                           │
│                 ▼                           │
│       ┌──────────────────┐                  │
│       │  Socket Layer    │ ◄── Task 2       │
│       │  (select/epoll)  │                  │
│       └──────────────────┘                  │
│                 │                           │
│                 ▼                           │
│       ┌──────────────────┐                  │
│       │ Stream Processor │ ◄── Task 1       │
│       │  (Buffer + \r\n) │                  │
│       └──────────────────┘                  │
│                 │                           │
│                 ▼                           │
│       ┌──────────────────┐                  │
│       │ Protocol Parser  │                  │
│       │  (Command Route) │                  │
│       └──────────────────┘                  │
│                 │                           │
│        ┌────────┴────────┐                  │
│        ▼                 ▼                  │
│  ┌──────────┐      ┌──────────┐             │
│  │   Auth   │      │  Other   │             │
│  │ Handlers │      │ Commands │             │
│  └──────────┘      └──────────┘             │
│       │ ◄── Task 3                          │
│       ▼                                     │
│  ┌──────────┐                               │
│  │PostgreSQL│                               │
│  │ Database │                               │
│  └──────────┘                               │
└─────────────────────────────────────────────┘
```

---

## 📁 Cấu Trúc File & Vai Trò

```
chat-server/
├── protocol.h              # Protocol definitions & constants
├── protocol.c              # Task 1: Stream processing & parsing
├── server.h                # Server structure definitions
├── server.c                # Task 2: Socket I/O & networking
├── auth.c                  # Task 3: Registration & authentication
├── database.h              # Database interface
├── database.c              # Database implementation
├── server_main.c           # Main entry point
├── Makefile                # Build system
├── test_client.py          # Python test client
├── sample_data.sql         # Sample data for testing
└── README.md               # This file
```

### Chi tiết các module:

#### 📄 **server_main.c** - Entry Point
- `main()`: Parse command line, khởi tạo server
- `signal_handler()`: Xử lý Ctrl+C để shutdown gracefully

#### 🌐 **server.c** - Networking Layer (Task 2)
- `server_create()`: Tạo listen socket, bind, listen
- `server_run()`: Main event loop với `select()`
- `server_accept_connection()`: Accept client mới
- `server_receive_data()`: Nhận data từ client
- `server_send_response()`: Gửi response về client
- `client_session_create()`: Quản lý session từng client

#### 📦 **protocol.c** - Stream Processing & Parsing (Task 1)
- `stream_buffer_create()`: Tạo buffer cho client
- `stream_buffer_append()`: Ghép data vào buffer
- `stream_buffer_extract_message()`: Tách message theo `\r\n`
- `parse_protocol_message()`: Parse command thành struct
- `validate_username()`, `validate_password()`: Validate input
- `build_response()`: Build response theo protocol

#### 🔐 **auth.c** - Business Logic (Task 3)
- `server_handle_client_message()`: Route command đến handler
- `handle_register_command()`: Xử lý đăng ký
- `handle_login_command()`: Xử lý đăng nhập
- `handle_logout_command()`: Xử lý đăng xuất
- `hash_password()`: SHA256 hashing
- `user_exists()`: Kiểm tra user đã tồn tại

#### 💾 **database.c** - Data Access Layer
- `connect_to_database()`: Kết nối PostgreSQL
- `execute_query()`: Execute INSERT/UPDATE/DELETE
- `execute_query_with_result()`: Execute SELECT
- `create_all_tables()`: Tạo schema database

---

## 🔄 Luồng Hoạt Động Chi Tiết

### 1️⃣ Khởi động Server

```
server_main.c:main()
    ↓
server.c:server_create(port)
    ↓ socket() → bind() → listen()
    ↓
database.c:connect_to_database()
    ↓ Kết nối PostgreSQL
    ↓
server.c:server_run()
    ↓ Vào event loop
```

### 2️⃣ Accept Client Mới

```
server.c:server_run()
    ↓ select() phát hiện activity trên listen_fd
    ↓
server.c:server_accept_connection()
    ↓ accept() → client_fd
    ↓
server.c:server_add_client(client_fd)
    ↓
server.c:client_session_create(client_fd)
    ↓
protocol.c:stream_buffer_create()
    ↓ Tạo buffer riêng cho client này
```

### 3️⃣ Nhận & Xử Lý Data

```
server.c:server_run()
    ↓ select() phát hiện client_fd có data
    ↓
server.c:server_receive_data(client)
    ↓ recv() nhận raw bytes
    ↓
protocol.c:stream_buffer_append(buffer, data)
    ↓ Append vào buffer
    ↓
protocol.c:stream_buffer_extract_message(buffer)
    ↓ Tìm "\r\n" và tách message
    ↓ Có message hoàn chỉnh?
    ↓ YES
    ↓
auth.c:server_handle_client_message(server, client, message)
    ↓
protocol.c:parse_protocol_message(message)
    ↓ Parse thành ParsedCommand struct
    ↓
    Switch theo cmd_type:
    ├─→ auth.c:handle_register_command()
    ├─→ auth.c:handle_login_command()
    └─→ auth.c:handle_logout_command()
```

### 4️⃣ Xử Lý REGISTER Command

```
Client gửi: "REGISTER alice pass123\r\n"
    ↓
auth.c:handle_register_command()
    ↓
    ┌─ Kiểm tra đã login? → Error 304
    ├─ Kiểm tra params < 2? → Error 500
    └─ OK, tiếp tục
    ↓
protocol.c:validate_username("alice")
    ↓ Check: 3-50 chars, alphanumeric + "_"
    ↓ OK
    ↓
protocol.c:validate_password("pass123")
    ↓ Check: 6-100 chars
    ↓ OK
    ↓
auth.c:user_exists(db_conn, "alice")
    ↓
database.c:execute_query_with_result()
    ↓ SQL: "SELECT COUNT(*) FROM users WHERE username='alice'"
    ↓ count = 0 (chưa tồn tại)
    ↓
auth.c:register_user(db_conn, "alice", "pass123")
    ↓
auth.c:hash_password("pass123", hash_output)
    ↓ SHA256 hash
    ↓ hash = "5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8"
    ↓
database.c:execute_query()
    ↓ SQL: "INSERT INTO users (username, password_hash, is_online) 
            VALUES ('alice', '5e88489...', FALSE)"
    ↓ Insert thành công
    ↓
protocol.c:build_response(101, "Registration successful for alice")
    ↓ response = "101 Registration successful for alice\r\n"
    ↓
server.c:server_send_response(client, response)
    ↓ send() qua socket
    ↓
Client nhận: "101 Registration successful for alice\r\n"
```

### 5️⃣ Xử Lý LOGIN Command

```
Client gửi: "LOGIN alice pass123\r\n"
    ↓
auth.c:handle_login_command()
    ↓
    ┌─ Kiểm tra đã login? → Error 304
    ├─ Kiểm tra user đang online ở session khác? → Error 304
    └─ OK, tiếp tục
    ↓
auth.c:verify_login(db_conn, "alice", "pass123")
    ↓
auth.c:hash_password("pass123", input_hash)
    ↓ input_hash = "5e884898..."
    ↓
database.c:execute_query_with_result()
    ↓ SQL: "SELECT id, password_hash FROM users WHERE username='alice'"
    ↓ stored_hash = "5e884898..." (từ DB)
    ↓
    So sánh: input_hash == stored_hash?
    ↓ YES → return user_id = 1
    ↓
auth.c:update_user_status(db_conn, user_id, 1)
    ↓ SQL: "UPDATE users SET is_online=TRUE WHERE id=1"
    ↓
    Lưu vào ClientSession:
    client->user_id = 1
    client->is_authenticated = 1
    strcpy(client->username, "alice")
    ↓
protocol.c:build_response(102, "Welcome alice")
    ↓ response = "102 Welcome alice\r\n"
    ↓
server.c:server_send_response(client, response)
    ↓
Client nhận: "102 Welcome alice\r\n"
```

### 6️⃣ Xử Lý LOGOUT Command

```
Client gửi: "LOGOUT\r\n"
    ↓
auth.c:handle_logout_command()
    ↓
    ┌─ Kiểm tra chưa login? → Error 305
    └─ OK, tiếp tục
    ↓
auth.c:update_user_status(db_conn, user_id, 0)
    ↓ SQL: "UPDATE users SET is_online=FALSE WHERE id=1"
    ↓
    Reset ClientSession:
    client->user_id = -1
    client->is_authenticated = 0
    memset(client->username, 0)
    ↓
protocol.c:build_response(103, "Goodbye alice")
    ↓ response = "103 Goodbye alice\r\n"
    ↓
server.c:server_send_response(client, response)
    ↓
Client nhận: "103 Goodbye alice\r\n"
```

---

## 🔧 Cài Đặt & Build

### Prerequisites

```bash
# Install dependencies
sudo apt-get install -y \
    gcc \
    make \
    postgresql \
    postgresql-contrib \
    libpq-dev \
    libssl-dev \
    python3 \
    netcat
```

### Setup Database

```bash
# Start PostgreSQL
sudo service postgresql start

# Create database and user (if needed)
sudo -u postgres psql -c "CREATE USER rin WITH PASSWORD 'admin';"
sudo -u postgres psql -c "CREATE DATABASE network OWNER rin;"

# Create tables
make create-tables

# Insert sample data (optional)
make sample-data
```

### Build Server

```bash
# Build
make server

# Or build and run
make run-server
```

---

## 🚀 Chạy Server

### Start Server

```bash
# Default port 8888
./chat_server

# Custom port
./chat_server 9999
```

### Test với Python Client

```bash
# Automated test suite
python3 test_client.py

# Interactive mode
python3 test_client.py -i
```

### Test với netcat

```bash
# Connect
nc localhost 8888

# Send commands
REGISTER alice password123
LOGIN alice password123
LOGOUT
```

---

## 📡 Giao Thức

### Format
```
Client → Server:  COMMAND <param1> <param2> ...\r\n
Server → Client:  <STATUS_CODE> <MESSAGE>\r\n
```

### Status Codes

#### Success (1xx)
- `101` - REGISTER_OK
- `102` - LOGIN_OK
- `103` - LOGOUT_OK

#### Client Errors (2xx)
- `201` - USERNAME_EXISTS
- `202` - WRONG_PASSWORD

#### Auth Errors (3xx)
- `301` - INVALID_USERNAME
- `302` - INVALID_PASSWORD
- `303` - USER_NOT_FOUND
- `304` - ALREADY_LOGGED_IN
- `305` - NOT_LOGGED_IN

#### Server Errors (4xx, 5xx)
- `400` - DATABASE_ERROR
- `500` - UNDEFINED_ERROR

### Commands Implemented

#### REGISTER
```
→ REGISTER <username> <password>
← 101 Registration successful for <username>
← 201 USERNAME_EXISTS
← 301 INVALID_USERNAME
← 302 INVALID_PASSWORD
```

**Validation Rules:**
- Username: 3-50 ký tự, chỉ chứa alphanumeric và underscore
- Password: 6-100 ký tự

#### LOGIN
```
→ LOGIN <username> <password>
← 102 Welcome <username>
← 202 WRONG_PASSWORD
← 303 USER_NOT_FOUND
← 304 ALREADY_LOGGED_IN
```

#### LOGOUT
```
→ LOGOUT
← 103 Goodbye <username>
← 305 NOT_LOGGED_IN
```

---

## 🧪 Testing

### Test Cases

#### 1. Stream Processing Test
```python
# Send multiple commands rapidly
REGISTER user1 pass1
REGISTER user2 pass2
REGISTER user3 pass3
# Server should handle all correctly
```

#### 2. Socket I/O Test
```bash
# Multiple concurrent connections
terminal1$ nc localhost 8888
terminal2$ nc localhost 8888
terminal3$ nc localhost 8888
# All should connect successfully
```

#### 3. Registration Test
```
✓ Valid registration
✗ Duplicate username
✗ Invalid username (too short)
✗ Invalid password (too short)
```

#### 4. Login Test
```
✓ Correct credentials
✗ Wrong password
✗ User not found
✗ Already logged in
```

### Run Full Test Suite

```bash
# Python automated tests
python3 test_client.py

# Expected output:
# ✓ Connected to localhost:8888
# ✓ Registration successful
# ✗ Registration failed (duplicate)
# ✓ Login successful
# ✗ Login failed (already logged in)
# ✓ Logout successful
# ...
```

---

## 🔍 Ví Dụ Trace Hoàn Chỉnh

### Scenario: Client đăng ký user mới

```
┌─────────────────────────────────────────────────────────────────┐
│ Client gửi: "REGISTER alice pass123\r\n"                        │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 1. server.c:server_receive_data()                               │
│    recv(client_fd) → "REGISTER alice pass123\r\n" (29 bytes)    │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 2. protocol.c:stream_buffer_append()                            │
│    buffer->data = "REGISTER alice pass123\r\n"                  │
│    buffer->length = 29                                          │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 3. protocol.c:stream_buffer_extract_message()                   │
│    Tìm "\r\n" tại position 27                                   │
│    Extract: "REGISTER alice pass123"                            │
│    Còn lại buffer: "" (empty)                                   │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 4. auth.c:server_handle_client_message()                        │
│    message = "REGISTER alice pass123"                           │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 5. protocol.c:parse_protocol_message()                          │
│    strtok → "REGISTER"                                          │
│    parse_command_type() → CMD_REGISTER                          │
│    strtok → "alice" → cmd->username                             │
│    strtok → "pass123" → cmd->password                           │
│    cmd->param_count = 2                                         │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 6. auth.c:handle_register_command()                             │
│    Check: client->is_authenticated = 0 ✓                        │
│    Check: param_count >= 2 ✓                                    │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 7. protocol.c:validate_username("alice")                        │
│    strlen = 5 (3-50) ✓                                          │
│    isalnum check ✓                                              │
│    return 1                                                     │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 8. protocol.c:validate_password("pass123")                      │
│    strlen = 7 (6-100) ✓                                         │
│    return 1                                                     │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 9. auth.c:user_exists(db, "alice")                              │
│    Query: "SELECT COUNT(*) FROM users WHERE username='alice'"   │
│    Result: count = 0                                            │
│    return 0 (not exists)                                        │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 10. auth.c:register_user(db, "alice", "pass123")                │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 11. auth.c:hash_password("pass123", output)                     │
│     SHA256("pass123")                                           │
│     → "5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11..." │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 12. database.c:execute_query()                                  │
│     SQL: "INSERT INTO users (username, password_hash, is_online)│
│           VALUES ('alice', '5e88489...', FALSE)"                │
│     PQexec() → PGRES_COMMAND_OK                                 │
│     return 1                                                    │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 13. protocol.c:build_response(101, "Registration successful...") │
│     snprintf → "101 Registration successful for alice\r\n"      │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ 14. server.c:server_send_response(client, response)             │
│     send(client_fd, "101 Registration successful for alice\r\n")│
│     → 43 bytes sent                                             │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│ Client nhận: "101 Registration successful for alice\r\n"        │
└─────────────────────────────────────────────────────────────────┘
```

---

## 📊 Database Schema

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password_hash VARCHAR(128) NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    is_online BOOLEAN DEFAULT FALSE
);
```

### View Database

```bash
# Show users
make show-users

# Output:
# ID    Username     Online    Created At
# ---   ----------   -------   ------------------
#  1    alice        Yes       2024-01-15 10:30:00
#  2    bob          No        2024-01-15 11:45:00
```

---

## 🐛 Debugging

### Enable Debug Output

Server tự động print ra:
```
Received 29 bytes from fd=4: REGISTER alice pass123
Processing message from fd=4: REGISTER alice pass123
✓ New user registered: alice
Sent to fd=4: 101 Registration successful for alice
```

### Common Issues

**1. Database Connection Failed**
```bash
# Check PostgreSQL is running
sudo service postgresql status

# Check credentials in database.h
#define PG_USER "rin"
#define PG_PASS "admin"
```

**2. Port Already in Use**
```bash
# Find process using port 8888
sudo lsof -i :8888

# Kill it
sudo kill -9 <PID>
```

**3. Client Disconnects Immediately**
- Check firewall settings
- Verify network connectivity
- Check server logs for errors

---

## 📈 Performance

### Current Capacity
- Max clients: 100 (configurable via `MAX_CLIENTS`)
- Max message length: 4096 bytes
- I/O model: `select()` (suitable for < 1000 clients)

### Optimization Tips
- For > 1000 clients: Switch to `epoll()` on Linux
- Add connection pooling for database
- Implement message queuing for offline users
- Add caching layer for frequently accessed data

---

## 🔮 Next Steps (Tasks 4-10)

### Remaining Tasks for Người A
4. ✅ Tạo nhóm chat (1đ)
5. ✅ Thêm người dùng vào nhóm (1đ)
6. ✅ Xóa người dùng khỏi nhóm (1đ)
7. ✅ Rời nhóm chat (1đ)
8. ✅ Ghi log hoạt động (1đ)

### Integration với Người B
- Friend management commands
- Private messaging
- Group messaging
- Offline messages

---

## 📝 Notes

### Security Considerations
- **Password hashing**: SHA256 (nên upgrade lên bcrypt)
- **SQL injection**: Hiện chưa có parameterized queries (TODO)
- **Session hijacking**: Chưa có token-based auth (TODO)

### Code Quality
- Error handling: ✅ Basic error checking implemented
- Memory management: ✅ Free allocated memory
- Thread safety: ⚠️ Single-threaded, không cần mutex
- Input validation: ✅ Username/password validation

---

## 📚 References

- [TCP Socket Programming](https://beej.us/guide/bgnet/)
- [PostgreSQL C API (libpq)](https://www.postgresql.org/docs/current/libpq.html)
- [select() man page](https://man7.org/linux/man-pages/man2/select.2.html)

---

## 👥 Contributors

**Người A**: Tasks 1, 2, 3 (Infrastructure & Core)
- Stream processing
- Socket I/O
- User registration & authentication

**Người B**: Tasks TBD (Business Logic)
- Friend management
- Messaging
- Group chat features

---

## 📄 License

Educational project - Internal use only