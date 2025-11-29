# Chat Client - Hướng Dẫn Sử Dụng

## 📦 Compilation

```bash
# Compile client
gcc -o chat_client client.c

# Hoặc dùng make
make client
```

## 🚀 Chạy Client

### Kết nối localhost (mặc định)
```bash
./chat_client
# Kết nối đến 127.0.0.1:8888
```

### Kết nối custom host
```bash
./chat_client 192.168.1.100
# Kết nối đến 192.168.1.100:8888
```

### Kết nối custom host và port
```bash
./chat_client 192.168.1.100 9999
# Kết nối đến 192.168.1.100:9999
```

---

## 💬 Các Lệnh Có Sẵn

### 1. Authentication

```bash
# Đăng ký
> REGISTER alice password123
Server: 101 Registration successful for alice

# Đăng nhập
> LOGIN alice password123
Server: 102 Welcome alice

# Đăng xuất
> LOGOUT
Server: 103 Goodbye alice
```

### 2. Friend Management

```bash
# Gửi lời mời kết bạn
> FRIEND_REQ bob
Server: 104 Friend request sent to bob

# Chấp nhận lời mời
> FRIEND_ACCEPT bob
Server: 105 You are now friends with bob

# Từ chối lời mời
> FRIEND_DECLINE bob
Server: 106 Friend request declined

# Xóa bạn
> FRIEND_REMOVE bob
Server: 107 Removed bob from friends

# Xem danh sách bạn
> FRIEND_LIST
Server: 108 Friends: alice, bob, charlie
```

### 3. Messaging

```bash
# Gửi tin nhắn riêng
> MSG bob Hello, how are you?
Server: 109 Message sent to bob
```

### 4. Group Chat

```bash
# Tạo nhóm
> GROUP_CREATE developers
Server: 110 Group created with ID: 1

# Mời người vào nhóm
> GROUP_INVITE 1 bob
Server: 111 Invited bob to group 1

# Tham gia nhóm
> GROUP_JOIN 1
Server: 112 Joined group 1

# Rời nhóm
> GROUP_LEAVE 1
Server: 113 Left group 1

# Kick thành viên (owner only)
> GROUP_KICK 1 bob
Server: 114 Kicked bob from group 1

# Gửi tin nhắn nhóm
> GROUP_MSG 1 Hello everyone!
Server: 115 Message sent to group 1
```

### 5. Client Commands

```bash
# Hiển thị help
> help

# Thoát
> quit
```

---

## 📝 Ví Dụ Session Hoàn Chỉnh

```bash
$ ./chat_client

========================================
       Chat Client - Network Project
========================================

Connecting to 127.0.0.1:8888...
Connected successfully!

Server: 100 Welcome to chat server

Type 'help' for available commands

> REGISTER alice password123
Server: 101 Registration successful for alice

> LOGIN alice password123
Server: 102 Welcome alice

> FRIEND_REQ bob
Server: 104 Friend request sent to bob

> FRIEND_LIST
Server: 108 Friends: bob

> MSG bob Hey!
Server: 109 Message sent to bob

> GROUP_CREATE myteam
Server: 110 Group created with ID: 1

> GROUP_INVITE 1 bob
Server: 111 Invited bob to group 1

> GROUP_MSG 1 Hello team!
Server: 115 Message sent to group 1

> LOGOUT
Server: 103 Goodbye alice

> quit
Goodbye!

Disconnected from server
```

---

## 🛠️ Troubleshooting

### Connection refused
```bash
# Kiểm tra server đang chạy
ps aux | grep chat_server

# Start server
./chat_server
```

### Invalid command
```bash
# Xem help để biết cú pháp đúng
> help
```

---

## 🔧 Testing

### Test với nhiều client

**Terminal 1:**
```bash
./chat_client
> REGISTER alice pass123
> LOGIN alice pass123
```

**Terminal 2:**
```bash
./chat_client
> REGISTER bob pass456
> LOGIN bob pass456
```

### Kết nối remote

```bash
# Remote server
./chat_client 123.45.67.89 8888

# Local network
./chat_client 192.168.1.100 8888
```

---

## 📊 Status Codes

- **1xx**: Success (101-116)
- **2xx**: Client errors (201, 202)
- **3xx**: Auth errors (301-306)
- **4xx-5xx**: Server errors (400-422, 500)

---

## 🐛 Known Issues

1. Multi-line messages not supported
2. Message limit: 4096 bytes
3. No message history

---

**Happy Chatting! 💬**
