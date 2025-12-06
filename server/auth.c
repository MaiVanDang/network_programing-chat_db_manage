#include "../server/server.h"
#include "../database/database.h"
#include "../server/group.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <openssl/sha.h>

// ============================================================================
// TASK 3: Register & Manage account
// ============================================================================

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

int validate_password(const char *password) {
    if (!password) return 0;
    
    size_t len = strlen(password);
    if (len < 6 || len > MAX_PASSWORD_LENGTH) return 0;
    
    return 1;
}

void hash_password(const char *password, char *output) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)password, strlen(password), hash);
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[SHA256_DIGEST_LENGTH * 2] = '\0';
}

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

int update_user_status(PGconn *conn, int user_id, int is_online) {
    char query[256];
    snprintf(query, sizeof(query),
            "UPDATE users SET is_online = %s WHERE id = %d",
            is_online ? "TRUE" : "FALSE", user_id);
    
    return execute_query(conn, query);
}

// ============================================================================
// Command Handlers
// ============================================================================

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
        response = build_simple_response(STATUS_UNDEFINED_ERROR);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (!validate_username(cmd->username)) {
        response = build_simple_response(STATUS_INVALID_USERNAME);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (!validate_password(cmd->password)) {
        response = build_simple_response(STATUS_INVALID_PASSWORD);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (user_exists(server->db_conn, cmd->username)) {
        response = build_simple_response(STATUS_USERNAME_EXISTS);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (register_user(server->db_conn, cmd->username, cmd->password)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Registration successful for %s", cmd->username);
        response = build_response(STATUS_REGISTER_OK, msg);
        server_send_response(client, response);
        free(response);
        printf("? New user registered: %s\n", cmd->username);
    } else {
        response = build_simple_response(STATUS_DATABASE_ERROR);
        server_send_response(client, response);
        free(response);
    }
}

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
        response = build_simple_response(STATUS_UNDEFINED_ERROR);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (server_get_client_by_username(server, cmd->username)) {
        response = build_simple_response(STATUS_ALREADY_LOGGED_IN);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int user_id = verify_login(server->db_conn, cmd->username, cmd->password);
    
    if (user_id < 0) {
        if (user_exists(server->db_conn, cmd->username)) {
            response = build_simple_response(STATUS_WRONG_PASSWORD);
        } else {
            response = build_simple_response(STATUS_USER_NOT_FOUND);
        }
        server_send_response(client, response);
        free(response);
        return;
    }
    
    client->user_id = user_id;
    client->is_authenticated = 1;
    strncpy(client->username, cmd->username, MAX_USERNAME_LENGTH - 1);
    
    update_user_status(server->db_conn, user_id, 1);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Welcome %s", cmd->username);
    response = build_response(STATUS_LOGIN_OK, msg);
    server_send_response(client, response);
    free(response);
    
    printf("User logged in: %s (id=%d, fd=%d)\n", 
           cmd->username, user_id, client->socket_fd);
}

void handle_logout_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (!client->is_authenticated) {
        response = build_simple_response(STATUS_NOT_LOGGED_IN);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    update_user_status(server->db_conn, client->user_id, 0);
    
    printf("? User logged out: %s (id=%d, fd=%d)\n", 
           client->username, client->user_id, client->socket_fd);
    
    char username_copy[MAX_USERNAME_LENGTH];
    strncpy(username_copy, client->username, MAX_USERNAME_LENGTH - 1);
    
    client->user_id = -1;
    client->is_authenticated = 0;
    memset(client->username, 0, MAX_USERNAME_LENGTH);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "Goodbye %s", username_copy);
    response = build_response(STATUS_LOGOUT_OK, msg);
    server_send_response(client, response);
    free(response);
}

