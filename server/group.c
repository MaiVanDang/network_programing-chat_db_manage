#include "server.h"
#include "auth.h"
#include "../database/database.h"
#include "../helper/helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// ============================================================================
// Common Helper Functions
// ============================================================================

/**
 * @function is_group_owner: Check if user is group owner
 * 
 * @param conn: Database connection
 * @param group_id: Group ID
 * @param user_id: User ID
 * 
 * @return true if user is owner, false otherwise
 */ 
int is_group_owner(PGconn *conn, int group_id, int user_id) {
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT COUNT(*) FROM group_members "
            "WHERE group_id = %d AND user_id = %d AND role = 'owner'",
            group_id, user_id);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (!res) return 0;
    
    int count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    return count > 0;
}

/**
 * @function find_group_id: Find group ID by group name
 * 
 * @param conn: Database connection
 * @param group_name: Group name
 * 
 * @return Group ID if found, -1 otherwise
 */
int find_group_id(PGconn *conn, const char *group_name) {
    char query[256];

    snprintf(query, sizeof(query),
             "SELECT id FROM groups WHERE group_name = '%s'",
             group_name);

    PGresult *res = execute_query_with_result(conn, query);
    if (!res) return -1;

    if (PQntuples(res) == 0) {
        PQclear(res);
        return -1;
    }

    int group_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    return group_id;
}

/**
 * @funtion store_offline_notification: Store offline notification for user
 * 
 * @param db_conn: Database connection
 * @param user_id: Target user ID
 * @param group_id: Group ID
 * @param owner: Owner username
 * @param group_name: Group name
 * @param status: "added" or "kicked"
 * 
 * @return true on success, false on failure
 */
bool store_offline_notification(PGconn *db_conn, int user_id, int group_id, 
                                const char *owner, const char *group_name, 
                                const char *status) {
    if (!db_conn) return false;
    
    char query[1024];
    char escaped_owner[256];
    char escaped_group_name[256];

    PQescapeStringConn(db_conn, escaped_owner, owner, strlen(owner), NULL);
    PQescapeStringConn(db_conn, escaped_group_name, group_name, 
                       strlen(group_name), NULL);
    
    snprintf(query, sizeof(query),
            "INSERT INTO offline_notifications "
            "(user_id, notification_type, group_id, sender_username, message, created_at) "
            "VALUES (%d, 'GROUP_INVITE', %d, '%s', "
            "'You have been %s to group ''%s'' by %s', NOW())",
            user_id, group_id, escaped_owner, status, escaped_group_name, escaped_owner);
    
    bool result = execute_query(db_conn, query);
    printf("%s offline notification for user_id=%d\n", 
           result ? "Stored" : "Failed to store", user_id);
    
    return result;
}

/**
 * @funtion send_notification: Send real-time notification or store offline
 * 
 * @param server: Server instance
 * @param target_user_id: Target user ID
 * @param username: Target username
 * @param group_id: Group ID
 * @param group_name: Group name
 * @param sender: Sender username
 * @param message: Notification message
 * @param status_code: Notification status code
 * @param notification_format: Notification format string
 * @param offline_status: "added" or "kicked"
 * 
 * @return void
 */
void send_notification(Server *server, int target_user_id, 
                               const char *username, int group_id,
                               const char *group_name, const char *sender,
                               const char *message, int status_code,
                               const char *notification_format,
                               const char *offline_status) {
    ClientSession *target = server_get_client_by_username(server, username);
    
    if (target && target->is_authenticated) {
        char notification[1024];
        snprintf(notification, sizeof(notification), notification_format,
                group_id, group_name, sender, message);
        
        char *response = build_response(status_code, notification);
        
        if (server_send_response(target, response) > 0) {
            printf("Real-time notification sent to '%s'\n", username);
        } else {
            printf("Failed to send, storing offline for '%s'\n", username);
            store_offline_notification(server->db_conn, target_user_id,
                                      group_id, sender, group_name, offline_status);
        }
        free(response);
    } else {
        printf("User '%s' offline, storing notification\n", username);
        store_offline_notification(server->db_conn, target_user_id,
                                  group_id, sender, group_name, offline_status);
    }
}

/**
 * @function validate_and_get_group: Validate group name and get group ID
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param group_name: Group name
 * 
 * @return Group ID if valid, -1 otherwise
 */
int validate_and_get_group(Server *server, ClientSession *client, 
                                   const char *group_name) {
    if (!group_name) {
        char *response = build_response(STATUS_INVALID_GROUP_NAME, 
            "Invalid group name");
        send_and_free(client, response);
        return -1;
    }
    
    int group_id = find_group_id(server->db_conn, group_name);
    if (group_id < 0) {
        char *response = build_response(STATUS_GROUP_NOT_FOUND, 
            "Group does not exist");
        send_and_free(client, response);
        return -1;
    }
    
    return group_id;
}

/**
 * @function check_owner_permission: Check if client is group owner
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param group_id: Group ID
 * @param error_msg: Error message to send if not owner
 * 
 * @return true if owner, false otherwise
 */
bool check_owner_permission(Server *server, ClientSession *client, 
                                    int group_id, const char *error_msg) {
    if (!is_group_owner(server->db_conn, group_id, client->user_id)) {
        char *response = build_response(STATUS_NOT_GROUP_OWNER, error_msg);
        send_and_free(client, response);
        return false;
    }
    return true;
}

