#include "../server/server.h"
#include "../database/database.h"
#include "../helper/helper.h"
#include "../server/group.h"
#include "friend.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <openssl/sha.h>

// ============================================================================
// TASK 3: Register & Manage account
// ============================================================================

/**
 * @function validate_username: Validate username format. 
 * 
 * @param username: The username to validate.
 * 
 * @return: 1 if valid, 0 otherwise.
 **/
int validate_username(const char *username) {
    if (!username) return 0;
    
    size_t len = strlen(username);
    if (len < 3 || len > MAX_USERNAME_LENGTH) return 0;
    
    for (size_t i = 0; i < len; i++) {
        if (!isalnum(username[i]) && username[i] != '_') {
            return 0;
        }
    }
    
    return 1;
}

/**
 * @function validate_password: Validate password format.
 * 
 * @param password: The password to validate.
 * 
 * @return: 1 if valid, 0 otherwise.
 **/
int validate_password(const char *password) {
    if (!password) return 0;
    
    size_t len = strlen(password);
    if (len < 6 || len > MAX_PASSWORD_LENGTH) return 0;
    
    return 1;
}

/**
 * @function hash_password: Hash password using SHA-256.
 * 
 * @param password: The password to hash.
 * @param output: Buffer to store the resulting hash (must be at least 65 bytes
 * 
 * @return: None (void function).
 */
void hash_password(const char *password, char *output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)password, strlen(password), hash);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[SHA256_DIGEST_LENGTH * 2] = '\0';
}

/**
 * @function user_exists: Check if a username already exists in the database.
 * 
 * @param conn: Pointer to the database connection.
 * @param username: The username to check.
 * 
 * @return: 1 if exists, 0 otherwise.
 */
int user_exists(PGconn *conn, const char *username) {
    char query[256];
    snprintf(query, sizeof(query),
            "SELECT COUNT(*) FROM users WHERE username = '%s'",
            username);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (!res) return 0;
    
    int count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    return count > 0;
}

/**
 * @function register_user: Register a new user in the database.
 * 
 * @param conn: Pointer to the database connection.
 * @param username: The username for the new user.
 * @param password: The password for the new user.
 * 
 * @return: 1 if registration successful, 0 otherwise.
 */
int register_user(PGconn *conn, const char *username, const char *password) {
    if (!conn || !username || !password) return 0;
    
    char password_hash[SHA256_DIGEST_LENGTH * 2 + 1];
    hash_password(password, password_hash);
    
    char query[512];
    snprintf(query, sizeof(query),
            "INSERT INTO users (username, password_hash, is_online) "
            "VALUES ('%s', '%s', FALSE)",
            username, password_hash);
    
    return execute_query(conn, query);
}

/**
 * @function verify_login: Verify user login credentials.
 * 
 * @param conn: Pointer to the database connection.
 * @param username: The username to verify.
 * @param password: The password to verify.
 * 
 * @return: User ID if credentials are valid, -1 otherwise.
 */
int verify_login(PGconn *conn, const char *username, const char *password) {
    if (!conn || !username || !password) return -1;
    
    char password_hash[SHA256_DIGEST_LENGTH * 2 + 1];
    hash_password(password, password_hash);
    
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id, password_hash FROM users WHERE username = '%s'",
            username);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return -1; // User not found
    }
    
    int user_id = atoi(PQgetvalue(res, 0, 0));
    const char *stored_hash = PQgetvalue(res, 0, 1);
    
    int match = (strcmp(password_hash, stored_hash) == 0);
    PQclear(res);
    
    return match ? user_id : -1;
}

/**
 * @function update_user_status: Update user's online status.
 * 
 * @param conn: Pointer to the database connection.
 * @param user_id: The user ID to update.
 * @param is_online: 1 to set online, 0 to set offline.
 * 
 * @return: 1 if update successful, 0 otherwise.
 */
int update_user_status(PGconn *conn, int user_id, int is_online) {
    char query[256];
    snprintf(query, sizeof(query),
            "UPDATE users SET is_online = %s WHERE id = %d",
            is_online ? "TRUE" : "FALSE", user_id);
    
    return execute_query(conn, query);
}

/**
 * @function check_auth: Check if client is authenticated.
 * 
 * @param client: Pointer to the client session.
 * 
 * @return: true if authenticated, false otherwise (sends error response).
 **/
