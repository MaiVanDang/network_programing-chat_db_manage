# 📋 Workflow Chi Tiết: Nhắn Tin Online & Offline

## 🟢 SCENARIO 1: Nhắn Tin ONLINE (Receiver đang online)

### Bước 1: Client A gửi tin nhắn
```
Client A → Server: "MSG userB Hello, how are you?"
```

### Bước 2: Server xác thực (handle_send_message)
```c
✅ Check: Client A đã login chưa?
✅ Check: UserB có tồn tại không? (query users table)
✅ Check: Client A và UserB có phải bạn bè không? (query friends table với status='accepted')
✅ Check: Message có trống hoặc quá dài không?
```

### Bước 3: Lưu message vào database
```sql
INSERT INTO messages (sender_id, receiver_id, content, is_delivered)
VALUES (clientA_id, userB_id, 'Hello, how are you?', FALSE)
```
**Note**: `is_delivered` mặc định là `FALSE`

### Bước 4: Kiểm tra UserB có online không?
```c
receiver_client = find_client_by_user_id(server, userB_id)

if (receiver_client && receiver_client->is_authenticated) {
    // UserB ONLINE ✅
}
```

### Bước 5: Forward message realtime đến UserB
```c
forward_message_to_online_user(server, userB_id, "userA", "Hello, how are you?")

// Gửi notification:
Server → Client B: "201 NEW_MESSAGE from userA: Hello, how are you?"
```

### Bước 6: Update is_delivered = TRUE
```sql
UPDATE messages 
SET is_delivered = TRUE 
WHERE id = (
    SELECT id FROM messages 
    WHERE sender_id = clientA_id 
      AND receiver_id = userB_id 
      AND content = 'Hello, how are you?' 
      AND is_delivered = FALSE 
    ORDER BY created_at DESC 
    LIMIT 1
)
```
**Lý do**: Vì UserB đã nhận được tin nhặn realtime → đánh dấu đã delivered

### Bước 7: Phản hồi cho Client A
```
Server → Client A: "109 OK - Message sent successfully (delivered)"
```

---

## 🔴 SCENARIO 2: Nhắn Tin OFFLINE (Receiver không online)

### Bước 1: Client A gửi tin nhắn
```
Client A → Server: "MSG userB Are you there?"
```

### Bước 2: Server xác thực (tương tự Scenario 1)
```c
✅ Check: Client A đã login chưa?
✅ Check: UserB có tồn tại không?
✅ Check: Client A và UserB có phải bạn bè không?
✅ Check: Message có hợp lệ không?
```

### Bước 3: Lưu message vào database
```sql
INSERT INTO messages (sender_id, receiver_id, content, is_delivered)
VALUES (clientA_id, userB_id, 'Are you there?', FALSE)
```
**Note**: `is_delivered` = `FALSE` (mặc định)

### Bước 4: Kiểm tra UserB có online không?
```c
receiver_client = find_client_by_user_id(server, userB_id)

if (!receiver_client || !receiver_client->is_authenticated) {
    // UserB OFFLINE ❌
}
```

### Bước 5: Không forward (UserB offline)
```c
// SKIP forward_message_to_online_user()
// KHÔNG update is_delivered = TRUE
```
**Lý do**: UserB chưa nhận được tin nhắn → giữ `is_delivered = FALSE`

### Bước 6: Phản hồi cho Client A
```
Server → Client A: "116 OK - Message sent successfully (stored for offline)"
```

---

## 📬 SCENARIO 3: Lấy Tin Nhắn OFFLINE (UserB login sau)

### Bước 1: UserB login và chọn chat với UserA
```
Client B → Server: "LOGIN userB password123"
Server → Client B: "102 LOGIN_OK"

Client B chọn menu: "3. Send Message"
Client B nhập: "userA"
```

### Bước 2: Client B tự động gửi GET_OFFLINE_MSG
```c
// Trong client/client.c - handle_messaging_mode()
char get_offline_cmd[BUFFER_SIZE];
snprintf(get_offline_cmd, sizeof(get_offline_cmd), 
         "GET_OFFLINE_MSG %s", "userA");
send_message(client, get_offline_cmd);
```

