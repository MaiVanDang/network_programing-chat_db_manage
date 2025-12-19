#include "message.h"
#include "../database/database.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 8192

// ============================================================================
// MAIN HANDLER: Send Direct Message
// ============================================================================

void handle_send_message(Server *server, ClientSession *client, ParsedCommand *cmd) {
    char *response = NULL;
    
    printf("\n=== HANDLE SEND MESSAGE ===\n");
    printf("DEBUG: From user '%s' (ID:%d)\n", client->username, client->user_id);
    
    // Check login
    if (!client->is_authenticated) {
        printf("ERROR: User not authenticated\n");
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    const char *receiver_username = cmd->target_user;
    const char *message_text = cmd->message;
    
    printf("DEBUG: Target user: '%s'\n", receiver_username);
    printf("DEBUG: Message: '%s'\n", message_text);
    
    // Check receiver username
    if (!receiver_username || strlen(receiver_username) == 0) {
        printf("ERROR: Receiver username is empty\n");
        response = build_response(STATUS_UNDEFINED_ERROR, "UNDEFINED_ERROR - Receiver username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check if receiver exists (303 - USER_NOT_FOUND)
    char query[BUFFER_SIZE];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            receiver_username);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        printf("ERROR: User '%s' not found\n", receiver_username);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - Receiver does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int receiver_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    printf("DEBUG: Found receiver '%s' with ID: %d\n", receiver_username, receiver_id);
    
    // Check not send message to yourself
    if (receiver_id == client->user_id) {
        printf("ERROR: Cannot send message to yourself\n");
        response = build_response(STATUS_UNDEFINED_ERROR, "UNDEFINED_ERROR - Cannot send message to yourself");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check friendship (403 - NOT_FRIEND)
    if (!check_friendship(server->db_conn, client->user_id, receiver_id)) {
        printf("ERROR: Users are not friends\n");
        response = build_response(STATUS_NOT_FRIEND, "NOT_FRIEND - You must be friends to send messages");
        server_send_response(client, response);
        free(response);
        return;
    }
    printf("DEBUG: Users are friends - OK\n");
    
    // Check message text is blank?
    if (!message_text || strlen(message_text) == 0) {
        printf("ERROR: Message text is empty\n");
        response = build_response(STATUS_UNDEFINED_ERROR, "UNDEFINED_ERROR - Message text required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check message length
    if (strlen(message_text) > MAX_MESSAGE_LENGTH - 1) {
        printf("ERROR: Message too long (%zu bytes)\n", strlen(message_text));
        response = build_response(STATUS_MESSAGE_TOO_LONG, "MESSAGE_TOO_LONG - Message exceeds maximum length");
        server_send_response(client, response);
        free(response);
        return;
    }

    // Save message to database 
    if (!save_message_to_database(server->db_conn, client->user_id, receiver_id, message_text)) {
        printf("ERROR: Failed to save message to database\n");
        response = build_response(STATUS_DATABASE_ERROR, "DATABASE_ERROR - Failed to save message");
        server_send_response(client, response);
        free(response);
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
        char update_query[BUFFER_SIZE];
        char *escaped_msg = PQescapeLiteral(server->db_conn, message_text, strlen(message_text));
        if (!escaped_msg) {
            printf("ERROR: Failed to escape message text\n");
            response = build_response(STATUS_MSG_OK, "OK - Message sent successfully (delivered)");
        } else {
            snprintf(update_query, sizeof(update_query),
                    "UPDATE messages SET is_delivered = TRUE "
                    "WHERE id = (SELECT id FROM messages "
                    "WHERE sender_id = %d AND receiver_id = %d "
                    "AND content = %s "
                    "AND is_delivered = FALSE "
                    "ORDER BY created_at DESC LIMIT 1)",
                    client->user_id, receiver_id, escaped_msg);
            
            PGresult *update_res = PQexec(server->db_conn, update_query);
            
            if (PQresultStatus(update_res) == PGRES_COMMAND_OK) {
                printf("DEBUG: Message marked as delivered in database\n");
            } else {
                printf("ERROR: Failed to update delivery status: %s\n", PQerrorMessage(server->db_conn));
            }
            
            PQclear(update_res);
            PQfreemem(escaped_msg);
            
            response = build_response(STATUS_MSG_OK, "OK - Message sent successfully (delivered)");
        }
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
    char query[BUFFER_SIZE * 2];
    
    // Escape message text để tránh SQL injection
    char *escaped_message = PQescapeLiteral(conn, message_text, strlen(message_text));
    if (!escaped_message) {
        fprintf(stderr, "ERROR: Failed to escape message text\n");
        return 0;
    }
    
    snprintf(query, sizeof(query),
            "INSERT INTO messages (sender_id, receiver_id, content) "
            "VALUES (%d, %d, %s)",
            sender_id, receiver_id, escaped_message);
    
    PQfreemem(escaped_message);
    
    int result = execute_query(conn, query);
    if (!result) {
        fprintf(stderr, "ERROR: Failed to insert message into database\n");
    }
    
    return result;
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
    
    // Check login
    if (!client->is_authenticated) {
        printf("ERROR: User not authenticated\n");
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Lấy username của người gửi từ command
    const char *sender_username = cmd->target_user;
    if (!sender_username || strlen(sender_username) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Sender username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Fetching offline messages from '%s'\n", sender_username);
    
    // Tìm sender_id
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            sender_username);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        printf("DEBUG: Sender '%s' not found\n", sender_username);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - Sender does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int sender_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    printf("DEBUG: Found sender '%s' with ID: %d\n", sender_username, sender_id);
    
    // Lấy tin nhắn chưa đọc (is_delivered = FALSE)
    snprintf(query, sizeof(query),
            "SELECT id, content, created_at "
            "FROM messages "
            "WHERE sender_id = %d AND receiver_id = %d AND is_delivered = FALSE "
            "ORDER BY created_at ASC",
            sender_id, client->user_id);
    
    res = execute_query_with_result(server->db_conn, query);
    if (!res) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to fetch offline messages");
        server_send_response(client, response);
        free(response);
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
    for (int i = 0; i < id_count; i++) {
        snprintf(query, sizeof(query),
                "UPDATE messages SET is_delivered = TRUE WHERE id = %d",
                message_ids[i]);
        
        if (!execute_query(server->db_conn, query)) {
            printf("WARNING: Failed to mark message %d as delivered\n", message_ids[i]);
        }
    }
    
    printf("DEBUG: Marked %d message(s) as delivered\n", id_count);
    
    // Gửi response
    response = build_response(STATUS_GET_OFFLINE_MSG_OK, message_list);
    server_send_response(client, response);
    free(response);
    
    printf("=== END HANDLE GET OFFLINE MESSAGES ===\n\n");
}