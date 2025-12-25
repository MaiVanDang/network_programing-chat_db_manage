#ifndef CLIENT_H
#define CLIENT_H

#include "../common/protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

#define INITIAL_BUFFER_SIZE 64
#define BUFFER_GROW_SIZE 32

// ============================================================================
// Data Structures
// ============================================================================

typedef struct {
    int sockfd;
    StreamBuffer *recv_buffer;
    int connected;
} ClientConn;

typedef enum {
    MENU_AUTH = 1,
    MENU_FRIEND = 2,
    MENU_MESSAGE = 3,
    MENU_GROUP = 4,
    MENU_EXIT = 5
} MainMenuOption;

typedef enum {
    AUTH_REGISTER = 1,
    AUTH_LOGIN = 2,
    AUTH_LOGOUT = 3,
    AUTH_BACK = 4
} AuthMenuOption;

typedef enum {
    FRIEND_REQ = 1,
    FRIEND_ACCEPT = 2,
    FRIEND_DECLINE = 3,
    FRIEND_REMOVE = 4,
    FRIEND_LIST = 5,
    FRIEND_BACK = 6
} FriendMenuOption;

typedef enum {
    GROUP_CREATE = 1,
    GROUP_INVITE = 2,
    GROUP_JOIN = 3,
    GROUP_APPROVE = 4,
    GROUP_REJECT = 5,
    GROUP_LIST_REQUESTS = 6,
    GROUP_LEAVE = 7,
    GROUP_KICK = 8,
    GROUP_MSG = 9,
    GROUP_BACK = 10
} GroupMenuOption;

// ============================================================================
// Function Prototypes
// ============================================================================

// Initialization and cleanup
int client_init(ClientConn *client, const char *server_addr, int port);
void client_cleanup(ClientConn *client);

// Input/Output
char* read_line(void);
int get_menu_choice_with_notifications(ClientConn *client);

// Network communication
void send_message(ClientConn *client, const char *message);
int handle_server_response(ClientConn *client);
int check_server_messages(ClientConn *client);

// Menu displays
void print_main_menu(void);
void print_auth_menu(void);
void print_friend_menu(void);
void print_group_menu(void);

// Notification handlers
void display_group_invite_notification(const char *message);
void display_offline_notification(const char *message);
void display_group_kick_notification(const char *message);
void display_group_join_request_notification(const char *message);
void display_group_join_result_notification(const char *message, int approved);

// Authentication handlers
int handle_register(ClientConn *client);
int handle_login(ClientConn *client);
int handle_logout(ClientConn *client);

// Friend management handlers
int handle_friend_req(ClientConn *client);
int handle_friend_accept(ClientConn *client);
int handle_friend_decline(ClientConn *client);
int handle_friend_remove(ClientConn *client);
int handle_friend_list(ClientConn *client);

// Messaging handlers
int handle_messaging_mode(ClientConn *client);

// Group chat handlers
void handle_group_create(ClientConn *client);
void handle_group_invite(ClientConn *client);
void handle_group_join(ClientConn *client);
void handle_group_leave(ClientConn *client);
void handle_group_kick(ClientConn *client);
void handle_group_msg(ClientConn *client);
void handle_group_approve(ClientConn *client);
void handle_group_reject(ClientConn *client);
void handle_list_join_requests(ClientConn *client);

// Message parsing
int parse_notification_field(const char *message, const char *field_name, char *output, size_t max_len);

#endif