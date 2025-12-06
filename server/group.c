#include "server.h"
#include "../database/database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void handle_group_create_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    // Check authentication
    if (!client->is_authenticated) {
        response = build_simple_response(STATUS_NOT_LOGGED_IN);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 1 || strlen(cmd->group_id) == 0) {
        response = build_simple_response(STATUS_UNDEFINED_ERROR);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Validate group name
    if (strlen(cmd->group_id) < 3 || strlen(cmd->group_id) > 50) {
        response = build_response(STATUS_UNDEFINED_ERROR, "Group name must be 3-50 characters");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Create group
    int group_id = create_group(server->db_conn, cmd->group_id, client->user_id);
    
    if (group_id == -2) {
        response = build_response(STATUS_GROUP_EXISTS, "Group name already exists");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    if (group_id < 0) {
        response = build_simple_response(STATUS_DATABASE_ERROR);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Success
    char msg[256];
    snprintf(msg, sizeof(msg), "Group '%s' created with ID: %d", cmd->group_id, group_id);
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
 * Format: GROUP_INVITE <group_id> <username>
 */
void handle_group_invite_command(Server *server, ClientSession *client, ParsedCommand *cmd) {
    if (!server || !client || !cmd) return;
    
    char *response = NULL;
    
    // Check authentication
    if (!client->is_authenticated) {
        response = build_simple_response(STATUS_NOT_LOGGED_IN);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check parameters
    if (cmd->param_count < 2) {
        response = build_simple_response(STATUS_UNDEFINED_ERROR);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    char* group_name = cmd->group_name;
    if (!group_name) {
        response = build_response(STATUS_INVALID_GROUP_ID, "Invalid group name");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check group exists
    int group_id = find_group_id(server->db_conn, group_name);
    if (group_id < 0) {
        response = build_response(STATUS_GROUP_NOT_FOUND, "Group not found");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check caller is owner
    if (!is_group_owner(server->db_conn, group_id, client->user_id)) {
        response = build_response(STATUS_NOT_GROUP_OWNER, "Only group owner can invite members");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Check target user exists
    if (!user_exists(server->db_conn, cmd->target_user)) {
        response = build_simple_response(STATUS_USER_NOT_FOUND);
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
        response = build_simple_response(STATUS_USER_NOT_FOUND);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    int target_user_id = atoi(PQgetvalue(res, 0, 0));
    PQclear(res);
    
    // Check if already in group
    if (is_in_group(server->db_conn, group_id, target_user_id)) {
        response = build_response(STATUS_ALREADY_IN_GROUP, "User already in group");
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Add user to group
    if (!add_user_to_group(server->db_conn, group_id, target_user_id)) {
        response = build_simple_response(STATUS_DATABASE_ERROR);
        server_send_response(client, response);
        free(response);
        return;
    }
    
    // Success
    char msg[256];
    snprintf(msg, sizeof(msg), "User '%s' added to group %d", cmd->target_user, group_id);
    response = build_response(STATUS_GROUP_INVITE_OK, msg);
    server_send_response(client, response);
    free(response);
    
    printf("? User %s added to group %d by %s\n", 
           cmd->target_user, group_id, client->username);
    
    // Notify target user if online
    ClientSession *target = server_get_client_by_username(server, cmd->target_user);
    if (target) {
        char notify[256];
        snprintf(notify, sizeof(notify), 
                "NOTIFY You have been added to group %d by %s",
                group_id, client->username);
        char *notify_msg = build_response(STATUS_GROUP_INVITE_OK, notify);
        server_send_response(target, notify_msg);
        free(notify_msg);
    }
}