/**
 * @function user_exists: Check if user exists by username
 * 
 * @param conn: Database connection
 * @param username: Username
 * 
 * @return true if exists, false otherwise
 */
int get_user_id(PGconn *conn, const char *username) {
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'", username);
    
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
 * @function validate_target_user: Validate target user by username
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param username: Target username
 * 
 * @return Target user ID if valid, -1 otherwise
 */
static int validate_target_user(Server *server, ClientSession *client, 
                                 const char *username) {
    if (!user_exists(server->db_conn, username)) {
        char *response = build_response(STATUS_USER_NOT_FOUND, 
            "User does not exist");
        send_and_free(client, response);
        return -1;
    }
    
    return get_user_id(server->db_conn, username);
}

/**
 * @function get_group_name: Get group name by group ID
 * 
 * @param conn: Database connection
 * @param group_id: Group ID
 * @param buffer: Buffer to store group name
 * @param size: Size of buffer
 * 
 * @return true if found, false otherwise
 */
bool get_group_name(PGconn *conn, int group_id, char *buffer, size_t size) {
    char query[256];
    snprintf(query, sizeof(query),
            "SELECT group_name FROM groups WHERE id = %d", group_id);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (res && PQntuples(res) > 0) {
        strncpy(buffer, PQgetvalue(res, 0, 0), size - 1);
        buffer[size - 1] = '\0';
        PQclear(res);
        return true;
    }
    if (res) PQclear(res);
    
    strncpy(buffer, "Unknown Group", size - 1);
    return false;
}

// ============================================================================
// Validation group 
// ============================================================================

/**
 * @function is_in_group: Check if user is in group
 * @param conn: Database connection
 * @param group_id: Group ID
 * @param user_id: User ID
 * 
 * @return true if user is in group, false otherwise
 */
int is_in_group(PGconn *conn, int group_id, int user_id) {
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT COUNT(*) FROM group_members "
            "WHERE group_id = %d AND user_id = %d",
            group_id, user_id);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (!res) return 0;
    
    int count = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    return count > 0;
}

/**
 * @function create_join_request: Create a join request for a group
 * 
 * @param conn: Database connection
 * @param group_id: Group ID
 * @param user_id: User ID
 * 
 * @return 0 on success, -1 on failure, -2 if pending request exists
 */
int create_join_request(PGconn *conn, int group_id, int user_id) {
    if (!conn) return -1;
    
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT status FROM group_join_requests "
            "WHERE group_id = %d AND user_id = %d", group_id, user_id);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (res && PQntuples(res) > 0) {
        const char *status = PQgetvalue(res, 0, 0);
        PQclear(res);
        
        if (strcmp(status, "pending") == 0) return -2;
        
        snprintf(query, sizeof(query),
                "DELETE FROM group_join_requests "
                "WHERE group_id = %d AND user_id = %d", group_id, user_id);
        execute_query(conn, query);
    } else if (res) {
        PQclear(res);
    }
    
    snprintf(query, sizeof(query),
            "INSERT INTO group_join_requests (group_id, user_id, status) "
            "VALUES (%d, %d, 'pending')", group_id, user_id);
    
    return execute_query(conn, query) ? 0 : -1;
}

/**
 * @function get_group_owner_id: Get group owner user ID
 * 
 * @param conn: Database connection
 * @param group_id: Group ID
 * 
 * @return Owner user ID, -1 if not found
 */
int get_group_owner_id(PGconn *conn, int group_id) {
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT user_id FROM group_members "
            "WHERE group_id = %d AND role = 'owner'", group_id);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return -1;
    }
    
    int owner_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    return owner_id;
}

/**
 * @function get_username_by_id: Get username by user ID
 * 
 * @param conn: Database connection
 * @param user_id: User ID
 * 
 * @return Username string (must be freed), NULL if not found
 */
char* get_username_by_id(PGconn *conn, int user_id) {
    char query[256];
    snprintf(query, sizeof(query),
            "SELECT username FROM users WHERE id = %d", user_id);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return NULL;
    }
    
    char *username = strdup(PQgetvalue(res, 0, 0));
    PQclear(res);
    return username;
}

/**
 * @function store_join_request_notification: Store join request notification
 * 
 * @param db_conn: Database connection
 * @param owner_id: Group owner user ID
 * @param group_id: Group ID
 * @param requester: Requester username
 * @param group_name: Group name
 * 
 * @return true on success, false on failure
 */
bool store_join_request_notification(PGconn *db_conn, int owner_id, int group_id,
                                     const char *requester, const char *group_name) {
    if (!db_conn) return false;
    
    char query[1024];
    char escaped_requester[256];
    char escaped_group_name[256];
    
    PQescapeStringConn(db_conn, escaped_requester, requester, strlen(requester), NULL);
    PQescapeStringConn(db_conn, escaped_group_name, group_name, strlen(group_name), NULL);
    
    snprintf(query, sizeof(query),
            "INSERT INTO offline_notifications "
            "(user_id, notification_type, group_id, sender_username, message, created_at) "
            "VALUES (%d, 'GROUP_JOIN_REQUEST', %d, '%s', "
            "'%s wants to join group ''%s''', NOW())",
            owner_id, group_id, escaped_requester, escaped_requester, escaped_group_name);
    
    return execute_query(db_conn, query);
}

