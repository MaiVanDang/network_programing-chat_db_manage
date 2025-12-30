// ============================================================================
// Message Router - Cleaned Version
// ============================================================================

#include "../database/database.h"
#include "../server/server.h"
#include "../server/group.h"
#include "../server/auth.h"
#include "../server/friend.h"
#include "../server/message.h"
#include "../helper/helper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @function server_handle_client_message: Routes and processes client messages.
 * 
 * @param server Pointer to the server instance.
 * @param client Pointer to the client session.
 * @param message The raw message received from the client.
 * 
 * @return void
 */
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
    const char *cmd_code = "UNKNOWN";
    char cmd_detail[512] = "";
    char result_code[16] = "0";
    char result_detail[256] = "Pending";
    
    const char *log_username = client->is_authenticated ? client->username : "Guest";
    int initial_response_code = client->last_response_code;
    
    switch (cmd->cmd_type) {
        // ====================================================================
        // Authentication Commands
        // ====================================================================
        case CMD_REGISTER:
            cmd_code = "REGISTER";
            snprintf(cmd_detail, sizeof(cmd_detail), "username=%s", cmd->username);
            handle_register_command(server, client, cmd);
            break;
            
        case CMD_LOGIN:
            cmd_code = "LOGIN";
            snprintf(cmd_detail, sizeof(cmd_detail), "username=%s", cmd->username);
            handle_login_command(server, client, cmd);
            log_username = client->is_authenticated ? client->username : "Guest";
            break;
            
        case CMD_LOGOUT:
            cmd_code = "LOGOUT";
            snprintf(cmd_detail, sizeof(cmd_detail), "username=%s", client->username);
            handle_logout_command(server, client, cmd);
            break;
            
        // ====================================================================
        // Friend Management Commands
        // ====================================================================
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

        // ====================================================================
        // Direct Messaging Commands
        // ====================================================================
        case CMD_MSG:
            cmd_code = "MSG";
            snprintf(cmd_detail, sizeof(cmd_detail), "to=%s, len=%zu", 
                    cmd->target_user, strlen(cmd->message));
            handle_send_message(server, client, cmd);
            break;

        case CMD_GET_OFFLINE_MSG:
            cmd_code = "GET_OFFLINE_MSG";
            snprintf(cmd_detail, sizeof(cmd_detail), "from=%s", cmd->target_user);
            handle_get_offline_messages(server, client, cmd);
            break;
            
        // ====================================================================
        // Group Management Commands
        // ====================================================================
        case CMD_GROUP_CREATE:
            cmd_code = "GROUP_CREATE";
            snprintf(cmd_detail, sizeof(cmd_detail), "name=%s", cmd->group_name);
            handle_group_create_command(server, client, cmd);
            break;
            
        case CMD_GROUP_INVITE:
            cmd_code = "GROUP_INVITE";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s, user=%s", 
                    cmd->group_name, cmd->target_user);
            handle_group_invite_command(server, client, cmd);
            break;
            
        case CMD_GROUP_JOIN:
            cmd_code = "GROUP_JOIN";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s", cmd->group_name);
            handle_group_join_command(server, client, cmd);
            break;
            
        case CMD_GROUP_LEAVE:
            cmd_code = "GROUP_LEAVE";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s", cmd->group_name);
            handle_group_leave_command(server, client, cmd);
            break;
            
        case CMD_GROUP_KICK:
            cmd_code = "GROUP_KICK";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s, user=%s", 
                    cmd->group_name, cmd->target_user);
            handle_group_kick_command(server, client, cmd);
            break;
            
        case CMD_GROUP_APPROVE:
            cmd_code = "GROUP_APPROVE";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s, user=%s", 
                    cmd->group_name, cmd->target_user);
            handle_group_approve_command(server, client, cmd);
            break;
            
        case CMD_GROUP_REJECT:
            cmd_code = "GROUP_REJECT";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s, user=%s", 
                    cmd->group_name, cmd->target_user);
            handle_group_reject_command(server, client, cmd);
            break;
            
        case CMD_LIST_JOIN_REQUESTS:
            cmd_code = "LIST_JOIN_REQUESTS";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s", cmd->group_name);
            handle_list_join_requests_command(server, client, cmd);
            break;
            
        // ====================================================================
        // Group Messaging Commands
        // ====================================================================
        case CMD_GROUP_MSG:
            cmd_code = "GROUP_MSG";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s, len=%zu", 
                    cmd->group_name, strlen(cmd->message));
            handle_group_msg_command(server, client, cmd);
            break;
            
        case CMD_GROUP_SEND_OFFLINE_MSG:
            cmd_code = "GROUP_SEND_OFFLINE_MSG";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s (enter messaging mode)", 
                    cmd->group_name);
            handle_get_group_offline_messages(server, client, cmd);
            break;
            
        case CMD_GROUP_EXIT_MESSAGING:
            cmd_code = "GROUP_EXIT_MESSAGING";
            snprintf(cmd_detail, sizeof(cmd_detail), "group=%s (exit messaging mode)", 
                    cmd->group_name);
            handle_exit_group_messaging(server, client, cmd);
            break;
            
        // ====================================================================
        // Not Implemented / Unknown Commands
        // ====================================================================
        case CMD_SEND_OFFLINE_MSG:
            cmd_code = "SEND_OFFLINE_MSG";
            snprintf(cmd_detail, sizeof(cmd_detail), "to=%s, len=%zu", 
                    cmd->target_user, strlen(cmd->message));
            {
                char *response = build_response(500, "Command not implemented");
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
    
    if (client->last_response_code != initial_response_code) {
        snprintf(result_code, sizeof(result_code), "%d", client->last_response_code);
        
        switch (client->last_response_code) {
            // Success codes (1xx)
            case 101: strcpy(result_detail, "Register Success"); break;
            case 102: strcpy(result_detail, "Login Success"); break;
            case 103: strcpy(result_detail, "Logout Success"); break;
            case 104: strcpy(result_detail, "Friend Request Sent"); break;
            case 105: strcpy(result_detail, "Friend Request Accepted"); break;
            case 106: strcpy(result_detail, "Friend Request Declined"); break;
            case 107: strcpy(result_detail, "Friend Removed"); break;
            case 108: strcpy(result_detail, "Friend List Retrieved"); break;
            case 109: strcpy(result_detail, "Message Sent"); break;
            case 110: strcpy(result_detail, "Group Created"); break;
            case 111: strcpy(result_detail, "Group Invite Sent"); break;
            case 112: strcpy(result_detail, "Group Joined"); break;
            case 113: strcpy(result_detail, "Group Left"); break;
            case 114: strcpy(result_detail, "Member Kicked"); break;
            case 115: strcpy(result_detail, "Group Message Sent"); break;
            case 116: strcpy(result_detail, "Offline Message Retrieved"); break;
            case 117: strcpy(result_detail, "Pending Requests Retrieved"); break;
            case 118: strcpy(result_detail, "Offline Messages Retrieved"); break;
            case 119: strcpy(result_detail, "Join Request Sent"); break;
            case 120: strcpy(result_detail, "Join Request Approved"); break;
            case 121: strcpy(result_detail, "Join Request Rejected"); break;
            case 122: strcpy(result_detail, "Group Message Sent Success"); break;
            
            // Client errors (2xx)
            case 201: strcpy(result_detail, "Username Already Exists"); break;
            case 202: strcpy(result_detail, "Wrong Password"); break;
            case 216: strcpy(result_detail, "Group Join Request Notification"); break;
            case 217: strcpy(result_detail, "Group Join Approved Notification"); break;
            case 218: strcpy(result_detail, "No Offline Messages"); break;
            case 219: strcpy(result_detail, "Group Join Rejected Notification"); break;
            case 250: strcpy(result_detail, "Group Invite Notification"); break;
            case 251: strcpy(result_detail, "User Offline Notification"); break;
            case 252: strcpy(result_detail, "Group Kick Notification"); break;
            
            // Auth/Session errors (3xx)
            case 301: strcpy(result_detail, "Invalid Username"); break;
            case 302: strcpy(result_detail, "Invalid Password"); break;
            case 303: strcpy(result_detail, "User Not Found"); break;
            case 304: strcpy(result_detail, "Already Logged In"); break;
            case 305: strcpy(result_detail, "Not Logged In"); break;
            case 306: strcpy(result_detail, "Already Friends"); break;
            
            // Database/Server errors (4xx)
            case 400: strcpy(result_detail, "Database Error"); break;
            case 401: strcpy(result_detail, "Request Already Pending"); break;
            case 402: strcpy(result_detail, "No Pending Request"); break;
            case 403: strcpy(result_detail, "Not Friends"); break;
            case 413: strcpy(result_detail, "User Offline"); break;
            case 414: strcpy(result_detail, "Message Too Long"); break;
            case 415: strcpy(result_detail, "Group Already Exists"); break;
            case 416: strcpy(result_detail, "Invalid Group Name"); break;
            case 417: strcpy(result_detail, "Not Group Owner"); break;
            case 418: strcpy(result_detail, "Already In Group"); break;
            case 419: strcpy(result_detail, "Group Not Found"); break;
            case 420: strcpy(result_detail, "Invite Required"); break;
            case 421: strcpy(result_detail, "Not In Group"); break;
            case 422: strcpy(result_detail, "Cannot Kick Owner"); break;
            
            // System errors (5xx)
            case 500: strcpy(result_detail, "Undefined Error"); break;
            
            default: strcpy(result_detail, "Unknown Status Code"); break;
        }
    }
    
    log_activity(log_username, cmd_code, cmd_detail, result_code, result_detail);
    
    free_parsed_command(cmd);
}