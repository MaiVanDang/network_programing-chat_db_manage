#ifndef SERVER_H
#define SERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <libpq-fe.h>
#include "protocol.h"

#define MAX_CLIENTS 100
#define PORT 8888
#define BACKLOG 10

// Client session structure
typedef struct {
    int socket_fd;
    int user_id;
    char username[MAX_USERNAME_LENGTH];
    int is_authenticated;
    StreamBuffer *recv_buffer;
    time_t last_activity;
} ClientSession;

// Server structure
typedef struct {
    int listen_fd;
    int max_fd;
    fd_set master_set;
    fd_set read_fds;
    ClientSession *clients[MAX_CLIENTS];
    PGconn *db_conn;
    int running;
} Server;

// Server lifecycle functions
Server* server_create(int port);
void server_destroy(Server *server);
int server_start(Server *server);
void server_stop(Server *server);
void server_run(Server *server);

// Client management
ClientSession* client_session_create(int socket_fd);
void client_session_destroy(ClientSession *session);
int server_add_client(Server *server, int socket_fd);
void server_remove_client(Server *server, int socket_fd);
ClientSession* server_get_client_by_fd(Server *server, int socket_fd);
ClientSession* server_get_client_by_username(Server *server, const char *username);

// Network I/O
int server_accept_connection(Server *server);
int server_receive_data(Server *server, ClientSession *client);
int server_send_response(ClientSession *client, const char *response);
int server_broadcast_to_group(Server *server, int group_id, const char *message, int exclude_fd);

// Command handlers
void server_handle_client_message(Server *server, ClientSession *client, const char *message);
void handle_register_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_login_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_logout_command(Server *server, ClientSession *client, ParsedCommand *cmd);

#endif