// ============================================================================
// TASK 4: Create group
// ============================================================================

/**
 * @function create_group: Create a new group
 * 
 * @param conn: Database connection
 * @param group_name: Group name
 * @param creator_id: Creator user ID
 * 
 * @return Group ID if successful, -1 on failure, -2 if name exists
 */
int create_group(PGconn *conn, const char *group_name, int creator_id) {
    if (!conn || !group_name || creator_id <= 0) return -1;
    
    char query[1024];
    snprintf(query, sizeof(query),
            "SELECT COUNT(*) FROM groups WHERE group_name = '%s'", group_name);
    
    PGresult *res = execute_query_with_result(conn, query);
    if (res) {
        int count = atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        if (count > 0) return -2;
    }
    
    snprintf(query, sizeof(query),
            "INSERT INTO groups (group_name, creator_id) "
            "VALUES ('%s', %d) RETURNING id", group_name, creator_id);
    
    res = execute_query_with_result(conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return -1;
    }
    
    int group_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    snprintf(query, sizeof(query),
            "INSERT INTO group_members (group_id, user_id, role) "
            "VALUES (%d, %d, 'owner')", group_id, creator_id);
    
    if (!execute_query(conn, query)) {
        snprintf(query, sizeof(query), "DELETE FROM groups WHERE id = %d", group_id);
        execute_query(conn, query);
        return -1;
    }
    
    return group_id;
}

/**
 * @function handle_group_create_command: Handle group creation command
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_group_create_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (!check_auth(client)) return;
    
    if (cmd->param_count < 1 || strlen(cmd->group_name) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, 
            "Group name required");
        send_and_free(client, response);
        return;
    }
    
    if (strlen(cmd->group_name) < 3 || strlen(cmd->group_name) > 50) {
        response = build_response(STATUS_UNDEFINED_ERROR, 
            "Group name must be 3-50 characters");
        send_and_free(client, response);
        return;
    }
    
    // Create group
    int group_id = create_group(server->db_conn, cmd->group_name, client->user_id);
    
    if (group_id == -2) {
        response = build_response(STATUS_GROUP_EXISTS,
            "Group name already exists");
        send_and_free(client, response);
        return;
    }
    
    if (group_id < 0) {
        response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to create group");
        send_and_free(client, response);
        return;
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "Group '%s' created successfully with ID: %d", 
             cmd->group_name, group_id);
    response = build_response(STATUS_GROUP_CREATE_OK, msg);
    send_and_free(client, response);
    
    printf("Group created: %s (id=%d) by %s\n", 
           cmd->group_name, group_id, client->username);
}

// ============================================================================
// TASK 5: Add user to group 
// ============================================================================

/**
 * @function add_user_to_group: Add user to group as member
 * 
 * @param conn: Database connection
 * @param group_id: Group ID
 * @param user_id: User ID
 * 
 * @return true on success, false on failure
 */
int add_user_to_group(PGconn *conn, int group_id, int user_id) {
    char query[512];
    snprintf(query, sizeof(query),
            "INSERT INTO group_members (group_id, user_id, role) "
            "VALUES (%d, %d, 'member')", group_id, user_id);
    
    return execute_query(conn, query);
}

/**
 * @function handle_group_invite_command: Handle group invite command
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_group_invite_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (!check_auth(client)) return;
    
    if (cmd->param_count < 2) {
        response = build_response(STATUS_UNDEFINED_ERROR, 
            "Group name and username required");
        send_and_free(client, response);
        return;
    }
    
    int group_id = validate_and_get_group(server, client, cmd->group_name);
    if (group_id < 0) return;
    
    if (!check_owner_permission(server, client, group_id,
            "Only group owner can invite members")) return;
    
    int target_user_id = validate_target_user(server, client, cmd->target_user);
    if (target_user_id < 0) return;
    
    if (is_in_group(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_ALREADY_IN_GROUP, 
            "User already in group");
        send_and_free(client, response);
        return;
    }
    
    if (!add_user_to_group(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to add user to group");
        send_and_free(client, response);
        return;
    }

    // Get group info for notification
    char group_name[128];
    get_group_name(server->db_conn, group_id, group_name, sizeof(group_name));
    
    char msg[512];
    snprintf(msg, sizeof(msg), "User '%s' has been added to group '%s'", 
            cmd->target_user, group_name);
    response = build_response(STATUS_GROUP_INVITE_OK, msg);
    send_and_free(client, response);
    
    printf("User '%s' added to group '%s' by '%s'\n", 
           cmd->target_user, group_name, client->username);
    
    printf("User '%s' added to group '%s' by '%s'\n", 
           cmd->target_user, group_name, client->username);
    
    // Send notification
    char notif_format[512];
    snprintf(notif_format, sizeof(notif_format),
            "GROUP_INVITE_NOTIFICATION group_id=%%d group_name=\"%%s\" "
            "invited_by=\"%%s\" message=\"%%s\"");
    
    char notif_msg[256];
    snprintf(notif_msg, sizeof(notif_msg), 
            "You have been added to group '%s' by %s", group_name, client->username);
    
    send_notification(server, target_user_id, cmd->target_user, group_id,
                        group_name, client->username, notif_msg,
                        STATUS_GROUP_INVITE_NOTIFICATION, notif_format, "added");
}

// ============================================================================
// TASK 6: Remove user from group
// ============================================================================

/**
 * @function remove_user_from_group: Remove user from group
 * 
 * @param conn: Database connection
 * @param group_id: Group ID
 * @param user_id: User ID
 * 
 * @return true on success, false on failure
 */
