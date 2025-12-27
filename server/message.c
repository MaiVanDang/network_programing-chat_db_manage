#include "message.h"
#include "../database/database.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 8192

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

/**
 * Kiểm tra authentication và gửi error response nếu chưa login
 * Return: 1 nếu authenticated, 0 nếu không
 */
static int check_authentication(Server *server __attribute__((unused)), ClientSession *client) {
    if (!client->is_authenticated) {
        printf("ERROR: User not authenticated\n");
        char *response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return 0;
    }
    return 1;
}

/**
 * Gửi error response và log
 */
static void send_error_response(ClientSession *client, int status_code, const char *message, const char *log_msg) {
    if (log_msg) {
        printf("ERROR: %s\n", log_msg);
    }
    char *response = build_response(status_code, message);
    server_send_response(client, response);
    free(response);
}

/**
 * Tìm user_id từ username
 * Return: user_id nếu tìm thấy, -1 nếu không tìm thấy hoặc lỗi
 */
static int get_user_id_by_username(PGconn *conn, const char *username) {
    if (!username || strlen(username) == 0) {
        return -1;
    }
    
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            username);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return -1;
    }
    
    int user_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    return user_id;
}

/**
 * Validate target user và kiểm tra xem có tồn tại không
 * Return: user_id nếu valid, -1 nếu invalid (và đã gửi error response)
 */
int validate_target_user(Server *server, ClientSession *client, const char *username, const char *error_context) {
    if (!username || strlen(username) == 0) {
        send_error_response(client, STATUS_UNDEFINED_ERROR, 
                          "Username required",
                          "Username is empty");
        return -1;
    }
    
    int user_id = get_user_id_by_username(server->db_conn, username);
    if (user_id < 0) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg), "%s '%s' not found", error_context, username);
        send_error_response(client, STATUS_USER_NOT_FOUND,
                          "User who you want to send does not exist",
                          log_msg);
        return -1;
    }
    
    return user_id;
}

/**
 * Mark nhiều messages là delivered
 */
int mark_messages_as_delivered(PGconn *conn, int *message_ids, int count) {
    if (!conn || !message_ids || count <= 0) {
        return 0;
    }
    
    char query[512];
    int success_count = 0;
    
    for (int i = 0; i < count; i++) {
        snprintf(query, sizeof(query),
                "UPDATE messages SET is_delivered = TRUE WHERE id = %d",
                message_ids[i]);
        
        if (execute_query(conn, query)) {
            success_count++;
        } else {
            printf("WARNING: Failed to mark message %d as delivered\n", message_ids[i]);
        }
    }
    
    printf("DEBUG: Marked %d/%d message(s) as delivered\n", success_count, count);
    return success_count;
}

/**
 * Mark một message cụ thể là delivered
 */
int mark_message_as_delivered(PGconn *conn, int sender_id, int receiver_id, const char *message_text) {
    // Dùng parameterized query để tránh lỗi escape
    const char *query = 
            "UPDATE messages SET is_delivered = TRUE "
            "WHERE id = ("
            "    SELECT id FROM messages "
            "    WHERE sender_id = $1 AND receiver_id = $2 "
            "    AND content = $3 "
            "    AND is_delivered = FALSE "
            "    ORDER BY created_at DESC "
            "    LIMIT 1"
            ")";
    
    char sender_str[32], receiver_str[32];
    snprintf(sender_str, sizeof(sender_str), "%d", sender_id);
    snprintf(receiver_str, sizeof(receiver_str), "%d", receiver_id);
    
    const char *paramValues[3] = {sender_str, receiver_str, message_text};
    
    PGresult *res = PQexecParams(conn, query, 3, NULL, paramValues, NULL, NULL, 0);
    int success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    
    if (success) {
        printf("DEBUG: Message marked as delivered in database\n");
    } else {
        printf("ERROR: Failed to update delivery status: %s\n", PQerrorMessage(conn));
    }
    
    PQclear(res);
    
    return success;
}

// ============================================================================
// MAIN HANDLER: Send Direct Message
// ============================================================================

