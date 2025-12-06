#ifndef GROUP_H
#define GROUP_H

void handle_group_create_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_group_invite_command(Server *server, ClientSession *client, ParsedCommand *cmd);

#endif
