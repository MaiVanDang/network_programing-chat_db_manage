#include "friend.h"
#include "../database/database.h"
#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 8192

// ==================== Helper Functions ====================

/**
 * @function validate_authentication: Check if user is authenticated.
 * 
 * @param client: Pointer to the client session to validate.
 * 
 * @return: 1 if user is logged in, 0 if not (and sends error response).
 **/
int validate_authentication(ClientSession *client) {
    if (!client->is_authenticated) {
        char *response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return 0;
    }
    return 1;
}

/**
 * @function clean_username: Clean and normalize username from input.
 * 
 * @param input: Input string to clean.
 * @param output: Buffer to store cleaned result (must have sufficient size).
 * @param max_len: Maximum size of output buffer.
 * 
 * @return: 1 if successful, 0 if username is empty.
 **/
int clean_username(const char *input, char *output, size_t max_len) {
    if (!input || strlen(input) == 0) {
        return 0;
    }
    
    const char *src = input;
    
    // Skip leading spaces
    while (*src == ' ' || *src == '\t') src++;
    
    // Copy username
    size_t i = 0;
    while (*src && *src != ' ' && *src != '\r' && *src != '\n' && *src != '\t' && i < max_len - 1) {
        output[i++] = *src++;
    }
    output[i] = '\0';
    
    return (strlen(output) > 0) ? 1 : 0;
}

/**
 * @function get_user_id_by_username: Get user ID from username.
 * 
 * @param db_conn: Database connection.
 * @param username: Username to search for.
 * @param user_id_out: Pointer to store found user ID.
 * 
 * @return: 1 if user found, 0 if not found.
 **/
int get_user_id_by_username(PGconn *db_conn, const char *username, int *user_id_out) {
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            username);
    
    PGresult *res = execute_query_with_result(db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: User '%s' not found\n", username);
        if (res) PQclear(res);
        return 0;
    }
    
    *user_id_out = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found user '%s' with ID: %d\n", username, *user_id_out);
    PQclear(res);
    return 1;
}

/**
 * @function check_not_self: Check if user is trying to perform action on themselves.
 * 
 * @param client: Pointer to the client session.
 * @param target_user_id: Target user ID to check against.
 * @param error_context: Error message context to display if self-action detected.
 * 
 * @return: 1 if not self-action (OK), 0 if self-action detected (error sent).
 **/
int check_not_self(ClientSession *client, int target_user_id, const char *error_context) {
    if (target_user_id == client->user_id) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "BAD_REQUEST - %s", error_context);
        char *response = build_response(STATUS_UNDEFINED_ERROR, error_msg);
        server_send_response(client, response);
        free(response);
        return 0;
    }
    return 1;
}

/**
 * @function check_friendship_status: Check friendship status between two users.
 * 
 * @param db_conn: Database connection.
 * @param user_id1: First user ID.
 * @param user_id2: Second user ID.
 * @param status_filter: Status to filter by ("accepted", "pending", or NULL for all).
 * 
 * @return: 1 if relationship exists with specified status, 0 if not.
 **/
int check_friendship_status(PGconn *db_conn, int user_id1, int user_id2, const char *status_filter) {
    char query[512];
    
    if (status_filter) {
        snprintf(query, sizeof(query),
                "SELECT id FROM friends WHERE "
                "((user_id = %d AND friend_id = %d) OR (user_id = %d AND friend_id = %d)) "
                "AND status = '%s'",
                user_id1, user_id2, user_id2, user_id1, status_filter);
    } else {
        snprintf(query, sizeof(query),
                "SELECT id FROM friends WHERE "
                "((user_id = %d AND friend_id = %d) OR (user_id = %d AND friend_id = %d))",
                user_id1, user_id2, user_id2, user_id1);
    }
    
    PGresult *res = execute_query_with_result(db_conn, query);
    int exists = (res && PQntuples(res) > 0) ? 1 : 0;
    if (res) PQclear(res);
    
    return exists;
}