void handle_send_message(Server *server, ClientSession *client, ParsedCommand *cmd) {
    char *response = NULL;
    
    printf("\n=== HANDLE SEND MESSAGE ===\n");
    printf("DEBUG: From user '%s' (ID:%d)\n", client->username, client->user_id);
    
    // Check authentication
    if (!check_authentication(server, client)) {
        return;
    }
    
    const char *receiver_username = cmd->target_user;
    const char *message_text = cmd->message;
    
    printf("DEBUG: Target user: '%s'\n", receiver_username);
    printf("DEBUG: Message: '%s'\n", message_text);
    
    // Validate and get receiver ID
    int receiver_id = validate_target_user(server, client, receiver_username, "Receiver");
    if (receiver_id < 0) {
        return;
    }
    printf("DEBUG: Found receiver '%s' with ID: %d\n", receiver_username, receiver_id);
    
    // Check not send message to yourself
    if (receiver_id == client->user_id) {
        send_error_response(client, STATUS_UNDEFINED_ERROR,
                          "Cannot send message to yourself",
                          "Cannot send message to yourself");
        return;
    }
    
    // Check friendship (403 - NOT_FRIEND)
    if (!check_friendship(server->db_conn, client->user_id, receiver_id)) {
        send_error_response(client, STATUS_NOT_FRIEND,
                          "You must be friends to send messages",
                          "Users are not friends");
        return;
    }
    printf("DEBUG: Users are friends - OK\n");
    
    // Check message text is blank?
    if (!message_text || strlen(message_text) == 0) {
        send_error_response(client, STATUS_UNDEFINED_ERROR,
                          "Message text required",
                          "Message text is empty");
        return;
    }
    
    // Check message length
    if (strlen(message_text) > MAX_MESSAGE_LENGTH - 1) {
        char log_msg[128];
        snprintf(log_msg, sizeof(log_msg), "Message too long (%zu bytes)", strlen(message_text));
        send_error_response(client, STATUS_MESSAGE_TOO_LONG,
                          "Message exceeds maximum length",
                          log_msg);
        return;
    }

    // Save message to database 
    if (!save_message_to_database(server->db_conn, client->user_id, receiver_id, message_text)) {
        send_error_response(client, STATUS_DATABASE_ERROR,
                          "DATABASE_ERROR - Failed to save message",
                          "Failed to save message to database");
        return;
    }
    printf("DEBUG: Message saved to database - OK\n");
    
    // Check if receiver is online
    ClientSession *receiver_client = find_client_by_user_id(server, receiver_id);
    
    if (receiver_client && receiver_client->is_authenticated) {
        // Receiver đang online - Forward message realtime
        printf("DEBUG: Receiver is ONLINE - Forwarding message\n");
        forward_message_to_online_user(server, receiver_id, client->username, message_text);
        
        // Update message as delivered (receiver is online)
        mark_message_as_delivered(server->db_conn, client->user_id, receiver_id, message_text);
        response = build_response(STATUS_MSG_OK, "OK - Message sent successfully (delivered)");
    } else {
        // Receiver offline - Chỉ lưu database (is_read = FALSE by default)
        printf("DEBUG: Receiver is OFFLINE - Message saved for later\n");
        response = build_response(STATUS_OFFLINE_MSG_OK, "OK - Message sent successfully (stored for offline)");
    }
    
    server_send_response(client, response);
    free(response);
    
    printf("=== END HANDLE SEND MESSAGE ===\n\n");
}

// Check if two users are friends

int check_friendship(PGconn *conn, int user_id1, int user_id2) {
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM friends "
            "WHERE ((user_id = %d AND friend_id = %d) OR (user_id = %d AND friend_id = %d)) "
            "AND status = 'accepted'",
            user_id1, user_id2, user_id2, user_id1);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (!res) {
        return 0;  // Database error
    }
    
    int is_friend = (PQntuples(res) > 0);
    PQclear(res);
    
    return is_friend;
}

// Save message to database

int save_message_to_database(PGconn *conn, int sender_id, int receiver_id, const char *message_text) {
    // Dùng parameterized query để tránh SQL injection và lỗi escape
    const char *query = "INSERT INTO messages (sender_id, receiver_id, content) VALUES ($1, $2, $3)";
    
    // Chuyển int thành string
    char sender_str[32], receiver_str[32];
    snprintf(sender_str, sizeof(sender_str), "%d", sender_id);
    snprintf(receiver_str, sizeof(receiver_str), "%d", receiver_id);
    
    const char *paramValues[3] = {sender_str, receiver_str, message_text};
    
    PGresult *res = PQexecParams(conn, query, 3, NULL, paramValues, NULL, NULL, 0);
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "ERROR: Failed to insert message into database: %s\n", PQerrorMessage(conn));
        PQclear(res);
        return 0;
    }
    
    PQclear(res);
    return 1;
}

