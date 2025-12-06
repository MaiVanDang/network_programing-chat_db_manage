#include "../database/database.h"
#include "../server/server.h"
#include "../server/group.h"
#include "../server/auth.h" 
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
        case CMD_FRIEND_ACCEPT:
        case CMD_FRIEND_DECLINE:
        case CMD_FRIEND_REMOVE:
        case CMD_FRIEND_LIST:
        case CMD_MSG:
        	
        case CMD_GROUP_CREATE:
        	handle_group_create_command(server, client, cmd);
            break;
            
        case CMD_GROUP_INVITE:
        	handle_group_invite_command(server, client, cmd);
            break;
            
        case CMD_GROUP_JOIN:
        	
        case CMD_GROUP_LEAVE:
//        	handle_group_leave_command(server, client, cmd);
//            break;
        	
        case CMD_GROUP_KICK:
//        	handle_group_kick_command(server, client, cmd);
//            break;
            
        case CMD_GROUP_MSG:
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
