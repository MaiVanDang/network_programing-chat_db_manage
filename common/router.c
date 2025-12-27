#include "../database/database.h"
#include "../server/server.h"
#include "../server/group.h"
#include "../server/auth.h"
#include "../server/friend.h"
#include "../server/message.h"
#include "../helper/helper.h"
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// Message Router
// ============================================================================

void server_handle_client_message(Server *server, ClientSession *client, const char *message) {
    if (!server || !client || !message) return;
    
    ParsedCommand *cmd = parse_protocol_message(message);
    if (!cmd) {
        char *response = build_simple_response(STATUS_UNDEFINED_ERROR);
        server_send_response(client, response);
        free(response);
        
        const char *username = client->is_authenticated ? client->username : "Guest";
        log_activity(username, "PARSE_ERROR", message, "500", "Failed to parse command");
        return;
    }
    
    // Get command name and details for logging
    const char *cmd_code = "UNKNOWN";
    char cmd_detail[512] = "";
    char result_code[16] = "0";
    char result_detail[256] = "Pending";
    
    // Get username for logging
    const char *log_username = client->is_authenticated ? client->username : "Guest";
    
    // Store initial response code
    int initial_response_code = client->last_response_code;
    
    switch (cmd->cmd_type) {
        case CMD_REGISTER:
            cmd_code = "REGISTER";
            snprintf(cmd_detail, sizeof(cmd_detail), "username=%s", cmd->username);
            handle_register_command(server, client, cmd);
            break;
            
        case CMD_LOGIN:
            cmd_code = "LOGIN";
            snprintf(cmd_detail, sizeof(cmd_detail), "username=%s", cmd->username);
            handle_login_command(server, client, cmd);
            // Update username after successful login
            log_username = client->is_authenticated ? client->username : "Guest";
            break;
            
        case CMD_LOGOUT:
            cmd_code = "LOGOUT";
            snprintf(cmd_detail, sizeof(cmd_detail), "username=%s", client->username);
            handle_logout_command(server, client, cmd);
            break;
            
        case CMD_FRIEND_REQ:
            cmd_code = "FRIEND_REQ";
            snprintf(cmd_detail, sizeof(cmd_detail), "to=%s", cmd->target_user);
        	handle_friend_request(server, client, cmd);
            break;

        case CMD_FRIEND_ACCEPT:
            cmd_code = "FRIEND_ACCEPT";
            snprintf(cmd_detail, sizeof(cmd_detail), "from=%s", cmd->target_user);
        	handle_friend_accept(server, client, cmd);
            break;
            
        case CMD_FRIEND_PENDING:
            cmd_code = "FRIEND_PENDING";
            strcpy(cmd_detail, "list_pending_requests");
        	handle_friend_pending(server, client, cmd);
            break;
            
        case CMD_FRIEND_DECLINE:
            cmd_code = "FRIEND_DECLINE";
            snprintf(cmd_detail, sizeof(cmd_detail), "from=%s", cmd->target_user);
        	handle_friend_decline(server, client, cmd);
            break;
            
        case CMD_FRIEND_REMOVE:
            cmd_code = "FRIEND_REMOVE";
            snprintf(cmd_detail, sizeof(cmd_detail), "user=%s", cmd->target_user);
        	handle_friend_remove(server, client, cmd);
            break;

        case CMD_FRIEND_LIST:
            cmd_code = "FRIEND_LIST";
            strcpy(cmd_detail, "get_friend_list");
        	handle_friend_list(server, client);
            break;

        case CMD_MSG:
            cmd_code = "MSG";
            snprintf(cmd_detail, sizeof(cmd_detail), "to=%s, len=%zu", 
                    cmd->target_user, strlen(cmd->message));
            handle_send_message(server, client, cmd);
            break;

        case CMD_GET_OFFLINE_MSG:
            cmd_code = "GET_OFFLINE_MSG";
            strcpy(cmd_detail, "retrieve_offline_messages");
            handle_get_offline_messages(server, client, cmd);
            break;
        	
        case CMD_GROUP_CREATE:
            cmd_code = "GROUP_CREATE";
            snprintf(cmd_detail, sizeof(cmd_detail), "name=%s", cmd->group_name);
        	handle_group_create_command(server, client, cmd);
            break;
            
        case CMD_GROUP_INVITE:
            cmd_code = "GROUP_INVITE";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s, user=%s", 
                    cmd->group_id, cmd->target_user);
        	handle_group_invite_command(server, client, cmd);
            break;
            
        case CMD_GROUP_JOIN:
        	handle_group_join_command(server, client, cmd);
            break;
        	
        case CMD_GROUP_LEAVE:
            cmd_code = "GROUP_LEAVE";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s", cmd->group_id);
        	handle_group_leave_command(server, client, cmd);
            break;
        	
        case CMD_GROUP_KICK:
            cmd_code = "GROUP_KICK";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s, user=%s", 
                    cmd->group_id, cmd->target_user);
        	handle_group_kick_command(server, client, cmd);
            break;
            
        case CMD_GROUP_MSG:
        	handle_group_msg_command(server, client, cmd);
            break;
        case CMD_GROUP_SEND_OFFLINE_MSG:
        	handle_get_group_offline_messages(server, client, cmd);
            break;
        case CMD_GROUP_APPROVE:
        	handle_group_approve_command(server, client, cmd);
            break;
        case CMD_GROUP_REJECT:
        	handle_group_reject_command(server, client, cmd);
            break;
        case CMD_LIST_JOIN_REQUESTS:
        	handle_list_join_requests_command(server, client, cmd);
            break;
            
        case CMD_SEND_OFFLINE_MSG:
            cmd_code = "SEND_OFFLINE_MSG";
            snprintf(cmd_detail, sizeof(cmd_detail), "to=%s, len=%zu", 
                    cmd->target_user, strlen(cmd->message));
            {
                char *response = build_response(500, "Command not implemented yet");
                send_and_free(client, response);
            }
            break;
            
        default:
            cmd_code = "UNKNOWN";
            strcpy(cmd_detail, "invalid_command");
            {
                char *response = build_simple_response(STATUS_UNDEFINED_ERROR);
                send_and_free(client, response);
            }
            break;
    }
    
    // Get actual response code from client session
    if (client->last_response_code != initial_response_code) {
        snprintf(result_code, sizeof(result_code), "%d", client->last_response_code);
        
        // Map status code to result detail
        if (client->last_response_code >= 100 && client->last_response_code < 200) {
            strcpy(result_detail, "Success");
        } else if (client->last_response_code >= 200 && client->last_response_code < 400) {
            strcpy(result_detail, "Client/Auth error");
        } else if (client->last_response_code >= 400 && client->last_response_code < 500) {
            strcpy(result_detail, "Database/Server error");
        } else if (client->last_response_code >= 500) {
            strcpy(result_detail, "System error");
        }
    }
    
    // Log the activity
    log_activity(log_username, cmd_code, cmd_detail, result_code, result_detail);
    
    free_parsed_command(cmd);
}