bool check_auth(ClientSession *client) {
    if (!client->is_authenticated) {
        char *response = build_response(STATUS_NOT_LOGGED_IN, 
            "Please login first");
        send_and_free(client, response);
        return false;
    }
    return true;
}
// ============================================================================
// Command Handlers
// ============================================================================

/**
 * @function handle_register_command: Handle user registration command.
 * 
 * @param server: Pointer to Server structure managing database connection.
 * @param client: Pointer to the client session requesting registration.
 * @param cmd: Pointer to parsed command containing registration details.
 * 
 * @return: None (void function).
 */
void handle_register_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (client->is_authenticated) {
        response = build_simple_response(STATUS_ALREADY_LOGGED_IN);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (cmd->param_count < 2) {
        response = build_response(STATUS_UNDEFINED_ERROR, "Username and password required");
        send_and_free(client, response);
        return;
    }
    
    if (!validate_username(cmd->username)) {
        response = build_response(STATUS_INVALID_USERNAME, "Username invalid");
        send_and_free(client, response);
        return;
    }
    
    if (!validate_password(cmd->password)) {
        response = build_response(STATUS_INVALID_PASSWORD, "Password invalid");
        send_and_free(client, response);
        return;
    }
    
    if (user_exists(server->db_conn, cmd->username)) {
        response = build_response(STATUS_USERNAME_EXISTS, "Username already exists");
        send_and_free(client, response);
        return;
    }
    
    if (register_user(server->db_conn, cmd->username, cmd->password)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Registration successful for %s", cmd->username);
        response = build_response(STATUS_REGISTER_OK, msg);
        send_and_free(client, response);
        printf("New user registered: %s\n", cmd->username);
    } else {
        response = build_response(STATUS_DATABASE_ERROR, "Failed to register user");
        send_and_free(client, response);
    }
}

/**
 * @function handle_login_command: Handle user login command.
 * 
 * @param server: Pointer to Server structure managing database connection.
 * @param client: Pointer to the client session requesting login.
 * @param cmd: Pointer to parsed command containing login details.
 * 
 * @return: None (void function).
 */
void handle_login_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (client->is_authenticated) {
        response = build_simple_response(STATUS_ALREADY_LOGGED_IN);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (cmd->param_count < 2) {
        response = build_response(STATUS_UNDEFINED_ERROR, "Username and password required");
        send_and_free(client, response);
        return;
    }
    
    if (server_get_client_by_username(server, cmd->username)) {
        response = build_response(STATUS_ALREADY_LOGGED_IN, "User already logged in from another session");
        send_and_free(client, response);
        return;
    }
    
    int user_id = verify_login(server->db_conn, cmd->username, cmd->password);
    
    if (user_id < 0) {
        if (user_exists(server->db_conn, cmd->username)) {
            response = build_response(STATUS_WRONG_PASSWORD, "Incorrect password");
        } else {
            response = build_response(STATUS_USER_NOT_FOUND, "User does not exist");
        }
        send_and_free(client, response);
        return;
    }
    
    client->user_id = user_id;
    client->is_authenticated = 1;
    strncpy(client->username, cmd->username, MAX_USERNAME_LENGTH - 1);
    
    update_user_status(server->db_conn, user_id, 1);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Welcome %s", cmd->username);
    response = build_response(STATUS_LOGIN_OK, msg);
    send_and_free(client, response);
    
    printf("User logged in: %s (id=%d, fd=%d)\n", 
           cmd->username, user_id, client->socket_fd);
    send_pending_notifications(server, client);
}

/**
 * @function handle_logout_command: Handle user logout command.
 * 
 * @param server: Pointer to Server structure managing database connection.
 * @param client: Pointer to the client session requesting logout.
 * @param cmd: Pointer to parsed command (not used here).
 * 
 * @return: None (void function).
 */
void handle_logout_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (!check_auth(client)) return;
    
    update_user_status(server->db_conn, client->user_id, 0);
    
    printf("User logged out: %s (id=%d, fd=%d)\n", 
           client->username, client->user_id, client->socket_fd);
    
    char username_copy[MAX_USERNAME_LENGTH];
    strncpy(username_copy, client->username, MAX_USERNAME_LENGTH - 1);
    
    client->user_id = -1;
    client->is_authenticated = 0;
    memset(client->username, 0, MAX_USERNAME_LENGTH);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Goodbye %s", username_copy);
    response = build_response(STATUS_LOGOUT_OK, msg);
    send_and_free(client, response);
}
