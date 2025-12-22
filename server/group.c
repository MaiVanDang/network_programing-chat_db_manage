#include "server.h"
#include "../database/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include<stdbool.h>

// ============================================================================
// Validation group 
// ============================================================================

/**
 * @brief Check user is owner's group?
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
 * @brief Check user in group?
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
 * @brief find group id 
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
 * @brief Store offline notification for user
 */
bool store_offline_notification(PGconn *db_conn, int user_id, int group_id, 
                                const char *owner, const char *group_name, const char *status) {
    if (!db_conn) return false;
    
    char query[1024];
    char escaped_owner[256];
    char escaped_group_name[256];

    PQescapeStringConn(db_conn, escaped_owner, owner, 
                       strlen(owner), NULL);
    PQescapeStringConn(db_conn, escaped_group_name, group_name, 
                       strlen(group_name), NULL);
    
    snprintf(query, sizeof(query),
            "INSERT INTO offline_notifications "
            "(user_id, notification_type, group_id, sender_username, message, created_at) "
            "VALUES (%d, 'GROUP_INVITE', %d, '%s', "
            "'You have been %s to group ''%s'' by %s', NOW())",
            user_id, group_id, escaped_owner, status, escaped_group_name, escaped_owner);
    
    bool result = execute_query(db_conn, query);
    
    if (result) {
        printf("Offline notification stored for user_id=%d\n", user_id);
    } else {
        printf("Failed to store offline notification for user_id=%d\n", user_id);
    }
    
    return result;
}

/**
 * @brief Send pending notifications to client upon login
 */
void send_pending_notifications(Server *server, ClientSession *client) {
    if (!server || !client || !client->is_authenticated) return;
    
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id, notification_type, group_id, sender_username, message, created_at "
            "FROM offline_notifications "
            "WHERE user_id = %d "
            "AND notification_type != 'GROUP_MESSAGE' "
            "ORDER BY created_at ASC",
            client->user_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) return;
    
    int notification_count = PQntuples(res);
    
    if (notification_count == 0) {
        PQclear(res);
        return;
    }
    
    printf("Sending %d pending notification(s) to '%s'\n", 
           notification_count, client->username);
    
    for (int i = 0; i < notification_count; i++) {
        int notif_id = atoi(PQgetvalue(res, i, 0));
        const char *type = PQgetvalue(res, i, 1);
        int group_id = atoi(PQgetvalue(res, i, 2));
        const char *sender = PQgetvalue(res, i, 3);
        const char *message = PQgetvalue(res, i, 4);
        const char *created_at = PQgetvalue(res, i, 5);
        
        char notification[1024];
        snprintf(notification, sizeof(notification),
                "OFFLINE_NOTIFICATION "
                "type=\"%s\" "
                "group_id=%d "
                "sender=\"%s\" "
                "message=\"%s\" "
                "time=\"%s\"",
                type, group_id, sender, message, created_at);
        
        char *notify_response = build_response(STATUS_OFFLINE_NOTIFICATION, notification);
        
        if (server_send_response(client, notify_response) > 0) {
            char delete_query[256];
            snprintf(delete_query, sizeof(delete_query),
                    "DELETE FROM offline_notifications WHERE id = %d",
                    notif_id);
            
            if (execute_query(server->db_conn, delete_query)) {
                printf("Notification %d delivered and deleted\n", notif_id);
            }
        } else {
            printf("Failed to send notification %d\n", notif_id);
        }
        
        free(notify_response);
    }
    
    PQclear(res);
    
    printf("All pending notifications processed for '%s'\n", client->username);
}

/**
 * @brief Create a join request for a group
 * @return 0 on success, -1 on error, -2 if pending request exists
 */