int remove_user_from_group(PGconn *conn, int group_id, int user_id) {
    char query[512];
    snprintf(query, sizeof(query),
            "DELETE FROM group_members WHERE group_id = %d AND user_id = %d",
            group_id, user_id);
    
    return execute_query(conn, query);
}

/**
 * @function handle_group_kick_command: Handle group kick command
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_group_kick_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (!check_auth(client)) return;
    
    // Check parameters
    if (cmd->param_count < 2) {
        response = build_response(STATUS_UNDEFINED_ERROR, "Group name and username required");
        send_and_free(client, response);
        return;
    }
    
    int group_id = validate_and_get_group(server, client, cmd->group_name);
    if (group_id < 0) return;
    
    if (!check_owner_permission(server, client, group_id,
            "Only group owner can kick members")) return;
    
    int target_user_id = validate_target_user(server, client, cmd->target_user);
    if (target_user_id < 0) return;
    
    // Check if target is in group
    if (!is_in_group(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_NOT_IN_GROUP, "User not in group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Cannot kick owner
    if (is_group_owner(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_CANNOT_KICK_OWNER, "Cannot kick group owner");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Remove user from group
    if (!remove_user_from_group(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_DATABASE_ERROR, "Failed to kick user from group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char group_name[128];
    get_group_name(server->db_conn, group_id, group_name, sizeof(group_name));

    // Success
    char msg[256];
    snprintf(msg, sizeof(msg), "User '%s' kicked from group '%s' successfully", cmd->target_user, group_name);
    response = build_response(STATUS_GROUP_KICK_OK, msg);
    send_and_free(client, response);
    
    printf("User %s kicked from group %s by %s\n", 
           cmd->target_user, group_name, client->username);
    
    char notif_format[512];
    snprintf(notif_format, sizeof(notif_format),
            "GROUP_KICK_NOTIFICATION group_id=%%d group_name=\"%%s\" "
            "kicked_by=\"%%s\" message=\"%%s\"");
    
    char notif_msg[256];
    snprintf(notif_msg, sizeof(notif_msg), 
            "You have been kicked from group '%s' by %s", group_name, client->username);
    
    send_notification(server, target_user_id, cmd->target_user, group_id,
                     group_name, client->username, notif_msg,
                     STATUS_GROUP_KICK_NOTIFICATION, notif_format, "kicked");
}

// ============================================================================
// TASK 7: Leave group
// ============================================================================

/**
 * @function handle_group_leave_command: Handle group leave command
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_group_leave_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (!check_auth(client)) return;
    
    if (cmd->param_count < 1) {
        response = build_response(STATUS_UNDEFINED_ERROR, 
            "Group name required");
        send_and_free(client, response);
        return;
    }
    
    int group_id = validate_and_get_group(server, client, cmd->group_name);
    if (group_id < 0) return;
    
    if (!is_in_group(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_IN_GROUP, 
            "You are not in this group");
        send_and_free(client, response);
        return;
    }
    
    if (is_group_owner(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_GROUP_OWNER, 
            "Owner cannot leave group. "
            "Transfer ownership or delete group first");
        send_and_free(client, response);
        return;
    }
    
    if (!remove_user_from_group(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to leave group");
        send_and_free(client, response);
        return;
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "You left group '%s' successfully", cmd->group_name);
    response = build_response(STATUS_GROUP_LEAVE_OK, msg);
    send_and_free(client, response);
    
    printf("User %s left group %s\n", client->username, cmd->group_name);
}

// ============================================================================
// TASK 8: Join group with approval
// ============================================================================

/**
 * @function handle_group_join_command: Handle group join command
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_group_join_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (!check_auth(client)) return;
    
    if (cmd->param_count < 1) {
        response = build_response(STATUS_UNDEFINED_ERROR, 
            "Group name required");
        send_and_free(client, response);
        return;
    }
    
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id, group_name FROM groups WHERE group_name = '%s'", 
            cmd->group_name);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        response = build_response(STATUS_GROUP_NOT_FOUND, 
            "Group does not exist");
        send_and_free(client, response);
        return;
    }
    
    int group_id = atoi(PQgetvalue(res, 0, 0));
    char group_name[128];
    strncpy(group_name, PQgetvalue(res, 0, 1), sizeof(group_name) - 1);
    group_name[sizeof(group_name) - 1] = '\0';
    PQclear(res);
    
    if (is_in_group(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_ALREADY_IN_GROUP, 
            "You are already a member");
        send_and_free(client, response);
        return;
    }
    
    int result = create_join_request(server->db_conn, group_id, client->user_id);
    
    if (result == -2) {
        response = build_response(STATUS_REQUEST_PENDING, 
            "You already have a pending join request for this group");
        send_and_free(client, response);
        return;
    }
    
    if (result == -1) {
        response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to create join request");
        send_and_free(client, response);
        return;
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), 
            "Join request sent for group '%s'. Waiting for owner approval.", group_name);
    response = build_response(STATUS_JOIN_REQUEST_SENT, msg);
    send_and_free(client, response);
    
    printf("User '%s' requested to join group '%s'\n", client->username, group_name);
    
    int owner_id = get_group_owner_id(server->db_conn, group_id);
    if (owner_id <= 0) return;
    
    char *owner_username = get_username_by_id(server->db_conn, owner_id);
    if (!owner_username) return;
        
    ClientSession *owner = server_get_client_by_username(server, owner_username);
    
    if (owner && owner->is_authenticated) {
        char notification[1024];
        snprintf(notification, sizeof(notification),
                "GROUP_JOIN_REQUEST_NOTIFICATION group_id=%d group_name=\"%s\" "
                "requester=\"%s\" message=\"%s wants to join group '%s'\"",
                group_id, group_name, client->username, client->username, group_name);
        
        char *notify_response = build_response(STATUS_GROUP_JOIN_REQUEST_NOTIFICATION, 
                                              notification);
        
        if (server_send_response(owner, notify_response) > 0) {
            printf("Join request notification sent to owner '%s'\n", owner_username);
        } else {
            store_join_request_notification(server->db_conn, owner_id, 
                                          group_id, client->username, group_name);
        }
        free(notify_response);
    } else {
        store_join_request_notification(server->db_conn, owner_id,
                                       group_id, client->username, group_name);
    }
    
    free(owner_username);
}

/**
 * @function validate_join_request: Validate join request for approval/rejection
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * @param group_id_out: Output group ID
 * @param requester_id_out: Output requester user ID
 * @param group_name_out: Output group name
 * 
 * @return 0 on success, -1 on failure
 */
