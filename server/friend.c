#include "friend.h"
#include "../database/database.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 8192
// ============================================================================
// Friend Management Functions
// ============================================================================

void handle_friend_request(Server *server, ClientSession *client, ParsedCommand *cmd) {
    char *response = NULL;
    
    // Kiểm tra đã login chưa
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Lấy username từ ParsedCommand->target_user
    const char *target_username = cmd->target_user;
    if (!target_username || strlen(target_username) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Copy và clean username
    char username_clean[256];
    const char *src = target_username;
    
    // Skip leading spaces
    while (*src == ' ' || *src == '\t') src++;
    
    // Copy username
    int i = 0;
    while (*src && *src != ' ' && *src != '\r' && *src != '\n' && *src != '\t' && i < 255) {
        username_clean[i++] = *src++;
    }
    username_clean[i] = '\0';
    
    if (strlen(username_clean) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Searching for user '%s'\n", username_clean);
    
    // Kiểm tra user có tồn tại không
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            username_clean);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: User '%s' not found\n", username_clean);
        if (res) PQclear(res);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int target_user_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found user '%s' with ID: %d\n", username_clean, target_user_id);
    PQclear(res);
    
    // Không thể gửi lời mời cho chính mình
    if (target_user_id == client->user_id) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Cannot send friend request to yourself");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Kiểm tra đã là bạn bè chưa
    snprintf(query, sizeof(query),
            "SELECT id FROM friends WHERE "
            "((user_id = %d AND friend_id = %d) OR (user_id = %d AND friend_id = %d)) "
            "AND status = 'accepted'",
            client->user_id, target_user_id, target_user_id, client->user_id);
    
    res = execute_query_with_result(server->db_conn, query);
    if (res && PQntuples(res) > 0) {
        PQclear(res);
        response = build_response(STATUS_ALREADY_FRIEND, "ALREADY_FRIEND - Already friends");
        server_send_response(client, response);
        free(response);
        return;
    }
    if (res) PQclear(res);
    
    // Kiểm tra lời mời đang chờ xử lý
    snprintf(query, sizeof(query),
            "SELECT id FROM friends WHERE "
            "((user_id = %d AND friend_id = %d) OR (user_id = %d AND friend_id = %d)) "
            "AND status = 'pending'",
            client->user_id, target_user_id, target_user_id, client->user_id);
    
    res = execute_query_with_result(server->db_conn, query);
    if (res && PQntuples(res) > 0) {
        PQclear(res);
        response = build_response(STATUS_REQUEST_PENDING, "REQUEST_PENDING - Friend request already pending");
        server_send_response(client, response);
        free(response);
        return;
    }
    if (res) PQclear(res);
    
    // Tạo lời mời kết bạn
    snprintf(query, sizeof(query),
            "INSERT INTO friends (user_id, friend_id, status, created_at) "
            "VALUES (%d, %d, 'pending', NOW())",
            client->user_id, target_user_id);
    
    if (!execute_query(server->db_conn, query)) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to send friend request");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Gửi phản hồi thành công
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg),
            "Friend request sent to %s successfully", username_clean);
    response = build_response(STATUS_FRIEND_REQ_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    // Thông báo cho user nhận lời mời (nếu đang online)
    // ClientSession *target_client = server_get_client_by_username(server, username_clean);
    // if (target_client && target_client->is_authenticated) {
    //     char notification[256];
    //     snprintf(notification, sizeof(notification),
    //             "You have a new friend request from %s", client->username);
    //     char *notify_msg = build_response(300, notification);
    //     server_send_response(target_client, notify_msg);
    //     free(notify_msg);
    // }
    
    printf("Friend request: %s -> %s\n", client->username, username_clean);
}