/**
 * @function send_error_response: Send error response to client.
 * 
 * @param client: Pointer to the client session.
 * @param status_code: HTTP-style status code for the error.
 * @param message: Error message to send.
 * 
 * @return: None (void function).
 **/
void send_error_response(ClientSession *client, int status_code, const char *message) {
    char *response = build_response(status_code, message);
    server_send_response(client, response);
    free(response);
}

// ==================== Friend Management Functions ====================

/**
 * @function handle_friend_request: Send a friend request to another user.
 * 
 * @param server: Pointer to Server structure managing database connection.
 * @param client: Pointer to the user session sending the request.
 * @param cmd: Pointer to parsed command containing target username.
 * 
 * @return: 0 if successful (sent via response).
 *         1 if error occurs (not logged in, invalid username, user does not exist, already friends, or request already pending).
 **/
void handle_friend_request(Server *server, ClientSession *client, ParsedCommand *cmd) {
    // Check if logged in
    if (!validate_authentication(client)) return;
    
    // Get and clean username from ParsedCommand->target_user
    char username_clean[256];
    if (!clean_username(cmd->target_user, username_clean, sizeof(username_clean))) {
        send_error_response(client, STATUS_UNDEFINED_ERROR, "Username required");
        return;
    }
    
    printf("DEBUG: Searching for user '%s'\n", username_clean);
    
    // Check if user exists
    int target_user_id;
    if (!get_user_id_by_username(server->db_conn, username_clean, &target_user_id)) {
        send_error_response(client, STATUS_USER_NOT_FOUND, "User does not exist");
        return;
    }
    
    // Cannot send friend request to yourself
    if (!check_not_self(client, target_user_id, "Cannot send friend request to yourself")) {
        return;
    }
    
    // Check if already friends
    if (check_friendship_status(server->db_conn, client->user_id, target_user_id, "accepted")) {
        send_error_response(client, STATUS_ALREADY_FRIEND, "Already friends");
        return;
    }
    
    // Check for pending friend request
    if (check_friendship_status(server->db_conn, client->user_id, target_user_id, "pending")) {
        send_error_response(client, STATUS_REQUEST_PENDING, "Friend request already pending");
        return;
    }
    
    // Create friend request
    char query[512];
    snprintf(query, sizeof(query),
            "INSERT INTO friends (user_id, friend_id, status, created_at) "
            "VALUES (%d, %d, 'pending', NOW())",
            client->user_id, target_user_id);
    
    if (!execute_query(server->db_conn, query)) {
        send_error_response(client, STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to send friend request");
        return;
    }
    
    // Send success response
    char success_msg[512];
    snprintf(success_msg, sizeof(success_msg),
            "Friend request sent to %s successfully", username_clean);
    char *response = build_response(STATUS_FRIEND_REQ_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    // Notify the target user (if online)
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

/**
 * @function handle_friend_pending: Get list of pending friend requests.
 * 
 * @param server: Pointer to Server structure managing database connection.
 * @param client: Pointer to the user session requesting the list.
 * @param cmd: Pointer to parsed command (not used).
 * 
 * @return: 0 if successful (sends list via response).
 *         1 if error occurs (not logged in or database error).
 **/
void handle_friend_pending(Server *server, ClientSession *client, ParsedCommand *cmd) {
    // Check if logged in
    if (!validate_authentication(client)) return;
    
    (void)cmd;  // Unused parameter
    
    // Query to get pending friend requests
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
        send_error_response(client, STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to fetch pending requests");
        return;
    }
    
    int num_pending = PQntuples(res);
    
    if (num_pending == 0) {
        PQclear(res);
        char *response = build_response(STATUS_FRIEND_PENDING_OK, "No pending friend requests");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Create response in table format
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
    
    char *response = build_response(STATUS_FRIEND_PENDING_OK, pending_list);
    server_send_response(client, response);
    free(response);
    
    printf("Sent %d pending requests to user %s\n", num_pending, client->username);
}

/**
 * @function handle_friend_accept: Accept a friend request from another user.
 * 
 * @param server: Pointer to Server structure managing database connection.
 * @param client: Pointer to the user session accepting the request.
 * @param cmd: Pointer to parsed command containing requester's username.
 * 
 * @return: 0 if successful (updates status to 'accepted').
 *         1 if error occurs (not logged in, invalid username, user does not exist, or no pending request).
 **/
void handle_friend_accept(Server *server, ClientSession *client, ParsedCommand *cmd) {
    // Check if logged in
    if (!validate_authentication(client)) return;
    
    // Get and clean username
    char username_clean[256];
    if (!clean_username(cmd->target_user, username_clean, sizeof(username_clean))) {
        send_error_response(client, STATUS_UNDEFINED_ERROR, "Username required");
        return;
    }
    
    printf("DEBUG: Accepting friend request from '%s'\n", username_clean);
    
    // Check if user exists
    int requester_user_id;
    if (!get_user_id_by_username(server->db_conn, username_clean, &requester_user_id)) {
        send_error_response(client, STATUS_USER_NOT_FOUND, "User does not exist");
        return;
    }
    
    // Check for pending request (requester_user_id sent to client->user_id)
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM friends WHERE "
            "user_id = %d AND friend_id = %d AND status = 'pending'",
            requester_user_id, client->user_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: No pending request from '%s' to current user\n", username_clean);
        if (res) PQclear(res);
        send_error_response(client, STATUS_NO_PENDING_REQUEST, "No pending friend request from this user");
        return;
    }
    
    int friend_request_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found pending request ID: %d\n", friend_request_id);
    PQclear(res);
    
    // Update status to 'accepted'
    snprintf(query, sizeof(query),
            "UPDATE friends SET status = 'accepted', created_at = NOW() "
            "WHERE id = %d",
            friend_request_id);
    
    if (!execute_query(server->db_conn, query)) {
        send_error_response(client, STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to accept friend request");
        return;
    }
    
    // Send success response
    char success_msg[512];
    snprintf(success_msg, sizeof(success_msg),
            "Friend request from %s accepted successfully", username_clean);
    char *response = build_response(STATUS_FRIEND_ACCEPT_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    // Notify the requester (if online)
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

/**
 * @function handle_friend_decline: Decline a friend request from another user.
 * 
 * @param server: Pointer to Server structure managing database connection.
 * @param client: Pointer to the user session declining the request.
 * @param cmd: Pointer to parsed command containing requester's username.
 * 
 * @return: 0 if successful (deletes the friend request).
 *         1 if error occurs (not logged in, invalid username, user does not exist, or no pending request).
 **/
void handle_friend_decline(Server *server, ClientSession *client, ParsedCommand *cmd) {
    // Check if logged in
    if (!validate_authentication(client)) return;
    
    // Get and clean username
    char username_clean[256];
    if (!clean_username(cmd->target_user, username_clean, sizeof(username_clean))) {
        send_error_response(client, STATUS_UNDEFINED_ERROR, "Username required");
        return;
    }
    
    printf("DEBUG: Declining friend request from '%s'\n", username_clean);
    
    // Check if user exists
    int requester_user_id;
    if (!get_user_id_by_username(server->db_conn, username_clean, &requester_user_id)) {
        send_error_response(client, STATUS_USER_NOT_FOUND, "User does not exist");
        return;
    }
    
    // Check for pending request (requester_user_id sent to client->user_id)
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM friends WHERE "
            "user_id = %d AND friend_id = %d AND status = 'pending'",
            requester_user_id, client->user_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        printf("DEBUG: No pending request from '%s' to current user\n", username_clean);
        if (res) PQclear(res);
        send_error_response(client, STATUS_NO_PENDING_REQUEST, "No pending friend request from this user");
        return;
    }
    
    int friend_request_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found pending request ID: %d\n", friend_request_id);
    PQclear(res);
    
    // Delete friend request (decline = delete)
    snprintf(query, sizeof(query),
            "DELETE FROM friends WHERE id = %d",
            friend_request_id);
    
    if (!execute_query(server->db_conn, query)) {
        send_error_response(client, STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to decline friend request");
        return;
    }
    
    // Send success response
    char success_msg[512];
    snprintf(success_msg, sizeof(success_msg),
            "Friend request from %s declined successfully", username_clean);
    char *response = build_response(STATUS_FRIEND_DECLINE_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    // Notify the requester (if online) - optional
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

/**
 * @function handle_friend_remove: Remove a friend from friend list.
 * 
 * @param server: Pointer to Server structure managing database connection.
 * @param client: Pointer to the user session removing the friend.
 * @param cmd: Pointer to parsed command containing friend's username to remove.
 * 
 * @return: 0 if successful (deletes the friendship relationship).
 *         1 if error occurs (not logged in, invalid username, user does not exist, or not friends).
 **/
void handle_friend_remove(Server *server, ClientSession *client, ParsedCommand *cmd) {
    // Check if logged in
    if (!validate_authentication(client)) return;
    
    // Get and clean username
    char username_clean[256];
    if (!clean_username(cmd->target_user, username_clean, sizeof(username_clean))) {
        send_error_response(client, STATUS_UNDEFINED_ERROR, "Username required");
        return;
    }
    
    printf("DEBUG: Removing friend '%s'\n", username_clean);
    
    // Check if user exists
    int friend_user_id;
    if (!get_user_id_by_username(server->db_conn, username_clean, &friend_user_id)) {
        send_error_response(client, STATUS_USER_NOT_FOUND, "User does not exist");
        return;
    }
    
    // Cannot remove yourself
    if (!check_not_self(client, friend_user_id, "Cannot remove yourself")) {
        return;
    }
    
    // Check if they are friends (status = 'accepted')
    if (!check_friendship_status(server->db_conn, client->user_id, friend_user_id, "accepted")) {
        send_error_response(client, STATUS_NOT_FRIEND, "You are not friends with this user");
        return;
    }
    
    // Get friendship_id to delete
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM friends WHERE "
            "((user_id = %d AND friend_id = %d) OR (user_id = %d AND friend_id = %d)) "
            "AND status = 'accepted'",
            client->user_id, friend_user_id, friend_user_id, client->user_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    int friendship_id = atoi(PQgetvalue(res, 0, 0));
    printf("DEBUG: Found friendship ID: %d\n", friendship_id);
    PQclear(res);
    
    // Delete friendship relationship
    snprintf(query, sizeof(query),
            "DELETE FROM friends WHERE id = %d",
            friendship_id);
    
    if (!execute_query(server->db_conn, query)) {
        send_error_response(client, STATUS_UNDEFINED_ERROR, "UNDEFINED_ERROR - Failed to remove friend");
        return;
    }
    
    // Send success response
    char success_msg[512];
    snprintf(success_msg, sizeof(success_msg),
            "Successfully removed %s from your friend list", username_clean);
    char *response = build_response(STATUS_FRIEND_REMOVE_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    // Notify the unfriended user (if online)
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

/**
 * @function handle_friend_list: Get list of all friends of current user.
 * 
 * @param server: Pointer to Server structure managing database connection.
 * @param client: Pointer to the user session requesting friend list.
 * 
 * @return: 0 if successful (sends friend list with online/offline status).
 *         1 if error occurs (not logged in or database error).
 **/
void handle_friend_list(Server *server, ClientSession *client) {
    // Check if logged in
    if (!validate_authentication(client)) return;
    
    // Query to get friend list (status = 'accepted')
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
        send_error_response(client, STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to fetch friend list");
        return;
    }
    
    int num_friends = PQntuples(res);
    
    if (num_friends == 0) {
        PQclear(res);
        char *response = build_response(STATUS_FRIEND_LIST_OK, "You don't have any friends yet");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Create response in table format
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
        
        // Convert is_online
        const char *status = "Offline";
        if (is_online_str && (is_online_str[0] == 't')) {
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
    
    char *response = build_response(STATUS_FRIEND_LIST_OK, friend_list);
    server_send_response(client, response);
    free(response);
    
    printf("Sent friend list (%d friends) to user %s\n", num_friends, client->username);
}