int validate_join_request(Server *server, ClientSession *client, 
                                  ParsedCommand *cmd, int *group_id_out,
                                  int *requester_id_out, char *group_name_out) {
    if (cmd->param_count < 2) {
        char *response = build_response(STATUS_UNDEFINED_ERROR, 
            "Group name and username required");
        send_and_free(client, response);
        return -1;
    }
    
    int group_id = validate_and_get_group(server, client, cmd->group_name);
    if (group_id < 0) return -1;
    
    if (!check_owner_permission(server, client, group_id,
            "Only group owner can approve/reject requests")) return -1;
    
    int requester_id = get_user_id(server->db_conn, cmd->target_user);
    if (requester_id < 0) {
        char *response = build_response(STATUS_USER_NOT_FOUND, 
            "User does not exist");
        send_and_free(client, response);
        return -1;
    }
    
    // Check if request exists
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT status FROM group_join_requests "
            "WHERE group_id = %d AND user_id = %d",
            group_id, requester_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        char *response = build_response(STATUS_NO_PENDING_REQUEST, 
            "No pending join request from this user");
        send_and_free(client, response);
        return -1;
    }
    
    const char *status = PQgetvalue(res, 0, 0);
    if (strcmp(status, "pending") != 0) {
        PQclear(res);
        char *response = build_response(STATUS_NO_PENDING_REQUEST, 
            "Request already processed");
        send_and_free(client, response);
        return -1;
    }
    PQclear(res);
    
    // Get group name
    get_group_name(server->db_conn, group_id, group_name_out, 128);
    
    *group_id_out = group_id;
    *requester_id_out = requester_id;
    return 0;
}

/**
 * @function update_request_status: Update join request status
 * 
 * @param conn: Database connection
 * @param group_id: Group ID
 * @param user_id: User ID
 * @param status: New status ("approved" or "rejected")
 */
void update_request_status(PGconn *conn, int group_id, int user_id, 
                                   const char *status) {
    char query[512];
    snprintf(query, sizeof(query),
            "UPDATE group_join_requests SET status = '%s' "
            "WHERE group_id = %d AND user_id = %d",
            status, group_id, user_id);
    execute_query(conn, query);
}