void handle_friend_pending(Server *server, ClientSession *client, ParsedCommand *cmd) {
    char *response = NULL;
    
    // Kiểm tra đã login chưa
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Query để lấy danh sách pending friend requests
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT u.username, f.created_at "
            "FROM friends f "
            "JOIN users u ON f.user_id = u.id "
            "WHERE f.friend_id = %d AND f.status = 'pending' "
            "ORDER BY f.created_at DESC",
            client->user_id);
    
    printf("DEBUG: Querying pending requests for user ID %d\n", client->user_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to fetch pending requests");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int num_pending = PQntuples(res);
    
    if (num_pending == 0) {
        PQclear(res);
        response = build_response(STATUS_FRIEND_PENDING_OK, "No pending friend requests");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Tạo response dạng bảng
    char pending_list[BUFFER_SIZE];
    int offset = 0;
    
    // Header
    offset += snprintf(pending_list + offset, BUFFER_SIZE - offset,
                      "\n+-----+----------------------+----------------------------+\n");
    offset += snprintf(pending_list + offset, BUFFER_SIZE - offset,
                      "| STT | Username             | Time requested             |\n");
    offset += snprintf(pending_list + offset, BUFFER_SIZE - offset,
                      "+-----+----------------------+----------------------------+\n");
    
    // Rows
    for (int i = 0; i < num_pending && offset < BUFFER_SIZE - 200; i++) {
        const char *username = PQgetvalue(res, i, 0);
        const char *created_at = PQgetvalue(res, i, 1);
        
        // Format: | 1   | alice                | 2025-12-04 15:30:45      |
        offset += snprintf(pending_list + offset, BUFFER_SIZE - offset,
                          "| %-3d | %-20s | %-25s |\n", 
                          i + 1, username, created_at);
    }
    
    // Footer
    offset += snprintf(pending_list + offset, BUFFER_SIZE - offset,
                      "+-----+----------------------+----------------------------+\n");
    offset += snprintf(pending_list + offset, BUFFER_SIZE - offset,
                      "Total: %d pending request(s)", num_pending);
    
    PQclear(res);
    
    response = build_response(STATUS_FRIEND_PENDING_OK, pending_list);
    server_send_response(client, response);
    free(response);
    
    printf("Sent %d pending requests to user %s\n", num_pending, client->username);
}

void handle_friend_accept(Server *server, ClientSession *client, ParsedCommand *cmd) {
    char *response = NULL;
    
    // Kiểm tra đã login chưa
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Lấy username từ ParsedCommand->target_user
    const char *requester_username = cmd->target_user;
    if (!requester_username || strlen(requester_username) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Copy và clean username
    char username_clean[256];
    const char *src = requester_username;
    
    // Skip leading spaces
    while (*src == ' ' || *src == '\t') src++;
    
    // Copy username
    int i = 0;
    while (*src && *src != ' ' && *src != '\r' && *src != '\n' && *src != '\t' && i < 255) {
        username_clean[i++] = *src++;
    }
    username_clean[i] = '\0';
    
    if (strlen(username_clean) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Accepting friend request from '%s'\n", username_clean);
    
    // Kiểm tra user có tồn tại không
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            username_clean);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: User '%s' not found\n", username_clean);
        if (res) PQclear(res);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int requester_user_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found requester user '%s' with ID: %d\n", username_clean, requester_user_id);
    PQclear(res);
    
    // Kiểm tra có lời mời pending không (requester_user_id gửi cho client->user_id)
    snprintf(query, sizeof(query),
            "SELECT id FROM friends WHERE "
            "user_id = %d AND friend_id = %d AND status = 'pending'",
            requester_user_id, client->user_id);
    
    res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: No pending request from '%s' to current user\n", username_clean);
        if (res) PQclear(res);
        response = build_response(STATUS_NO_PENDING_REQUEST, "NO_PENDING_REQUEST - No pending friend request from this user");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int friend_request_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found pending request ID: %d\n", friend_request_id);
    PQclear(res);
    
    // Cập nhật status thành 'accepted'
    snprintf(query, sizeof(query),
            "UPDATE friends SET status = 'accepted', created_at = NOW() "
            "WHERE id = %d",
            friend_request_id);
    
    if (!execute_query(server->db_conn, query)) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to accept friend request");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Gửi phản hồi thành công
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg),
            "Friend request from %s accepted successfully", username_clean);
    response = build_response(STATUS_FRIEND_ACCEPT_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    // Thông báo cho người gửi lời mời (nếu đang online)
    // ClientSession *requester_client = server_get_client_by_username(server, username_clean);
    // if (requester_client && requester_client->is_authenticated) {
    //     char notification[256];
    //     snprintf(notification, sizeof(notification),
    //             "%s accepted your friend request", client->username);
    //     char *notify_msg = build_response(300, notification);
    //     server_send_response(requester_client, notify_msg);
    //     free(notify_msg);
    // }
    
    printf("Friend accepted: %s <-> %s\n", username_clean, client->username);
}

void handle_friend_decline(Server *server, ClientSession *client, ParsedCommand *cmd) {
    char *response = NULL;
    
    // Kiểm tra đã login chưa
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Lấy username từ ParsedCommand->target_user
    const char *requester_username = cmd->target_user;
    if (!requester_username || strlen(requester_username) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Copy và clean username
    char username_clean[256];
    const char *src = requester_username;
    
    // Skip leading spaces
    while (*src == ' ' || *src == '\t') src++;
    
    // Copy username
    int i = 0;
    while (*src && *src != ' ' && *src != '\r' && *src != '\n' && *src != '\t' && i < 255) {
        username_clean[i++] = *src++;
    }
    username_clean[i] = '\0';
    
    if (strlen(username_clean) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Declining friend request from '%s'\n", username_clean);
    
    // Kiểm tra user có tồn tại không
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            username_clean);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: User '%s' not found\n", username_clean);
        if (res) PQclear(res);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int requester_user_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found requester user '%s' with ID: %d\n", username_clean, requester_user_id);
    PQclear(res);
    
    // Kiểm tra có lời mời pending không (requester_user_id gửi cho client->user_id)
    snprintf(query, sizeof(query),
            "SELECT id FROM friends WHERE "
            "user_id = %d AND friend_id = %d AND status = 'pending'",
            requester_user_id, client->user_id);
    
    res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: No pending request from '%s' to current user\n", username_clean);
        if (res) PQclear(res);
        response = build_response(STATUS_NO_PENDING_REQUEST, "NO_PENDING_REQUEST - No pending friend request from this user");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int friend_request_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found pending request ID: %d\n", friend_request_id);
    PQclear(res);
    
    // Xóa lời mời kết bạn (decline = delete)
    snprintf(query, sizeof(query),
            "DELETE FROM friends WHERE id = %d",
            friend_request_id);
    
    if (!execute_query(server->db_conn, query)) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to decline friend request");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Gửi phản hồi thành công
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg),
            "Friend request from %s declined successfully", username_clean);
    response = build_response(STATUS_FRIEND_DECLINE_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    // Thông báo cho người gửi lời mời (nếu đang online) - tùy chọn
    // ClientSession *requester_client = server_get_client_by_username(server, username_clean);
    // if (requester_client && requester_client->is_authenticated) {
    //     char notification[256];
    //     snprintf(notification, sizeof(notification),
    //             "%s declined your friend request", client->username);
    //     char *notify_msg = build_response(300, notification);
    //     server_send_response(requester_client, notify_msg);
    //     free(notify_msg);
    // }
    
    printf("Friend declined: %s declined request from %s\n", client->username, username_clean);
}

void handle_friend_remove(Server *server, ClientSession *client, ParsedCommand *cmd) {
    char *response = NULL;
    
    // Kiểm tra đã login chưa
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Lấy username từ ParsedCommand->target_user
    const char *friend_username = cmd->target_user;
    if (!friend_username || strlen(friend_username) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "UNDEFINED_ERROR - Username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Copy và clean username
    char username_clean[256];
    const char *src = friend_username;
    
    // Skip leading spaces
    while (*src == ' ' || *src == '\t') src++;
    
    // Copy username
    int i = 0;
    while (*src && *src != ' ' && *src != '\r' && *src != '\n' && *src != '\t' && i < 255) {
        username_clean[i++] = *src++;
    }
    username_clean[i] = '\0';
    
    if (strlen(username_clean) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "UNDEFINED_ERROR - Username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Removing friend '%s'\n", username_clean);
    
    // Kiểm tra user có tồn tại không
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            username_clean);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: User '%s' not found\n", username_clean);
        if (res) PQclear(res);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int friend_user_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found user '%s' with ID: %d\n", username_clean, friend_user_id);
    PQclear(res);
    
    // Không thể remove chính mình
    if (friend_user_id == client->user_id) {
        response = build_response(STATUS_UNDEFINED_ERROR, "UNDEFINED_ERROR - Cannot remove yourself");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Kiểm tra có phải bạn bè không (status = 'accepted')
    // Kiểm tra cả 2 chiều: (A->B) hoặc (B->A)
    snprintf(query, sizeof(query),
            "SELECT id FROM friends WHERE "
            "((user_id = %d AND friend_id = %d) OR (user_id = %d AND friend_id = %d)) "
            "AND status = 'accepted'",
            client->user_id, friend_user_id, friend_user_id, client->user_id);
    
    res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: Not friends with '%s'\n", username_clean);
        if (res) PQclear(res);
        response = build_response(STATUS_NOT_FRIEND, "NOT_FRIEND - You are not friends with this user");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int friendship_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found friendship ID: %d\n", friendship_id);
    PQclear(res);
    
    // Xóa mối quan hệ bạn bè
    snprintf(query, sizeof(query),
            "DELETE FROM friends WHERE id = %d",
            friendship_id);
    
    if (!execute_query(server->db_conn, query)) {
        response = build_response(STATUS_UNDEFINED_ERROR, "UNDEFINED_ERROR - Failed to remove friend");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Gửi phản hồi thành công
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg),
            "Successfully removed %s from your friend list", username_clean);
    response = build_response(STATUS_FRIEND_REMOVE_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    // Thông báo cho người bị unfriend (nếu đang online)
    // ClientSession *friend_client = server_get_client_by_username(server, username_clean);
    // if (friend_client && friend_client->is_authenticated) {
    //     char notification[256];
    //     snprintf(notification, sizeof(notification),
    //             "%s removed you from their friend list", client->username);
    //     char *notify_msg = build_response(300, notification);
    //     server_send_response(friend_client, notify_msg);
    //     free(notify_msg);
    // }
    
    printf("Friend removed: %s unfriended %s\n", client->username, username_clean);
}

void handle_friend_list(Server *server, ClientSession *client) {  // BỎ ParsedCommand *cmd
    char *response = NULL;
    
    // Kiểm tra đã login chưa
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Query để lấy danh sách bạn bè (status = 'accepted')
    char query[1024];
    snprintf(query, sizeof(query),
            "SELECT DISTINCT "
            "CASE "
            "  WHEN f.user_id = %d THEN u2.username "
            "  ELSE u1.username "
            "END as friend_username, "
            "CASE "
            "  WHEN f.user_id = %d THEN u2.is_online "
            "  ELSE u1.is_online "
            "END as is_online "
            "FROM friends f "
            "JOIN users u1 ON f.user_id = u1.id "
            "JOIN users u2 ON f.friend_id = u2.id "
            "WHERE (f.user_id = %d OR f.friend_id = %d) "
            "AND f.status = 'accepted' "
            "ORDER BY friend_username",
            client->user_id, client->user_id, 
            client->user_id, client->user_id);
    
    printf("DEBUG: Querying friend list for user ID %d\n", client->user_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to fetch friend list");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int num_friends = PQntuples(res);
    
    if (num_friends == 0) {
        PQclear(res);
        response = build_response(STATUS_FRIEND_LIST_OK, "You don't have any friends yet");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Tạo response dạng bảng
    char friend_list[BUFFER_SIZE];
    int offset = 0;
    
    // Header
    offset += snprintf(friend_list + offset, BUFFER_SIZE - offset,
                      "\n+-----+----------------------+------------+\n");
    offset += snprintf(friend_list + offset, BUFFER_SIZE - offset,
                      "| STT | Username             | Status     |\n");
    offset += snprintf(friend_list + offset, BUFFER_SIZE - offset,
                      "+-----+----------------------+------------+\n");
    
    // Rows
    for (int i = 0; i < num_friends && offset < BUFFER_SIZE - 200; i++) {
        const char *username = PQgetvalue(res, i, 0);
        const char *is_online_str = PQgetvalue(res, i, 1);
        
        // Chuyển đổi is_online
        const char *status = "Offline";
        if (is_online_str && (is_online_str[0] == 't' || is_online_str[0] == '1')) {
            status = "Online";
        }
        
        offset += snprintf(friend_list + offset, BUFFER_SIZE - offset,
                          "| %-3d | %-20s | %-10s |\n", 
                          i + 1, username, status);
    }
    
    // Footer
    offset += snprintf(friend_list + offset, BUFFER_SIZE - offset,
                      "+-----+----------------------+------------+\n");
    offset += snprintf(friend_list + offset, BUFFER_SIZE - offset,
                      "Total: %d friend(s)", num_friends);
    
    PQclear(res);
    
    response = build_response(STATUS_FRIEND_LIST_OK, friend_list);
    server_send_response(client, response);
    free(response);
    
    printf("Sent friend list (%d friends) to user %s\n", num_friends, client->username);
}