int create_join_request(PGconn *conn, int group_id, int user_id) {
    if (!conn) return -1;
    
    char check_query[512];
    snprintf(check_query, sizeof(check_query),
            "SELECT status FROM group_join_requests "
            "WHERE group_id = %d AND user_id = %d",
            group_id, user_id);
    
    PGresult *res = execute_query_with_result(conn, check_query);
    if (res && PQntuples(res) > 0) {
        const char *status = PQgetvalue(res, 0, 0);
        PQclear(res);
        
        if (strcmp(status, "pending") == 0) {
            return -2;
        }
        
        char delete_query[512];
        snprintf(delete_query, sizeof(delete_query),
                "DELETE FROM group_join_requests "
                "WHERE group_id = %d AND user_id = %d",
                group_id, user_id);
        execute_query(conn, delete_query);
    } else if (res) {
        PQclear(res);
    }
    
    char query[512];
    snprintf(query, sizeof(query),
            "INSERT INTO group_join_requests (group_id, user_id, status) "
            "VALUES (%d, %d, 'pending')",
            group_id, user_id);
    
    if (!execute_query(conn, query)) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief Get group owner user_id
 */
int get_group_owner_id(PGconn *conn, int group_id) {
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT user_id FROM group_members "
            "WHERE group_id = %d AND role = 'owner'",
            group_id);
    
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
 * @brief Get username by user_id
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
 * @brief Store join request notification for owner
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

int create_group(PGconn *conn, const char *group_name, int creator_id) {
    if (!conn || !group_name || creator_id <= 0) return -1;
    
    char check_query[1024];
    snprintf(check_query, sizeof(check_query),
            "SELECT COUNT(*) FROM groups "
            "WHERE group_name = '%s'",
            group_name);
    
    PGresult *res = execute_query_with_result(conn, check_query);
    if (res) {
        int count = atoi(PQgetvalue(res, 0, 0));
        PQclear(res);
        if (count > 0) {
            return -2;
        }
    }
    
    char query[1024];
    snprintf(query, sizeof(query),
            "INSERT INTO groups (group_name, creator_id) "
            "VALUES ('%s', %d) RETURNING id",
            group_name, creator_id);
    
    res = execute_query_with_result(conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        return -1;
    }
    
    int group_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    char add_creator_query[1024];
    snprintf(add_creator_query, sizeof(add_creator_query),
            "INSERT INTO group_members (group_id, user_id, role) "
            "VALUES (%d, %d, 'owner')",
            group_id, creator_id);
    
    if (!execute_query(conn, add_creator_query)) {
        char rollback_query[1024];
        snprintf(rollback_query, sizeof(rollback_query),
                "DELETE FROM groups WHERE id = %d", group_id);
        execute_query(conn, rollback_query);
        return -1;
    }
    
    return group_id;
}

/**
 * @brief Handle GROUP_CREATE command
 * Format: GROUP_CREATE <group_name>
 */
void handle_group_create_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    // Check authentication
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 1 || strlen(cmd->group_name) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Validate group name
    if (strlen(cmd->group_name) < 3 || strlen(cmd->group_name) > 50) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name must be 3-50 characters");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Create group
    int group_id = create_group(server->db_conn, cmd->group_name, client->user_id);
    
    if (group_id == -2) {
        response = build_response(STATUS_GROUP_EXISTS, "GROUP_EXISTS - Group name already exists");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (group_id < 0) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to create group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Success
    char msg[256];
    snprintf(msg, sizeof(msg), "Group '%s' created successfully with ID: %d", cmd->group_name, group_id);
    response = build_response(STATUS_GROUP_CREATE_OK, msg);
    server_send_response(client, response);
    free(response);
    
    printf("Group created: %s (id=%d) by %s (user_id=%d)\n", 
           cmd->group_id, group_id, client->username, client->user_id);
}

// ============================================================================
// TASK 5: Add user to group 
// ============================================================================

/**
 * @brief add user to group
 */
int add_user_to_group(PGconn *conn, int group_id, int user_id) {
    char query[512];
    snprintf(query, sizeof(query),
            "INSERT INTO group_members (group_id, user_id, role) "
            "VALUES (%d, %d, 'member')",
            group_id, user_id);
    
    return execute_query(conn, query);
}

/**
 * @brief Handle GROUP_INVITE command
 * Format: GROUP_INVITE <group_name> <username>
 */
void handle_group_invite_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 2) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name and username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char* group_name = cmd->group_name;
    if (!group_name) {
        response = build_response(STATUS_INVALID_GROUP_NAME, "INVALID_GROUP_NAME - Invalid group name");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check group exists
    int group_id = find_group_id(server->db_conn, group_name);
    if (group_id < 0) {
        response = build_response(STATUS_GROUP_NOT_FOUND, "GROUP_NOT_FOUND - Group does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check caller is owner
    if (!is_group_owner(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_GROUP_OWNER, "NOT_GROUP_OWNER - Only group owner can invite members");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check target user exists
    if (!user_exists(server->db_conn, cmd->target_user)) {
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            cmd->target_user);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int target_user_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    if (is_in_group(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_ALREADY_IN_GROUP, "ALREADY_IN_GROUP - User already in group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (!add_user_to_group(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to add user to group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Get group info for notification
    char fetched_group_name[128] = "Unknown Group";
    snprintf(query, sizeof(query),
            "SELECT group_name FROM groups WHERE id = %d", group_id);
    
    PGresult *group_res = execute_query_with_result(server->db_conn, query);
    if (group_res && PQntuples(group_res) > 0) {
        strncpy(fetched_group_name, PQgetvalue(group_res, 0, 0), sizeof(fetched_group_name) - 1);
        fetched_group_name[sizeof(fetched_group_name) - 1] = '\0';
        PQclear(group_res);
    }
    
    char success_msg[512];
    snprintf(success_msg, sizeof(success_msg), 
            "User '%s' has been added to group '%s'", 
            cmd->target_user, fetched_group_name);
    
    response = build_response(STATUS_GROUP_INVITE_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    printf("User '%s' added to group '%s' by '%s'\n", 
           cmd->target_user, fetched_group_name, client->username);
    
    ClientSession *target = server_get_client_by_username(server, cmd->target_user);
    if (target && target->is_authenticated) {
        char notification[1024];
        snprintf(notification, sizeof(notification),
                "GROUP_INVITE_NOTIFICATION "
                "group_id=%d "
                "group_name=\"%s\" "
                "invited_by=\"%s\" "
                "message=\"You have been added to group '%s' by %s\"",
                group_id,
                fetched_group_name,
                client->username,
                fetched_group_name,
                client->username);
        
        char *notify_response = build_response(STATUS_GROUP_INVITE_NOTIFICATION, notification);
        
        int send_result = server_send_response(target, notify_response);
        
        if (send_result > 0) {
            printf("Real-time notification sent to '%s' (fd=%d)\n", 
                   cmd->target_user, target->socket_fd);
        } else {
            printf("Failed to send notification to '%s', storing offline\n",
                   cmd->target_user);
            
            store_offline_notification(server->db_conn, target_user_id, 
                                      group_id, client->username, fetched_group_name, "added");
        }
        
        free(notify_response);
    } else {
        
        printf("User '%s' is offline, storing notification\n", cmd->target_user);
        
        store_offline_notification(server->db_conn, target_user_id, 
                                  group_id, client->username, fetched_group_name, "added");
    }
}

// ============================================================================
// TASK 6: Remove user from group
// ============================================================================

/**
 * @brief remove user from group
 */
int remove_user_from_group(PGconn *conn, int  group_id, int user_id) {
    char query[512];
    snprintf(query, sizeof(query),
            "DELETE FROM group_members "
            "WHERE group_id = %d AND user_id = %d",
            group_id, user_id);
    
    return execute_query(conn, query);
}

/**
 * @brief Handle GROUP_KICK command
 * Format: GROUP_KICK <group_name> <username>
 */
void handle_group_kick_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    // Check authentication
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 2) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name and username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char* group_name = cmd->group_name;
    if (!group_name) {
        response = build_response(STATUS_INVALID_GROUP_NAME, "INVALID_GROUP_ID - Invalid group name");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check group exists
    int group_id = find_group_id(server->db_conn, group_name);
    if (group_id < 0) {
        response = build_response(STATUS_GROUP_NOT_FOUND, "GROUP_NOT_FOUND - Group does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check caller is owner
    if (!is_group_owner(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_GROUP_OWNER, "NOT_GROUP_OWNER - Only group owner can kick members");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check target user exists
    if (!user_exists(server->db_conn, cmd->target_user)) {
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Get target user ID
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'",
            cmd->target_user);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int target_user_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    // Check if target is in group
    if (!is_in_group(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_NOT_IN_GROUP, "NOT_IN_GROUP - User not in group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Cannot kick owner
    if (is_group_owner(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_CANNOT_KICK_OWNER, "CANNOT_KICK_OWNER - Cannot kick group owner");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Remove user from group
    if (!remove_user_from_group(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to kick user from group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Success
    char msg[256];
    snprintf(msg, sizeof(msg), "User '%s' kicked from group '%s' successfully", cmd->target_user, group_name);
    response = build_response(STATUS_GROUP_KICK_OK, msg);
    server_send_response(client, response);
    free(response);
    
    printf("User %s kicked from group %s by %s\n", 
           cmd->target_user, group_name, client->username);
    
    // Get group info for notification
    char fetched_group_name[128] = "Unknown Group";
    snprintf(query, sizeof(query),
            "SELECT group_name FROM groups WHERE id = %d", group_id);
    
    PGresult *group_res = execute_query_with_result(server->db_conn, query);
    if (group_res && PQntuples(group_res) > 0) {
        strncpy(fetched_group_name, PQgetvalue(group_res, 0, 0), sizeof(fetched_group_name) - 1);
        fetched_group_name[sizeof(fetched_group_name) - 1] = '\0';
        PQclear(group_res);
    }
    
    char success_msg[512];
    snprintf(success_msg, sizeof(success_msg), 
            "User '%s' has been kicked from group '%s'", 
            cmd->target_user, fetched_group_name);
    
    response = build_response(STATUS_GROUP_KICK_OK, success_msg);
    server_send_response(client, response);
    free(response);
    
    printf("User '%s' kicked from group '%s' by '%s'\n", 
           cmd->target_user, fetched_group_name, client->username);
    
    ClientSession *target = server_get_client_by_username(server, cmd->target_user);
    if (target && target->is_authenticated) {
        char notification[1024];
        snprintf(notification, sizeof(notification),
                "GROUP_KICK_NOTIFICATION "
                "group_id=%d "
                "group_name=\"%s\" "
                "kicked_by=\"%s\" "
                "message=\"You have been kicked from group '%s' by %s\"",
                group_id,
                fetched_group_name,
                client->username,
                fetched_group_name,
                client->username);
        
        char *notify_response = build_response(STATUS_GROUP_KICK_NOTIFICATION, notification);
        
        int send_result = server_send_response(target, notify_response);
        
        if (send_result > 0) {
            printf("Real-time notification sent to '%s' (fd=%d)\n", 
                   cmd->target_user, target->socket_fd);
        } else {
            printf("Failed to send notification to '%s', storing offline\n",
                   cmd->target_user);
            
            store_offline_notification(server->db_conn, target_user_id, 
                                      group_id, client->username, fetched_group_name, "kicked");
        }
        
        free(notify_response);
    } else {
        
        printf("User '%s' is offline, storing notification\n", cmd->target_user);
        
        store_offline_notification(server->db_conn, target_user_id, 
                                  group_id, client->username, fetched_group_name, "kicked");
    }
}

// ============================================================================
// TASK 7: Leave group
// ============================================================================
/**
 * @brief Handle GROUP_LEAVE command
 * Format: GROUP_LEAVE <group_id>
 */
void handle_group_leave_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (cmd->param_count < 1) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char* group_name = cmd->group_name;
    if (!group_name) {
        response = build_response(STATUS_INVALID_GROUP_NAME, "INVALID_GROUP_NAME - Invalid group name");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int group_id = find_group_id(server->db_conn, group_name);
    if (group_id < 0) {
        response = build_response(STATUS_GROUP_NOT_FOUND, "GROUP_NOT_FOUND - Group does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (!is_in_group(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_IN_GROUP, "NOT_IN_GROUP - You are not in this group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (is_group_owner(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_GROUP_OWNER, 
                "OWNER_CANNOT_LEAVE - Owner cannot leave group. Transfer ownership or delete group first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (!remove_user_from_group(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to leave group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char msg[128];
    snprintf(msg, sizeof(msg), "You left group '%s' successfully", group_name);
    response = build_response(STATUS_GROUP_LEAVE_OK, msg);
    server_send_response(client, response);
    free(response);
    
    printf("User %s left group %s\n", client->username, group_name);
}

// ============================================================================
// TASK 8: Join group with approval
// ============================================================================
/**
 * @brief Handle GROUP_JOIN command (with approval system)
 * Format: GROUP_JOIN <group_name>
 */
void handle_group_join_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    // Check authentication
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 1) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char* group_name = cmd->group_name;
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id, group_name FROM groups WHERE group_name = '%s'", group_name);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        response = build_response(STATUS_GROUP_NOT_FOUND, "GROUP_NOT_FOUND - Group does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int group_id = atoi(PQgetvalue(res, 0, 0));
    char fetched_group_name[128];
    strncpy(fetched_group_name, PQgetvalue(res, 0, 1), sizeof(fetched_group_name) - 1);
    fetched_group_name[sizeof(fetched_group_name) - 1] = '\0';
    PQclear(res);
    
    // Check if already in group
    if (is_in_group(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_ALREADY_IN_GROUP, "ALREADY_IN_GROUP - You are already a member");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Create join request
    int result = create_join_request(server->db_conn, group_id, client->user_id);
    
    if (result == -2) {
        response = build_response(STATUS_REQUEST_PENDING, 
                "REQUEST_PENDING - You already have a pending join request for this group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (result == -1) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to create join request");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Success - request created
    char msg[256];
    snprintf(msg, sizeof(msg), 
            "Join request sent for group '%s'. Waiting for owner approval.", fetched_group_name);
    response = build_response(STATUS_JOIN_REQUEST_SENT, msg);
    server_send_response(client, response);
    free(response);
    
    printf("User '%s' requested to join group '%s' (id=%d)\n", 
           client->username, fetched_group_name, group_id);
    
    // Notify owner
    int owner_id = get_group_owner_id(server->db_conn, group_id);
    if (owner_id <= 0) return;
    char *owner_username = get_username_by_id(server->db_conn, owner_id);
        
    if (owner_username) {
        ClientSession *owner = server_get_client_by_username(server, owner_username);
            
        if (owner && owner->is_authenticated) {
            char notification[1024];
            snprintf(notification, sizeof(notification),
                    "GROUP_JOIN_REQUEST_NOTIFICATION "
                    "group_id=%d "
                    "group_name=\"%s\" "
                    "requester=\"%s\" "
                    "message=\"%s wants to join group '%s'\"",
                    group_id, fetched_group_name, client->username,
                    client->username, fetched_group_name);
                
            char *notify_response = build_response(STATUS_GROUP_JOIN_REQUEST_NOTIFICATION, 
                                                    notification);
                
            int send_result = server_send_response(owner, notify_response);
            
            if (send_result > 0) {
                printf("Join request notification sent to owner '%s'\n", owner_username);                
            } else {
                printf("Failed to send to owner, storing offline\n");
                store_join_request_notification(server->db_conn, owner_id, 
                                                group_id, client->username, fetched_group_name);
            }
                
            free(notify_response);
        } else {
            printf("Owner '%s' is offline, storing notification\n", owner_username);
            store_join_request_notification(server->db_conn, owner_id,
                                            group_id, client->username, fetched_group_name);
        }
            
        free(owner_username);
    }
}

/**
 * @brief Handle GROUP_APPROVE command
 * Format: GROUP_APPROVE <group_name> <username>
 */
void handle_group_approve_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    // Check authentication
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 2) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name and username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char* group_name = cmd->group_name;
    if (!group_name) {
        response = build_response(STATUS_INVALID_GROUP_NAME, "INVALID_GROUP_NAME - Invalid group name");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check group exists
    int group_id = find_group_id(server->db_conn, group_name);
    if (group_id < 0) {
        response = build_response(STATUS_GROUP_NOT_FOUND, "GROUP_NOT_FOUND - Group does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check if caller is owner
    if (!is_group_owner(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_GROUP_OWNER, 
                "NOT_GROUP_OWNER - Only group owner can approve requests");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Get requester user_id
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'", cmd->target_user);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int requester_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    // Check if request exists
    snprintf(query, sizeof(query),
            "SELECT status FROM group_join_requests "
            "WHERE group_id = %d AND user_id = %d",
            group_id, requester_id);
    
    res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        response = build_response(STATUS_NO_PENDING_REQUEST, 
                "NO_PENDING_REQUEST - No pending join request from this user");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    const char *status = PQgetvalue(res, 0, 0);
    if (strcmp(status, "pending") != 0) {
        PQclear(res);
        response = build_response(STATUS_NO_PENDING_REQUEST, 
                "NO_PENDING_REQUEST - Request already processed");
        server_send_response(client, response);
        free(response);
        return;
    }
    PQclear(res);
    
    // Add user to group
    if (!add_user_to_group(server->db_conn, group_id, requester_id)) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to add user");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Update request status
    snprintf(query, sizeof(query),
            "UPDATE group_join_requests "
            "SET status = 'approved' "
            "WHERE group_id = %d AND user_id = %d",
            group_id, requester_id);
    execute_query(server->db_conn, query);
    
    // Get group name
    snprintf(query, sizeof(query),
            "SELECT group_name FROM groups WHERE id = %d", group_id);
    res = execute_query_with_result(server->db_conn, query);
    
    char fetched_group_name[128] = "Unknown";
    if (res && PQntuples(res) > 0) {
        strncpy(fetched_group_name, PQgetvalue(res, 0, 0), sizeof(fetched_group_name) - 1);
        PQclear(res);
    }
    
    // Send success to owner
    char msg[256];
    snprintf(msg, sizeof(msg), "User '%s' approved to join group '%s'", 
            cmd->target_user, fetched_group_name);
    response = build_response(STATUS_GROUP_APPROVE_OK, msg);
    server_send_response(client, response);
    free(response);
    
    printf("Owner '%s' approved '%s' to join group '%s'\n",
           client->username, cmd->target_user, fetched_group_name);
    
    // Notify requester
    ClientSession *requester = server_get_client_by_username(server, cmd->target_user);
    
    if (requester && requester->is_authenticated) {
        char notification[512];
        snprintf(notification, sizeof(notification),
                "GROUP_JOIN_APPROVED_NOTIFICATION "
                "group_id=%d "
                "group_name=\"%s\" "
                "message=\"Your request to join group '%s' has been approved!\"",
                group_id, fetched_group_name, fetched_group_name);
        
        char *notify_response = build_response(STATUS_GROUP_JOIN_APPROVED, notification);
        server_send_response(requester, notify_response);
        free(notify_response);
        
        printf("Approval notification sent to '%s'\n", cmd->target_user);
    } else {
        // Store offline notification
        char notif_msg[512];
        snprintf(notif_msg, sizeof(notif_msg),
                "Your request to join group '%s' has been approved!", fetched_group_name);
        
        store_offline_notification(server->db_conn, requester_id, group_id,
                                  client->username, fetched_group_name, "approved to join");
    }
}

/**
 * @brief Handle GROUP_REJECT command
 * Format: GROUP_REJECT <group_name> <username>
 */
void handle_group_reject_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    // Check authentication
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 2) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name and username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char* group_name = cmd->group_name;
    if (!group_name) {
        response = build_response(STATUS_INVALID_GROUP_NAME, "INVALID_GROUP_NAME - Invalid group name");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check group exists
    int group_id = find_group_id(server->db_conn, group_name);
    if (group_id < 0) {
        response = build_response(STATUS_GROUP_NOT_FOUND, "GROUP_NOT_FOUND - Group does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check if caller is owner
    if (!is_group_owner(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_GROUP_OWNER, 
                "NOT_GROUP_OWNER - Only group owner can reject requests");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Get requester user_id
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id FROM users WHERE username = '%s'", cmd->target_user);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        response = build_response(STATUS_USER_NOT_FOUND, "USER_NOT_FOUND - User does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int requester_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    // Check if request exists
    snprintf(query, sizeof(query),
            "SELECT status FROM group_join_requests "
            "WHERE group_id = %d AND user_id = %d",
            group_id, requester_id);
    
    res = execute_query_with_result(server->db_conn, query);
    if (!res || PQntuples(res) == 0) {
        if (res) PQclear(res);
        response = build_response(STATUS_NO_PENDING_REQUEST, 
                "NO_PENDING_REQUEST - No pending join request from this user");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    const char *status = PQgetvalue(res, 0, 0);
    if (strcmp(status, "pending") != 0) {
        PQclear(res);
        response = build_response(STATUS_NO_PENDING_REQUEST, 
                "NO_PENDING_REQUEST - Request already processed");
        server_send_response(client, response);
        free(response);
        return;
    }
    PQclear(res);
    
    // Update request status
    snprintf(query, sizeof(query),
            "UPDATE group_join_requests "
            "SET status = 'rejected' "
            "WHERE group_id = %d AND user_id = %d",
            group_id, requester_id);
    execute_query(server->db_conn, query);
    
    // Get group name
    snprintf(query, sizeof(query),
            "SELECT group_name FROM groups WHERE id = %d", group_id);
    res = execute_query_with_result(server->db_conn, query);
    
    char fetched_group_name[128] = "Unknown";
    if (res && PQntuples(res) > 0) {
        strncpy(fetched_group_name, PQgetvalue(res, 0, 0), sizeof(fetched_group_name) - 1);
        PQclear(res);
    }
    
    // Send success to owner
    char msg[256];
    snprintf(msg, sizeof(msg), "Join request from '%s' rejected", cmd->target_user);
    response = build_response(STATUS_GROUP_REJECT_OK, msg);
    server_send_response(client, response);
    free(response);
    
    printf("Owner '%s' rejected '%s' from joining group '%s'\n",
           client->username, cmd->target_user, fetched_group_name);
    
    // Notify requester
    ClientSession *requester = server_get_client_by_username(server, cmd->target_user);
    
    if (requester && requester->is_authenticated) {
        char notification[512];
        snprintf(notification, sizeof(notification),
                "GROUP_JOIN_REJECTED_NOTIFICATION "
                "group_id=%d "
                "group_name=\"%s\" "
                "message=\"Your request to join group '%s' has been rejected\"",
                group_id, fetched_group_name, fetched_group_name);
        
        char *notify_response = build_response(STATUS_GROUP_JOIN_REJECTED, notification);
        server_send_response(requester, notify_response);
        free(notify_response);
        
        printf("Rejection notification sent to '%s'\n", cmd->target_user);
    } else {
        // Store offline notification
        char notif_msg[512];
        snprintf(notif_msg, sizeof(notif_msg),
                "Your request to join group '%s' has been rejected", fetched_group_name);
        
        store_offline_notification(server->db_conn, requester_id, group_id,
                                  client->username, fetched_group_name, "rejected from");
    }
}

/**
 * @brief Handle LIST_JOIN_REQUESTS command
 * Format: LIST_JOIN_REQUESTS <group_name>
 */
void handle_list_join_requests_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    // Check authentication
    if (!client->is_authenticated) {
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 2) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name and username required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char* group_name = cmd->group_name;
    if (!group_name) {
        response = build_response(STATUS_INVALID_GROUP_NAME, "INVALID_GROUP_NAME - Invalid group name");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check group exists
    int group_id = find_group_id(server->db_conn, group_name);
    if (group_id < 0) {
        response = build_response(STATUS_GROUP_NOT_FOUND, "GROUP_NOT_FOUND - Group does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (!is_group_owner(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_GROUP_OWNER, 
                "NOT_GROUP_OWNER - Only owner can view join requests");
        server_send_response(client, response);
        free(response);
        return;
    }
    
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
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to fetch requests");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int count = PQntuples(res);
    
    if (count == 0) {
        response = build_response(STATUS_MSG_OK, "No pending join requests");
        server_send_response(client, response);
        PQclear(res);
        free(response);
        return;
    }
    
    char msg[2048] = "Pending join requests:\n";
    for (int i = 0; i < count; i++) {
        const char *username = PQgetvalue(res, i, 0);
        const char *time = PQgetvalue(res, i, 1);
        
        char line[256];
        snprintf(line, sizeof(line), "%d. %s (requested at: %s)\n", i + 1, username, time);
        strcat(msg, line);
    }
    
    PQclear(res);
    
    response = build_response(STATUS_MSG_OK, msg);
    server_send_response(client, response);
    free(response);
}

// ============================================================================
// Group Messaging System
// ============================================================================

/**
 * @brief Broadcast message to all group members
 */
void broadcast_group_message(Server *server, int group_id, const char *group_name, 
                             const char *sender_username, int sender_id,
                             const char *message, int message_id) {
    if (!server || !group_name || !sender_username || !message) return;
    
    printf("\n=== BROADCASTING GROUP MESSAGE ===\n");
    printf("DEBUG: Group '%s' (ID:%d)\n", group_name, group_id);
    printf("DEBUG: Message ID: %d, From '%s': %s\n", message_id, sender_username, message);
    
    // Get all members of the group
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT u.username, u.id "
            "FROM group_members gm "
            "JOIN users u ON gm.user_id = u.id "
            "WHERE gm.group_id = %d",
            group_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) {
        printf("ERROR: Failed to get group members\n");
        return;
    }
    
    int member_count = PQntuples(res);
    printf("DEBUG: Group has %d member(s)\n", member_count);
    
    int online_count = 0;
    int offline_count = 0;
    
    for (int i = 0; i < member_count; i++) {
        const char *member_username = PQgetvalue(res, i, 0);
        int member_id = atoi(PQgetvalue(res, i, 1));
        
        // Skip sender
        if (member_id == sender_id) {
            printf("DEBUG: Skipping sender '%s'\n", member_username);
            
            // Mark as read by sender immediately
            char read_query[256];
            snprintf(read_query, sizeof(read_query),
                    "INSERT INTO group_message_reads (message_id, user_id) "
                    "VALUES (%d, %d) "
                    "ON CONFLICT (message_id, user_id) DO NOTHING",
                    message_id, sender_id);
            execute_query(server->db_conn, read_query);
            
            continue;
        }
        
        // Try to send to online members
        ClientSession *member = server_get_client_by_username(server, member_username);
        
        if (member && member->is_authenticated) {
            // Member is online
            char notification[1024];
            snprintf(notification, sizeof(notification),
                    "GROUP_MSG %s %s: %s",
                    group_name, sender_username, message);
            
            char *response = build_response(STATUS_GROUP_MSG_OK, notification);
            int send_result = server_send_response(member, response);
            free(response);
            
            if (send_result > 0) {
                printf("DEBUG: Message sent to ONLINE user '%s'\n", member_username);
                online_count++;

                // Mark as read immediately for online users
                char read_query[256];
                snprintf(read_query, sizeof(read_query),
                        "INSERT INTO group_message_reads (message_id, user_id) "
                        "VALUES (%d, %d) "
                        "ON CONFLICT (message_id, user_id) DO NOTHING",
                        message_id, member_id);
                execute_query(server->db_conn, read_query);
            } else {
                printf("WARNING: Failed to send to '%s', will fetch offline later\n", member_username);
                offline_count++;
            }
        } else {
            // Member is offline, store message
            printf("DEBUG: User '%s' is OFFLINE, will fetch later\n", member_username);
            offline_count++;
        }
    }
    
    PQclear(res);
    
    printf("DEBUG: Broadcast complete - Online: %d, Offline: %d\n", online_count, offline_count);
    printf("=== END BROADCASTING ===\n\n");
}

/**
 * @brief Handle GROUP_MSG command
 * Format: GROUP_MSG <group_name> <message>
 */
void handle_group_msg_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    printf("\n=== HANDLE GROUP MESSAGE ===\n");
    printf("DEBUG: From user '%s' (ID:%d)\n", client->username, client->user_id);
    
    // Check authentication
    if (!client->is_authenticated) {
        printf("ERROR: User not authenticated\n");
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 2 || !cmd->group_name || !cmd->message) {
        printf("ERROR: Invalid parameters\n");
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name and message required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Target group: '%s'\n", cmd->group_name);
    printf("DEBUG: Message: '%s'\n", cmd->message);
    
    // Find group
    int group_id = find_group_id(server->db_conn, cmd->group_name);
    if (group_id < 0) {
        printf("ERROR: Group not found\n");
        response = build_response(STATUS_GROUP_NOT_FOUND, "GROUP_NOT_FOUND - Group does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Found group '%s' with ID: %d\n", cmd->group_name, group_id);
    
    // Check if user is member
    if (!is_in_group(server->db_conn, group_id, client->user_id)) {
        printf("ERROR: User not in group\n");
        response = build_response(STATUS_NOT_IN_GROUP, "NOT_IN_GROUP - You are not a member of this group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: User is member - OK\n");
    
    // Check message length
    if (strlen(cmd->message) > MAX_MESSAGE_LENGTH - 1) {
        printf("ERROR: Message too long (%zu bytes)\n", strlen(cmd->message));
        response = build_response(STATUS_MESSAGE_TOO_LONG, "MESSAGE_TOO_LONG - Message exceeds maximum length");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Save message to database and GET message_id
    char query[BUFFER_SIZE * 2];
    char *escaped_message = PQescapeLiteral(server->db_conn, cmd->message, strlen(cmd->message));
    if (!escaped_message) {
        printf("ERROR: Failed to escape message\n");
        response = build_response(STATUS_DATABASE_ERROR, "DATABASE_ERROR - Failed to save message");
        server_send_response(client, response);
        free(response);
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
        response = build_response(STATUS_DATABASE_ERROR, "DATABASE_ERROR - Failed to save message");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int message_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    printf("DEBUG: Message saved to database with ID: %d\n", message_id);
    
    // Broadcast message to all group members
    broadcast_group_message(server, group_id, cmd->group_name, 
                          client->username, client->user_id,
                          cmd->message, message_id);
    
    // Send success response to sender
    response = build_response(STATUS_GROUP_MSG_SENT_OK, "OK - Group message sent successfully");
    server_send_response(client, response);
    free(response);
    
    printf("=== END HANDLE GROUP MESSAGE ===\n\n");
}

/**
 * @brief Handle GET_GROUP_OFFLINE_MSG command
 * Format: GROUP_SEND_OFFLINE_MSG <group_name>
 */
void handle_get_group_offline_messages(Server *server, ClientSession *client, ParsedCommand *cmd) {
    char *response = NULL;
    
    printf("\n=== GET GROUP OFFLINE MESSAGES ===\n");
    printf("DEBUG: User '%s' (ID:%d) requesting offline group messages\n", 
           client->username, client->user_id);
    
    // Check login
    if (!client->is_authenticated) {
        printf("ERROR: User not authenticated\n");
        response = build_response(STATUS_NOT_LOGGED_IN, "NOT_LOGGED_IN - Please login first");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Get group name from command
    const char *group_name = cmd->group_name;
    if (!group_name || strlen(group_name) == 0) {
        response = build_response(STATUS_UNDEFINED_ERROR, "BAD_REQUEST - Group name required");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Fetching offline messages for group '%s'\n", group_name);
    
    // Find group ID
    int group_id = find_group_id(server->db_conn, group_name);
    if (group_id < 0) {
        printf("ERROR: Group not found\n");
        response = build_response(STATUS_GROUP_NOT_FOUND, "GROUP_NOT_FOUND - Group does not exist");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check if user is member
    if (!is_in_group(server->db_conn, group_id, client->user_id)) {
        printf("ERROR: User not in group\n");
        response = build_response(STATUS_NOT_IN_GROUP, "NOT_IN_GROUP - You are not a member");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Query unread messages from group_messages
    char query[1024];
    snprintf(query, sizeof(query),
            "SELECT gm.id, u.username, gm.content, gm.created_at "
            "FROM group_messages gm "
            "JOIN users u ON gm.sender_id = u.id "
            "LEFT JOIN group_message_reads gmr ON gm.id = gmr.message_id AND gmr.user_id = %d "
            "WHERE gm.group_id = %d "
              "AND gmr.id IS NULL "
            "ORDER BY gm.created_at ASC",
            client->user_id, group_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) {
        response = build_response(STATUS_DATABASE_ERROR, "UNKNOWN_ERROR - Failed to fetch offline messages");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int num_messages = PQntuples(res);
    
    if (num_messages == 0) {
        PQclear(res);
        printf("DEBUG: No unread messages for group '%s'\n", group_name);
        response = build_response(STATUS_NOT_HAVE_OFFLINE_MESSAGE, "No unread messages");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    printf("DEBUG: Found %d unread message(s)\n", num_messages);
    
    // Create response containing all messages
    char message_list[BUFFER_SIZE * 2];
    int offset = 0;
    
    offset += snprintf(message_list + offset, sizeof(message_list) - offset,
                      "\n=== OFFLINE MESSAGES FROM GROUP '%s' ===\n", group_name);
    
    // Store message IDs to delete later
    int message_ids[100];
    int id_count = 0;
    
    for (int i = 0; i < num_messages && offset < (int)sizeof(message_list) - 500; i++) {
        int msg_id = atoi(PQgetvalue(res, i, 0));
        const char *sender = PQgetvalue(res, i, 1);
        const char *content = PQgetvalue(res, i, 2);
        const char *created_at = PQgetvalue(res, i, 3);
        
        if (id_count < 100) {
            message_ids[id_count++] = msg_id;
        }
        
        offset += snprintf(message_list + offset, sizeof(message_list) - offset,
                          "[%s] %s: %s\n", created_at, sender, content);
    }
    
    offset += snprintf(message_list + offset, sizeof(message_list) - offset,
                      "=== END OF UNREAD MESSAGES (%d total) ===", num_messages);
    
    PQclear(res);
    
    // Mark messages as read
    for (int i = 0; i < id_count; i++) {
        snprintf(query, sizeof(query),
                "INSERT INTO group_message_reads (message_id, user_id) "
                "VALUES (%d, %d) "
                "ON CONFLICT (message_id, user_id) DO NOTHING",
                message_ids[i], client->user_id);
        
        if (!execute_query(server->db_conn, query)) {
            printf("WARNING: Failed to mark message %d as read\n", message_ids[i]);
        }
    }
    
    printf("DEBUG: Marked %d message(s) as read\n", id_count);
    
    // Send response
    response = build_response(STATUS_GET_OFFLINE_MSG_OK, message_list);
    server_send_response(client, response);
    free(response);
    
    printf("=== END GET GROUP OFFLINE MESSAGES ===\n\n");
}