/**
 * @function handle_group_approve_command: Handle group join approval command
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_group_approve_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    if (!check_auth(client)) return;
    
    int group_id, requester_id;
    char group_name[128];
    
    if (validate_join_request(server, client, cmd, &group_id, 
                              &requester_id, group_name) < 0) return;
    
    // Add user to group
    if (!add_user_to_group(server->db_conn, group_id, requester_id)) {
        char *response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to add user");
        send_and_free(client, response);
        return;
    }
    
    // Update request status
    update_request_status(server->db_conn, group_id, requester_id, "approved");
    
    // Send success to owner
    char msg[256];
    snprintf(msg, sizeof(msg), "User '%s' approved to join group '%s'", 
            cmd->target_user, group_name);
    char *response = build_response(STATUS_GROUP_APPROVE_OK, msg);
    send_and_free(client, response);
    
    printf("Owner '%s' approved '%s' to join group '%s'\n",
           client->username, cmd->target_user, group_name);
    
    // Notify requester
    ClientSession *requester = server_get_client_by_username(server, cmd->target_user);
    
    if (requester && requester->is_authenticated) {
        char notification[512];
        snprintf(notification, sizeof(notification),
                "GROUP_JOIN_APPROVED_NOTIFICATION group_id=%d group_name=\"%s\" "
                "message=\"Your request to join group '%s' has been approved!\"",
                group_id, group_name, group_name);
        
        char *notify_response = build_response(STATUS_GROUP_JOIN_APPROVED, notification);
        server_send_response(requester, notify_response);
        free(notify_response);
        
        printf("Approval notification sent to '%s'\n", cmd->target_user);
    } else {
        store_offline_notification(server->db_conn, requester_id, group_id,
                                  client->username, group_name, "approved to join");
    }
}

/**
 * @function handle_group_reject_command: Handle group join rejection command
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_group_reject_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    if (!check_auth(client)) return;
    
    int group_id, requester_id;
    char group_name[128];
    
    if (validate_join_request(server, client, cmd, &group_id, 
                              &requester_id, group_name) < 0) return;
    
    // Update request status
    update_request_status(server->db_conn, group_id, requester_id, "rejected");
    
    // Send success to owner
    char msg[256];
    snprintf(msg, sizeof(msg), "Join request from '%s' rejected", cmd->target_user);
    char *response = build_response(STATUS_GROUP_REJECT_OK, msg);
    send_and_free(client, response);
    
    printf("Owner '%s' rejected '%s' from joining group '%s'\n",
           client->username, cmd->target_user, group_name);
    
    // Notify requester
    ClientSession *requester = server_get_client_by_username(server, cmd->target_user);
    
    if (requester && requester->is_authenticated) {
        char notification[512];
        snprintf(notification, sizeof(notification),
                "GROUP_JOIN_REJECTED_NOTIFICATION group_id=%d group_name=\"%s\" "
                "message=\"Your request to join group '%s' has been rejected\"",
                group_id, group_name, group_name);
        
        char *notify_response = build_response(STATUS_GROUP_JOIN_REJECTED, notification);
        server_send_response(requester, notify_response);
        free(notify_response);
        
        printf("Rejection notification sent to '%s'\n", cmd->target_user);
    } else {
        store_offline_notification(server->db_conn, requester_id, group_id,
                                  client->username, group_name, "rejected from");
    }
}

/**
 * @function handle_list_join_requests_command: Handle listing join requests command
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_list_join_requests_command(Server *server, ClientSession *client, 
                                       ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    if (!check_auth(client)) return;
    
    if (cmd->param_count < 1) {
        char *response = build_response(STATUS_UNDEFINED_ERROR, 
            "Group name required");
        send_and_free(client, response);
        return;
    }
    
    int group_id = validate_and_get_group(server, client, cmd->group_name);
    if (group_id < 0) return;
    
    if (!check_owner_permission(server, client, group_id,
            "Only owner can view join requests")) return;
    
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT u.username, gjr.created_at "
            "FROM group_join_requests gjr "
            "JOIN users u ON gjr.user_id = u.id "
            "WHERE gjr.group_id = %d AND gjr.status = 'pending' "
            "ORDER BY gjr.created_at ASC",
            group_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) {
        char *response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to fetch requests");
        send_and_free(client, response);
        return;
    }
    
    int count = PQntuples(res);
    
    if (count == 0) {
        char *response = build_response(STATUS_MSG_OK, "No pending join requests");
        send_and_free(client, response);
        PQclear(res);
        return;
    }
    
    char msg[2048] = "Pending join requests:\n";
    for (int i = 0; i < count; i++) {
        const char *username = PQgetvalue(res, i, 0);
        const char *time = PQgetvalue(res, i, 1);
        
        char line[256];
        snprintf(line, sizeof(line), "%d. %s (requested at: %s)\n", 
                i + 1, username, time);
        strcat(msg, line);
    }
    
    PQclear(res);
    
    char *response = build_response(STATUS_MSG_OK, msg);
    send_and_free(client, response);
}

// ============================================================================
// Group Messaging System
// ============================================================================

/**
 * @function is_user_in_group_messaging: Check if user is in messaging mode for group
 * 
 * @param db_conn: Database connection
 * @param user_id: User ID
 * @param group_id: Group ID
 * 
 * @return 1 if in messaging mode, 0 otherwise
 */
int is_user_in_group_messaging(PGconn *db_conn, int user_id, int group_id) {
    char query[256];
    snprintf(query, sizeof(query),
            "SELECT is_messaging FROM group_members "
            "WHERE user_id = %d AND group_id = %d",
            user_id, group_id);
    
    PGresult *res = execute_query_with_result(db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return 0;
    }
    
    const char *is_messaging = PQgetvalue(res, 0, 0);
    int result = (strcmp(is_messaging, "t") == 0);
    PQclear(res);
    
    return result;
}

/**
 * @function set_group_messaging_status: Set user's messaging status for group
 * 
 * @param db_conn: Database connection
 * @param user_id: User ID
 * @param group_id: Group ID
 * @param is_messaging: 1 to enable messaging, 0 to disable
 * 
 * @return 1 on success, 0 on failure
 */
int set_group_messaging_status(PGconn *db_conn, int user_id, int group_id, int is_messaging) {
    char query[256];
    snprintf(query, sizeof(query),
            "UPDATE group_members SET is_messaging = %s "
            "WHERE user_id = %d AND group_id = %d",
            is_messaging ? "TRUE" : "FALSE", user_id, group_id);
    
    return execute_query(db_conn, query);
}

/**
 * @function broadcast_group_message: Broadcast message to group members
 * 
 * @param server: Server instance
 * @param group_id: Group ID
 * @param group_name: Group name
 * @param sender_username: Sender's username
 * @param sender_id: Sender's user ID
 * @param message: Message content
 * @param message_id: Message ID
 * 
 * @return void
 */
