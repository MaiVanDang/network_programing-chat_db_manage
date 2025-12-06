#ifndef FRIEND_H
#define FRIEND_H

#include "../server/server.h"

// Friend Management Functions
void handle_friend_request(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_accept(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_pending(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_decline(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_remove(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_list(Server *server, ClientSession *client);

#endif // FRIEND_H