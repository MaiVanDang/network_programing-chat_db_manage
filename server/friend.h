#ifndef FRIEND_H
#define FRIEND_H

#include "../server/server.h"

// ==================== Helper Functions ====================
int validate_authentication(ClientSession *client);
int clean_username(const char *input, char *output, size_t max_len);
int get_user_id_by_username(PGconn *db_conn, const char *username, int *user_id_out);
int check_not_self(ClientSession *client, int target_user_id, const char *error_context);
int check_friendship_status(PGconn *db_conn, int user_id1, int user_id2, const char *status_filter);
void send_error_response(ClientSession *client, int status_code, const char *message);

// ==================== Friend Management Functions ====================
void handle_friend_request(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_accept(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_pending(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_decline(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_remove(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_friend_list(Server *server, ClientSession *client);

#endif // FRIEND_H