void broadcast_group_message(Server *server, int group_id, const char *group_name, 
                             const char *sender_username, int sender_id,
                             const char *message, int message_id) {
    if (!server || !group_name || !sender_username || !message) return;
    
    printf("\n=== BROADCASTING GROUP MESSAGE ===\n");
    printf("Group '%s' (ID:%d), Message ID: %d, From '%s': %s\n", 
           group_name, group_id, message_id, sender_username, message);
    
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT u.username, u.id FROM group_members gm "
            "JOIN users u ON gm.user_id = u.id "
            "WHERE gm.group_id = %d",
            group_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) {
        printf("ERROR: Failed to get group members\n");
        return;
    }
    
    int member_count = PQntuples(res);
    int online_count = 0, offline_count = 0;
    
    printf("Group has %d member(s)\n", member_count);
    
    for (int i = 0; i < member_count; i++) {
        const char *member_username = PQgetvalue(res, i, 0);
        int member_id = atoi(PQgetvalue(res, i, 1));
        
        // Skip sender
        if (member_id == sender_id) {
            printf("Skipping sender '%s'\n", member_username);
            continue;
        }
        
        ClientSession *member = server_get_client_by_username(server, member_username);
        
        // Chỉ gửi real-time nếu user đang online VÀ đang trong chế độ messaging
        if (member && member->is_authenticated && 
            is_user_in_group_messaging(server->db_conn, member_id, group_id)) {
            
            char notification[1024];
            snprintf(notification, sizeof(notification),
                    "GROUP_MSG %s %s: %s", group_name, sender_username, message);
            
            char *response = build_response(STATUS_GROUP_MSG_OK, notification);
            int send_result = server_send_response(member, response);
            free(response);
            
            if (send_result > 0) {
                printf("Message sent to ONLINE user '%s' (in messaging mode)\n", member_username);
                online_count++;
            } else {
                printf("Failed to send to '%s', will fetch offline later\n", member_username);
                offline_count++;
            }
        } else {
            printf("User '%s' is OFFLINE or not in messaging mode, will fetch later\n", member_username);
            offline_count++;
        }
    }
    
    PQclear(res);
    
    printf("Broadcast complete - Online: %d, Offline: %d\n", online_count, offline_count);
    printf("=== END BROADCASTING ===\n\n");
}


