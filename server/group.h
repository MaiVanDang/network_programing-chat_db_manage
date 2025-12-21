#ifndef GROUP_H
#define GROUP_H

void handle_group_create_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_group_invite_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_group_kick_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_group_leave_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_group_msg_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_group_join_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_group_approve_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_group_reject_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_list_join_requests_command(Server *server, ClientSession *client, ParsedCommand *cmd);

#endif
