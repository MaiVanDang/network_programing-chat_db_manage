#ifndef MESSAGE_H
#define MESSAGE_H

#include "../server/server.h"
#include "../common/protocol.h"

// Hàm xử lý chính
void handle_send_message(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_get_offline_messages(Server *server, ClientSession *client, ParsedCommand *cmd);

int check_friendship(PGconn *conn, int user_id1, int user_id2);
int save_message_to_database(PGconn *conn, int sender_id, int receiver_id, const char *message_text);
int forward_message_to_online_user(Server *server, int receiver_id, const char *sender_username, const char *message_text);
ClientSession* find_client_by_user_id(Server *server, int user_id);

#endif