/**
 * @function handle_group_msg_command: Handle group message command
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_group_msg_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    printf("\n=== HANDLE GROUP MESSAGE ===\n");
    printf("From user '%s' (ID:%d)\n", client->username, client->user_id);
    
    if (!check_auth(client)) return;
    
    if (cmd->param_count < 2 || !cmd->group_name || !cmd->message) {
        printf("ERROR: Invalid parameters\n");
        char *response = build_response(STATUS_UNDEFINED_ERROR, 
            "Group name and message required");
        send_and_free(client, response);
        return;
    }
    
    printf("Target group: '%s', Message: '%s'\n", cmd->group_name, cmd->message);
    
    int group_id = find_group_id(server->db_conn, cmd->group_name);
    if (group_id < 0) {
        printf("ERROR: Group not found\n");
        char *response = build_response(STATUS_GROUP_NOT_FOUND, 
            "Group does not exist");
        send_and_free(client, response);
        return;
    }
    
    printf("Found group '%s' with ID: %d\n", cmd->group_name, group_id);
    
    if (!is_in_group(server->db_conn, group_id, client->user_id)) {
        printf("ERROR: User not in group\n");
        char *response = build_response(STATUS_NOT_IN_GROUP, 
            "You are not a member of this group");
        send_and_free(client, response);
        return;
    }
    
    if (strlen(cmd->message) > MAX_MESSAGE_LENGTH - 1) {
        printf("ERROR: Message too long (%zu bytes)\n", strlen(cmd->message));
        char *response = build_response(STATUS_MESSAGE_TOO_LONG, 
            "Message exceeds maximum length");
        send_and_free(client, response);
        return;
    }
    
    char query[BUFFER_SIZE * 2];
    char *escaped_message = PQescapeLiteral(server->db_conn, cmd->message, 
                                           strlen(cmd->message));
    if (!escaped_message) {
        printf("ERROR: Failed to escape message\n");
        char *response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to save message");
        send_and_free(client, response);
        return;
    }
    
    snprintf(query, sizeof(query),
            "INSERT INTO group_messages (group_id, sender_id, content) "
            "VALUES (%d, %d, %s) RETURNING id",
            group_id, client->user_id, escaped_message);
    
    PQfreemem(escaped_message);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        printf("ERROR: Failed to save message to database\n");
        char *response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to save message");
        send_and_free(client, response);
        return;
    }
    
    int message_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    printf("Message saved to database with ID: %d\n", message_id);
    
    broadcast_group_message(server, group_id, cmd->group_name, 
                          client->username, client->user_id,
                          cmd->message, message_id);
    
    char *response = build_response(STATUS_GROUP_MSG_SENT_OK, 
        "Group message sent successfully");
    send_and_free(client, response);
    
    printf("=== END HANDLE GROUP MESSAGE ===\n\n");
}

/**
 * @function handle_get_group_offline_messages: Handle fetching offline group messages
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_get_group_offline_messages(Server *server, ClientSession *client, 
                                       ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;

    printf("\n=== GET GROUP OFFLINE MESSAGES ===\n");
    printf("User '%s' (ID:%d) entering group messaging mode\n", 
           client->username, client->user_id);
    
    if (!check_auth(client)) return;
    
    if (!cmd->group_name || strlen(cmd->group_name) == 0) {
        char *response = build_response(STATUS_UNDEFINED_ERROR, 
            "Group name required");
        send_and_free(client, response);
        return;
    }
    
    printf("Entering messaging mode for group '%s'\n", cmd->group_name);
    
    int group_id = find_group_id(server->db_conn, cmd->group_name);
    if (group_id < 0) {
        printf("ERROR: Group not found\n");
        char *response = build_response(STATUS_GROUP_NOT_FOUND, 
            "Group does not exist");
        send_and_free(client, response);
        return;
    }
    
    if (!is_in_group(server->db_conn, group_id, client->user_id)) {
        printf("ERROR: User not in group\n");
        char *response = build_response(STATUS_NOT_IN_GROUP, 
            "You are not a member");
        send_and_free(client, response);
        return;
    }
    
    if (!set_group_messaging_status(server->db_conn, client->user_id, group_id, 1)) {
        printf("ERROR: Failed to set messaging status\n");
        char *response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to enter messaging mode");
        send_and_free(client, response);
        return;
    }
    
    printf("Messaging mode activated. Fetching offline messages...\n");
    
    char query[1024];
    snprintf(query, sizeof(query),
            "SELECT gm.id, u.username, gm.content, gm.created_at "
            "FROM group_messages gm "
            "JOIN users u ON gm.sender_id = u.id "
            "JOIN group_members gm_receiver ON gm_receiver.group_id = gm.group_id "
            "  AND gm_receiver.user_id = %d "
            "WHERE gm.group_id = %d "
            "  AND gm.sender_id != %d "
            "  AND gm.created_at > gm_receiver.last_read_at "
            "ORDER BY gm.created_at ASC",
            client->user_id, group_id, client->user_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) {
        char *response = build_response(STATUS_DATABASE_ERROR, 
            "Failed to fetch offline messages");
        send_and_free(client, response);
        return;
    }
    
    int num_messages = PQntuples(res);
    
    if (num_messages == 0) {
        PQclear(res);
        printf("No unread messages for group '%s'\n", cmd->group_name);
        
        char update_query[256];
        snprintf(update_query, sizeof(update_query),
                "UPDATE group_members SET last_read_at = NOW() "
                "WHERE user_id = %d AND group_id = %d",
                client->user_id, group_id);
        execute_query(server->db_conn, update_query);
        
        char *response = build_response(STATUS_NOT_HAVE_OFFLINE_MESSAGE, 
            "No unread messages");
        send_and_free(client, response);
        return;
    }
    
    printf("Found %d unread message(s)\n", num_messages);
    
    char message_list[BUFFER_SIZE * 2];
    int offset = 0;
    
    offset += snprintf(message_list + offset, sizeof(message_list) - offset,
                      "\n=== OFFLINE MESSAGES FROM GROUP '%s' ===\n", cmd->group_name);
    
    for (int i = 0; i < num_messages && offset < (int)sizeof(message_list) - 500; i++) {
        const char *sender = PQgetvalue(res, i, 1);
        const char *content = PQgetvalue(res, i, 2);
        const char *created_at = PQgetvalue(res, i, 3);
        
        offset += snprintf(message_list + offset, sizeof(message_list) - offset,
                          "[%s] %s: %s\n", created_at, sender, content);
    }
    
    offset += snprintf(message_list + offset, sizeof(message_list) - offset,
                      "=== END OF UNREAD MESSAGES (%d total) ===", num_messages);
    
    PQclear(res);
    
    char update_query[256];
    snprintf(update_query, sizeof(update_query),
            "UPDATE group_members SET last_read_at = NOW() "
            "WHERE user_id = %d AND group_id = %d",
            client->user_id, group_id);
    execute_query(server->db_conn, update_query);
    
    printf("Marked messages as read\n");
    
    char *response = build_response(STATUS_GET_OFFLINE_MSG_OK, message_list);
    send_and_free(client, response);
    
    printf("=== END GET GROUP OFFLINE MESSAGES ===\n\n");
}

/**
 * @function handle_exit_group_messaging: Handle exiting group messaging mode
 * 
 * @param server: Server instance
 * @param client: Client session
 * @param cmd: Parsed command
 * 
 * @return void
 */
void handle_exit_group_messaging(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    printf("\n=== EXIT GROUP MESSAGING ===\n");
    printf("User '%s' (ID:%d) exiting group messaging mode\n", 
           client->username, client->user_id);
    
    if (!check_auth(client)) return;
    
    if (!cmd->group_name || strlen(cmd->group_name) == 0) {
        return;
    }
    
    int group_id = find_group_id(server->db_conn, cmd->group_name);
    if (group_id < 0) {
        return;
    }
    
    set_group_messaging_status(server->db_conn, client->user_id, group_id, 0);
    
    printf("Messaging mode deactivated for group '%s'\n", cmd->group_name);
    printf("=== END EXIT GROUP MESSAGING ===\n\n");
}