### Bước 3: Server xử lý GET_OFFLINE_MSG (handle_get_offline_messages)
```c
✅ Check: Client B đã login chưa?
✅ Check: UserA có tồn tại không?
```

### Bước 4: Query tin nhắn chưa đọc
```sql
SELECT id, content, created_at 
FROM messages 
WHERE sender_id = userA_id 
  AND receiver_id = userB_id 
  AND is_delivered = FALSE   -- Chỉ lấy tin nhắn chưa đọc
ORDER BY created_at ASC      -- Từ cũ đến mới
```

**Kết quả giả sử**:
| id | content | created_at |
|----|---------|------------|
| 42 | Hello, how are you? | 2025-12-19 10:30:00 |
| 43 | Are you there? | 2025-12-19 10:35:00 |

### Bước 5: Tạo response chứa tất cả tin nhắn
```c
char message_list[BUFFER_SIZE * 2];
offset = snprintf(message_list, ..., "\n=== OFFLINE MESSAGES FROM userA ===\n");

for (int i = 0; i < num_messages; i++) {
    message_ids[i] = 42, 43, ...  // Lưu IDs
    offset += snprintf(..., "[2025-12-19 10:30:00] Hello, how are you?\n");
    offset += snprintf(..., "[2025-12-19 10:35:00] Are you there?\n");
}

offset += snprintf(..., "=== END OF OFFLINE MESSAGES (2 total) ===");
```

### Bước 6: Update is_delivered = TRUE
```sql
UPDATE messages SET is_delivered = TRUE WHERE id = 42;
UPDATE messages SET is_delivered = TRUE WHERE id = 43;
```
**Lý do**: UserB đã nhận và đọc tin nhắn → đánh dấu đã delivered

### Bước 7: Gửi response cho Client B
```
Server → Client B: 
"118 GET_OFFLINE_MSG_OK

=== OFFLINE MESSAGES FROM userA ===
[2025-12-19 10:30:00] Hello, how are you?
[2025-12-19 10:35:00] Are you there?
=== END OF OFFLINE MESSAGES (2 total) ==="
```

### Bước 8: Client B hiển thị tin nhắn
```
--- Chatting with: userA ---

=== OFFLINE MESSAGES FROM userA ===
[2025-12-19 10:30:00] Hello, how are you?
[2025-12-19 10:35:00] Are you there?
=== END OF OFFLINE MESSAGES (2 total) ===

[You]: 
```

---

## 📊 So Sánh is_delivered Flag

| Tình huống | is_delivered sau INSERT | is_delivered sau xử lý |
|-----------|-------------------------|------------------------|
| **Online** | FALSE | TRUE (update ngay lập tức) |
| **Offline** | FALSE | FALSE (giữ nguyên) |
| **Get Offline** | - | TRUE (update khi lấy) |

---

## 🔄 Sequence Diagram

```
=== ONLINE MESSAGE ===
ClientA  →  Server  :  MSG userB Hello
Server   →  DB      :  INSERT (is_delivered=FALSE)
Server   ↔  Memory  :  find_client_by_user_id(userB) → FOUND ✅
Server   →  ClientB :  201 NEW_MESSAGE from userA: Hello
Server   →  DB      :  UPDATE is_delivered=TRUE
Server   →  ClientA :  109 OK (delivered)

=== OFFLINE MESSAGE ===
ClientA  →  Server  :  MSG userB Are you there?
Server   →  DB      :  INSERT (is_delivered=FALSE)
Server   ↔  Memory  :  find_client_by_user_id(userB) → NOT FOUND ❌
Server   →  ClientA :  116 OK (stored for offline)

=== GET OFFLINE (UserB login sau) ===
ClientB  →  Server  :  GET_OFFLINE_MSG userA
Server   →  DB      :  SELECT * WHERE is_delivered=FALSE
Server   →  DB      :  UPDATE is_delivered=TRUE (for all fetched)
Server   →  ClientB :  118 OK + message list
```

