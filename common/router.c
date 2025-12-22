#include "../database/database.h"
#include "../server/server.h"
#include "../server/group.h"
#include "../server/auth.h"
#include "../server/friend.h"
#include "../server/message.h"
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
        return;
    }
    
    switch (cmd->cmd_type) {
        case CMD_REGISTER:
            handle_register_command(server, client, cmd);
            break;
            
        case CMD_LOGIN:
            handle_login_command(server, client, cmd);
            break;
            
        case CMD_LOGOUT:
            handle_logout_command(server, client, cmd);
            break;
            
        case CMD_FRIEND_REQ:
        	handle_friend_request(server, client, cmd);
            break;

        case CMD_FRIEND_ACCEPT:
        	handle_friend_accept(server, client, cmd);
            break;
            
        case CMD_FRIEND_PENDING:
        	handle_friend_pending(server, client, cmd);
            break;
            
        case CMD_FRIEND_DECLINE:
        	handle_friend_decline(server, client, cmd);
            break;
            
        case CMD_FRIEND_REMOVE:
        	handle_friend_remove(server, client, cmd);
            break;

        case CMD_FRIEND_LIST:
        	handle_friend_list(server, client);
            break;

        case CMD_MSG:
            handle_send_message(server, client, cmd);
            break;

        case CMD_GET_OFFLINE_MSG:  // Thêm mới
            handle_get_offline_messages(server, client, cmd);
            break;
        	
        case CMD_GROUP_CREATE:
        	handle_group_create_command(server, client, cmd);
            break;
            
        case CMD_GROUP_INVITE:
        	handle_group_invite_command(server, client, cmd);
            break;
            
        case CMD_GROUP_JOIN:
        	handle_group_join_command(server, client, cmd);
            break;
        	
        case CMD_GROUP_LEAVE:
        	handle_group_leave_command(server, client, cmd);
            break;
        	
        case CMD_GROUP_KICK:
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
            {
                char *response = build_response(500, "Command not implemented yet");
                server_send_response(client, response);
                free(response);
            }
            break;
            
        default:
            {
                char *response = build_simple_response(STATUS_UNDEFINED_ERROR);
                server_send_response(client, response);
                free(response);
            }
            break;
    }
    
    free_parsed_command(cmd);
}