// Forward message to online user (realtime)

int forward_message_to_online_user(Server *server, int receiver_id, const char *sender_username, const char *message_text) {
    ClientSession *receiver_client = find_client_by_user_id(server, receiver_id);
    
    if (!receiver_client || !receiver_client->is_authenticated) {
        return 0;  // Receiver not online
    }
    
    printf("DEBUG: Forwarding message to online user ID:%d\n", receiver_id);
    
    // Tạo notification message
    char notification[BUFFER_SIZE];
    snprintf(notification, sizeof(notification),
            "NEW_MESSAGE from %s: %s",
            sender_username, message_text);
    
    // Gửi notification đến receiver với status code 201
    char *response = build_response(201, notification);
    server_send_response(receiver_client, response);
    free(response);
    
    return 1;  // Forwarded successfully
}

// ============================================================================
// HELPER: Find client session by user_id
// ============================================================================

ClientSession* find_client_by_user_id(Server *server, int user_id) {
    if (!server) return NULL;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        ClientSession *client = server->clients[i];
        if (client && client->user_id == user_id && client->is_authenticated) {
            return client;
        }
    }
    
    return NULL;  // Not found
}

void handle_get_offline_messages(Server *server, ClientSession *client, ParsedCommand *cmd) {
    char *response = NULL;
    
    printf("\n=== OFFLINE MESSAGES ===\n");
    printf("DEBUG: User '%s' (ID:%d) requesting offline messages\n", 
           client->username, client->user_id);
    
    // Check authentication
    if (!check_authentication(server, client)) {
        return;
    }
    
    // Validate and get sender ID
    const char *sender_username = cmd->target_user;
    printf("DEBUG: Fetching offline messages from '%s'\n", sender_username);
    
    int sender_id = validate_target_user(server, client, sender_username, "Sender");
    if (sender_id < 0) {
        return;
    }
    printf("DEBUG: Found sender '%s' with ID: %d\n", sender_username, sender_id);
    
    // Lấy tin nhắn chưa đọc (is_delivered = FALSE)
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id, content, created_at "
            "FROM messages "
            "WHERE sender_id = %d AND receiver_id = %d AND is_delivered = FALSE "
            "ORDER BY created_at ASC",
            sender_id, client->user_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) {
        send_error_response(client, STATUS_DATABASE_ERROR,
                          "UNKNOWN_ERROR - Failed to fetch offline messages",
                          "Database query failed");
        return;
    }
    
    int num_messages = PQntuples(res);
    
    if (num_messages == 0) {
        PQclear(res);
        printf("DEBUG: No offline messages from '%s'\n", sender_username);
        response = build_response(STATUS_NOT_HAVE_OFFLINE_MESSAGE, "No offline messages");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Found %d offline message(s)\n", num_messages);
    
    // Tạo response chứa tất cả tin nhắn
    char message_list[BUFFER_SIZE * 2];
    int offset = 0;
    
    offset += snprintf(message_list + offset, sizeof(message_list) - offset,
                      "\n=== OFFLINE MESSAGES FROM %s ===\n", sender_username);
    
    // Lưu các message ID để update sau
    int message_ids[100];  // Giới hạn tối đa 100 messages
    int id_count = 0;
    
    for (int i = 0; i < num_messages && offset < (int)sizeof(message_list) - 500; i++) {
        const char *content = PQgetvalue(res, i, 1);
        const char *created_at = PQgetvalue(res, i, 2);
        int msg_id = atoi(PQgetvalue(res, i, 0));
        
        if (id_count < 100) {
            message_ids[id_count++] = msg_id;
        }
        
        offset += snprintf(message_list + offset, sizeof(message_list) - offset,
                          "[%s] %s\n", created_at, content);
    }
    
    offset += snprintf(message_list + offset, sizeof(message_list) - offset,
                      "=== END OF OFFLINE MESSAGES (%d total) ===", num_messages);
    
    PQclear(res);
    
    // Update is_delivered = TRUE cho tất cả tin nhắn đã gửi
    mark_messages_as_delivered(server->db_conn, message_ids, id_count);
    
    // Gửi response
    response = build_response(STATUS_GET_OFFLINE_MSG_OK, message_list);
    server_send_response(client, response);
    free(response);
    
    printf("=== END HANDLE GET OFFLINE MESSAGES ===\n\n");
}