---

## 🎯 Key Points

1. **is_delivered = FALSE**: Tin nhắn chưa được người nhận đọc
2. **is_delivered = TRUE**: Tin nhắn đã được gửi realtime (online) hoặc đã được lấy về (offline)
3. **GET_OFFLINE_MSG**: Tự động gọi khi bắt đầu chat → lấy + update is_delivered
4. **Database index**: `idx_messages_receiver (receiver_id, is_delivered)` để query nhanh

---

## 🗂️ Database Schema - Messages Table

```sql
CREATE TABLE messages (
    id SERIAL PRIMARY KEY,
    sender_id INTEGER REFERENCES users(id),
    receiver_id INTEGER REFERENCES users(id),
    group_id INTEGER REFERENCES groups(id),
    content TEXT NOT NULL,
    is_delivered BOOLEAN DEFAULT FALSE,  -- ⭐ Core field cho offline messaging
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Index để tối ưu query offline messages
CREATE INDEX idx_messages_receiver 
ON messages(receiver_id, is_delivered);
```

---

## 📂 File Liên Quan

- **Handler**: `server/message.c`
  - `handle_send_message()` - Xử lý gửi tin nhắn
  - `handle_get_offline_messages()` - Xử lý lấy tin nhắn offline
  - `forward_message_to_online_user()` - Forward tin nhắn realtime
  
- **Protocol**: `common/protocol.h`, `common/protocol.c`
  - Status codes: `STATUS_MSG_OK`, `STATUS_OFFLINE_MSG_OK`, `STATUS_GET_OFFLINE_MSG_OK`, `STATUS_NOT_HAVE_OFFLINE_MESSAGE`
  - Commands: `CMD_MSG`, `CMD_GET_OFFLINE_MSG`
  
- **Router**: `common/router.c`
  - `server_handle_client_message()` - Routing commands
  
- **Client**: `client/client.c`
  - `handle_messaging_mode()` - Client-side chat interface

---

## 🧪 Test Scenarios

### Test 1: Online Message
```bash
# Terminal 1: Start Server
./chat_server 8888

# Terminal 2: User A
./chat_client
> LOGIN alice password
> 3  # Send Message
> bob
> Hello Bob!

# Terminal 3: User B (online)
./chat_client
> LOGIN bob password
# Should receive: [alice]: Hello Bob!
```

### Test 2: Offline Message
```bash
# Terminal 1: Start Server
./chat_server 8888

# Terminal 2: User A (bob is NOT online)
./chat_client
> LOGIN alice password
> 3  # Send Message
> bob
> Are you there?
# Should see: "116 OK - Message sent successfully (stored for offline)"

# Terminal 3: User B login sau
./chat_client
> LOGIN bob password
> 3  # Send Message
> alice
# Should see offline messages:
# === OFFLINE MESSAGES FROM alice ===
# [timestamp] Are you there?
# === END OF OFFLINE MESSAGES (1 total) ===
```

### Test 3: Multiple Offline Messages
```bash
# User A sends 3 messages while B is offline
MSG bob Message 1
MSG bob Message 2
MSG bob Message 3

# User B login and chat with A
# Should receive all 3 messages in order
```

---

## 🐛 Debugging Tips

1. **Check is_delivered status**:
```sql
SELECT id, sender_id, receiver_id, content, is_delivered, created_at 
FROM messages 
WHERE is_delivered = FALSE;
```

2. **Check server logs**:
```
DEBUG: Message saved to database - OK
DEBUG: Forwarding message to online user ID:5
DEBUG: Message marked as delivered in database
```

3. **Check client online status**:
```sql
SELECT id, username, is_online FROM users;
```

---

## 📝 Notes

- Tin nhắn offline có thể tích lũy nếu user không login lâu
- Cân nhắc thêm limit số lượng tin nhắn offline (hiện tại: 100 messages)
- Có thể mở rộng thêm timestamp "read_at" để track khi nào tin nhắn được đọc
- Cân nhắc thêm pagination nếu có quá nhiều offline messages
