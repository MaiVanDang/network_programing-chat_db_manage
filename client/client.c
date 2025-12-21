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

#define BUFFER_SIZE 4096
#define INITIAL_BUFFER_SIZE 64
#define BUFFER_GROW_SIZE 32

// Global variable for signal handling
int g_socket_fd = -1;

typedef struct {
    int sockfd;
    StreamBuffer *recv_buffer;
    int connected;
} ClientConn;

ClientConn global_client;

// Function prototypes
char* read_line();
void send_message(ClientConn *client, const char *message);
int handle_server_response(ClientConn *client);
int check_server_messages(ClientConn *client);
int get_menu_choice_with_notifications(ClientConn *client);
void print_main_menu();
void print_auth_menu();
void print_friend_menu();
void print_group_menu();
void display_group_invite_notification(const char *message);
void display_offline_notification(const char *message);
void display_group_kick_notification(const char *message);

// Handlers
int handle_register(ClientConn *client);
int handle_login(ClientConn *client);
int handle_logout(ClientConn *client);
int handle_friend_req(ClientConn *client);
int handle_friend_accept(ClientConn *client);
int handle_friend_decline(ClientConn *client);
int handle_friend_remove(ClientConn *client);
int handle_friend_list(ClientConn *client);
int handle_send_message(ClientConn *client);
void handle_group_create(ClientConn *client);
void handle_group_invite(ClientConn *client);
void handle_group_join(ClientConn *client);
void handle_group_leave(ClientConn *client);
void handle_group_kick(ClientConn *client);
void handle_group_msg(ClientConn *client);

void signal_handler(int signum);

// ============================================================================
// Utility Functions
// ============================================================================

void signal_handler(int signum) {
    printf("\nReceived signal %d, disconnecting...\n", signum);
    if (g_socket_fd >= 0) {
        close(g_socket_fd);
        g_socket_fd = -1;
    }
    exit(0);
}

char* read_line() {
    size_t capacity = INITIAL_BUFFER_SIZE;
    size_t length = 0;
    char* buffer = malloc(capacity);
    
    if (!buffer) {
        printf("Memory allocation failed.\n");
        return NULL;
    }
    
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
        if (length >= capacity - 1) {
            capacity += BUFFER_GROW_SIZE;
            char* newBuffer = realloc(buffer, capacity);
            if (!newBuffer) {
                free(buffer);
                printf("Memory reallocation failed.\n");
                return NULL;
            }
            buffer = newBuffer;
        }
        buffer[length++] = c;
    }
    
    buffer[length] = '\0';
    
    if (length == 0) {
        free(buffer);
        return NULL;
    }
    
    char* finalBuffer = realloc(buffer, length + 1);
    if (finalBuffer) {
        return finalBuffer;
    }
    
    return buffer;
}

void send_message(ClientConn *client, const char *message) {
    char buff[BUFFER_SIZE];
    snprintf(buff, BUFFER_SIZE, "%s%s", message, PROTOCOL_DELIMITER);
    send(client->sockfd, buff, strlen(buff), 0);
}

int handle_server_response(ClientConn *client) {
    if (!client || !client->connected) return 0;
    
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(client->sockfd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            printf("Server disconnected!\n");
        } else {
            perror("Recv error");
        }
        client->connected = 0;
        return 0;
    }
    
    buffer[bytes_received] = '\0';
    
    if (!stream_buffer_append(client->recv_buffer, buffer, bytes_received)) {
        fprintf(stderr, "Buffer overflow in client\n");
        return -1;
    }
    
    char *message;
    int messages_processed = 0;
    while ((message = stream_buffer_extract_message(client->recv_buffer)) != NULL) {
        printf("[Server] %s\n", message);
        free(message);
        messages_processed++;
    }
    
    return messages_processed > 0 ? 1 : 0;
}

int check_server_messages(ClientConn *client) {
    if (!client || !client->connected) return 0;
    
    int flags = fcntl(client->sockfd, F_GETFL, 0);
    fcntl(client->sockfd, F_SETFL, flags | O_NONBLOCK);
    
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(client->sockfd, buffer, sizeof(buffer) - 1, 0);
    
    fcntl(client->sockfd, F_SETFL, flags);
    
    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            printf("Server disconnected!\n");
            client->connected = 0;
            return -1;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        } else {
            perror("Recv error");
            client->connected = 0;
            return -1;
        }
    }
    
    buffer[bytes_received] = '\0';
    
    if (!stream_buffer_append(client->recv_buffer, buffer, bytes_received)) {
        fprintf(stderr, "Buffer overflow in client\n");
        return -1;
    }
    
    char *message;
    int notification_count = 0;
    while ((message = stream_buffer_extract_message(client->recv_buffer)) != NULL) {
        
        if (strstr(message, "GROUP_INVITE_NOTIFICATION")) {
            display_group_invite_notification(message);
            notification_count++;
        } 
        else if (strstr(message, "OFFLINE_NOTIFICATION")) {
            display_offline_notification(message);
            notification_count++;
        }
        else if (strstr(message, "GROUP_KICK_NOTIFICATION")) {
            display_group_kick_notification(message);
            notification_count++;
        }
        free(message);
    }
    
    return notification_count;
}

// ============================================================================
// Menu Display Functions
// ============================================================================

int get_menu_choice_with_notifications(ClientConn *client) {
    int choice = -1;
    int got_input = 0;
    
    printf("Your choice: ");
    fflush(stdout);
    
    while (!got_input && client->connected) {
        fd_set read_fds;
        struct timeval timeout;
        
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(client->sockfd, &read_fds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int max_fd = (client->sockfd > STDIN_FILENO) ? client->sockfd : STDIN_FILENO;
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0 && errno != EINTR) {
            perror("select() error");
            return -1;
        }
        
        if (FD_ISSET(client->sockfd, &read_fds)) {
            int notif_count = check_server_messages(client);
            if (notif_count > 0) {
                printf("\nYour choice: ");
                fflush(stdout);
            }
        }
        
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (scanf("%d", &choice) == 1) {
                got_input = 1;
            } else {
                printf("Invalid input!\nYour choice: ");
                fflush(stdout);
            }
            while (getchar() != '\n');
        }
    }
    
    return choice;
}

void print_main_menu() {
    printf("\n");
    printf("========================================\n");
    printf("           CHAT CLIENT MENU             \n");
    printf("========================================\n");
    printf("1. Authentication\n");
    printf("2. Friend Management\n");
    printf("3. Send Message\n");
    printf("4. Group Chat\n");
    printf("5. Exit\n");
    printf("========================================\n");
}

void print_auth_menu() {
    printf("\n");
    printf("=== AUTHENTICATION ===\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Logout\n");
    printf("4. Back to main menu\n");
    printf("======================\n");
}

void print_friend_menu() {
    printf("\n");
    printf("=== FRIEND MANAGEMENT ===\n");
    printf("1. Send friend request\n");
    printf("2. Accept friend request\n");
    printf("3. Decline friend request\n");
    printf("4. Remove friend\n");
    printf("5. List friends\n");
    printf("6. Back to main menu\n");
    printf("=========================\n");
}

void print_group_menu() {
    printf("\n");
    printf("=== GROUP CHAT ===\n");
    printf("1. Create group\n");
    printf("2. Invite to group\n");
    printf("3. Join group\n");
    printf("4. Leave group\n");
    printf("5. Kick from group\n");
    printf("6. Send group message\n");
    printf("7. Back to main menu\n");
    printf("==================\n");
}

void display_group_invite_notification(const char *message) {
    int group_id = 0;
    char group_name[128] = {0};
    char invited_by[64] = {0};
    char notification_msg[512] = {0};
    
    const char *id_start = strstr(message, "group_id=");
    if (id_start) {
        sscanf(id_start, "group_id=%d", &group_id);
    }
    
    const char *name_start = strstr(message, "group_name=\"");
    if (name_start) {
        name_start += 12;
        const char *name_end = strchr(name_start, '"');
        if (name_end) {
            int len = name_end - name_start;
            if (len > 0 && len < sizeof(group_name)) {
                strncpy(group_name, name_start, len);
                group_name[len] = '\0';
            }
        }
    }
    
    const char *inviter_start = strstr(message, "invited_by=\"");
    if (inviter_start) {
        inviter_start += 12;
        const char *inviter_end = strchr(inviter_start, '"');
        if (inviter_end) {
            int len = inviter_end - inviter_start;
            if (len > 0 && len < sizeof(invited_by)) {
                strncpy(invited_by, inviter_start, len);
                invited_by[len] = '\0';
            }
        }
    }
    
    printf("\n");
    printf("NEW GROUP INVITATION: \n");
    printf("- Group: %s\n", group_name);
    printf("- ID: %d\n", group_id);
    printf("- Invited by: %s\n", invited_by);
    printf("\n");
}

void display_offline_notification(const char *message) {
    char type[64] = {0};
    int group_id = 0;
    char sender[64] = {0};
    char notif_msg[512] = {0};
    char time_str[64] = {0};
    
    const char *type_start = strstr(message, "type=\"");
    if (type_start) {
        type_start += 6;
        const char *type_end = strchr(type_start, '"');
        if (type_end) {
            int len = type_end - type_start;
            if (len > 0 && len < sizeof(type)) {
                strncpy(type, type_start, len);
                type[len] = '\0';
            }
        }
    }
    
    const char *id_start = strstr(message, "group_id=");
    if (id_start) {
        sscanf(id_start, "group_id=%d", &group_id);
    }
    
    const char *sender_start = strstr(message, "sender=\"");
    if (sender_start) {
        sender_start += 8;
        const char *sender_end = strchr(sender_start, '"');
        if (sender_end) {
            int len = sender_end - sender_start;
            if (len > 0 && len < sizeof(sender)) {
                strncpy(sender, sender_start, len);
                sender[len] = '\0';
            }
        }
    }
    
    const char *msg_start = strstr(message, "message=\"");
    if (msg_start) {
        msg_start += 9;
        const char *msg_end = strchr(msg_start, '"');
        if (msg_end) {
            int len = msg_end - msg_start;
            if (len > 0 && len < sizeof(notif_msg)) {
                strncpy(notif_msg, msg_start, len);
                notif_msg[len] = '\0';
            }
        }
    }
    
    printf("\n");
    printf("OFFLINE NOTIFICATION: \n");
    printf("- Message: %s\n", notif_msg);
    printf("\n");
}

void display_group_kick_notification(const char *message) {
    int group_id = 0;
    char group_name[128] = {0};
    char kicked_by[64] = {0};
    char notification_msg[512] = {0};
    
    const char *id_start = strstr(message, "group_id=");
    if (id_start) {
        sscanf(id_start, "group_id=%d", &group_id);
    }
    
    const char *name_start = strstr(message, "group_name=\"");
    if (name_start) {
        name_start += 12;
        const char *name_end = strchr(name_start, '"');
        if (name_end) {
            int len = name_end - name_start;
            if (len > 0 && len < sizeof(group_name)) {
                strncpy(group_name, name_start, len);
                group_name[len] = '\0';
            }
        }
    }
    
    const char *kicker_start = strstr(message, "kicked_by=\"");
    if (kicker_start) {
        kicker_start += 11;
        const char *kicker_end = strchr(kicker_start, '"');
        if (kicker_end) {
            int len = kicker_end - kicker_start;
            if (len > 0 && len < sizeof(kicked_by)) {
                strncpy(kicked_by, kicker_start, len);
                kicked_by[len] = '\0';
            }
        }
    }
    
    printf("\n");
    printf("GROUP KICK NOTIFICATION: \n");
    printf("- Group: %s\n", group_name);
    printf("- ID: %d\n", group_id);
    printf("- Kicked by: %s\n", kicked_by);
    printf("\n");
}

// ============================================================================
// Authentication Handlers
// ============================================================================

int handle_register(ClientConn *client) {
    printf("\n--- REGISTER ---\n");
    printf("Enter username: ");
    char *username = read_line();
    if (!username || strlen(username) == 0) {
        printf("Username cannot be empty!\n");
        if (username) free(username);
        return 1;
    }
    
    printf("Enter password: ");
    char *password = read_line();
    if (!password || strlen(password) == 0) {
        printf("Password cannot be empty!\n");
        free(username);
        if (password) free(password);
        return 1;
    }
    
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "REGISTER %s %s", username, password);
    send_message(client, message);
    
    int result = handle_server_response(client);
    free(username);
    free(password);
    return result;
}

int handle_login(ClientConn *client) {
    printf("\n--- LOGIN ---\n");
    printf("Enter username: ");
    char *username = read_line();
    if (!username || strlen(username) == 0) {
        printf("Username cannot be empty!\n");
        if (username) free(username);
        return 1;
    }
    
    printf("Enter password: ");
    char *password = read_line();
    if (!password || strlen(password) == 0) {
        printf("Password cannot be empty!\n");
        free(username);
        if (password) free(password);
        return 1;
    }
    
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "LOGIN %s %s", username, password);
    send_message(client, message);
    
    int result = handle_server_response(client);

    if (result > 0) {
        sleep(1);
        check_server_messages(client);
    }

    free(username);
    free(password);
    return result;
}

int handle_logout(ClientConn *client) {
    printf("\n--- LOGOUT ---\n");
    send_message(client, "LOGOUT");
    return handle_server_response(client);
}

// ============================================================================
// Friend Management Handlers
// ============================================================================

int handle_friend_req(ClientConn *client) {
    printf("\n--- SEND FRIEND REQUEST ---\n");
    printf("Enter username to send friend request: ");
    
    char *username = read_line();
    if (!username || strlen(username) == 0) {
        printf("Username cannot be empty!\n");
        if (username) free(username);
        return 1;
    }
    
    // Tạo message theo protocol: FRIEND_REQ <username>
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "FRIEND_REQ %s", username);
    
    // Gửi message đến server
    send_message(client, message);
    printf("Sending friend request to %s...\n", username);
    
    // Nhận response từ server
    int result = handle_server_response(client);
    
    free(username);
    return result;
}

int handle_friend_accept(ClientConn *client) {
    printf("\n--- ACCEPT FRIEND REQUEST ---\n");

    send_message(client, "FRIEND_PENDING");
    handle_server_response(client);
    printf("\n");

    printf("Enter username to accept friend request from: ");
    
    char *username = read_line();
    if (!username || strlen(username) == 0) {
        printf("Username cannot be empty!\n");
        if (username) free(username);
        return 1;
    }
    
    // Trim whitespace
    char *trimmed = username;
    while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
    
    // Tạo message theo protocol: FRIEND_ACCEPT <username>
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "FRIEND_ACCEPT %s", trimmed);
    
    // Gửi message đến server
    send_message(client, message);
    printf("Accepting friend request from %s...\n", trimmed);
    
    // Nhận response từ server
    int result = handle_server_response(client);
    
    free(username);
    return result;
}

int handle_friend_decline(ClientConn *client) {
    printf("\n--- DECLINE FRIEND REQUEST ---\n");

    send_message(client, "FRIEND_PENDING");
    handle_server_response(client);
    printf("\n");
    
    printf("Enter username to decline friend request from: ");
    
    char *username = read_line();
    if (!username || strlen(username) == 0) {
        printf("Username cannot be empty!\n");
        if (username) free(username);
        return 1;
    }
    
    // Trim whitespace
    char *trimmed = username;
    while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
    
    // Tạo message theo protocol: FRIEND_DECLINE <username>
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "FRIEND_DECLINE %s", trimmed);
    
    // Gửi message đến server
    send_message(client, message);
    printf("Declining friend request from %s...\n", trimmed);
    
    // Nhận response từ server
    int result = handle_server_response(client);
    
    free(username);
    return result;
}

int handle_friend_remove(ClientConn *client) {
    printf("\n--- REMOVE FRIEND ---\n");

    // Hiển thị danh sách bạn bè trước
    printf("\nFetching your friend list...\n");
    send_message(client, "FRIEND_LIST");
    handle_server_response(client);
    printf("\n");
    
    // Nhập username muốn hủy kết bạn
    printf("Enter username to remove from friend list (or press Enter to cancel): ");
    
    char *username = read_line();
    if (!username || strlen(username) == 0) {
        printf("Cancelled.\n");
        if (username) free(username);
        return 1;
    }
    
    // Trim whitespace
    char *trimmed = username;
    while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
    
    // Xác nhận
    printf("\nWARNING: Are you sure you want to remove '%s' from your friend list? (y/n): ", trimmed);
    char confirm[10];
    if (fgets(confirm, sizeof(confirm), stdin)) {
        if (confirm[0] != 'y' && confirm[0] != 'Y') {
            printf("Cancelled.\n");
            free(username);
            return 1;
        }
    }
    
    // Tạo message theo protocol: FRIEND_REMOVE <username>
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "FRIEND_REMOVE %s", trimmed);
    
    // Gửi message đến server
    send_message(client, message);
    printf("Removing friend %s...\n", trimmed);
    
    // Nhận response từ server
    int result = handle_server_response(client);
    
    free(username);
    return result;
}

int handle_friend_list(ClientConn *client) {
    printf("\n--- MY FRIEND LIST ---\n");
    
    // Gửi command đến server
    send_message(client, "FRIEND_LIST");
    printf("Fetching friend list...\n");
    
    // Nhận và hiển thị danh sách
    int result = handle_server_response(client);
    
    printf("\nPress Enter to continue...");
    getchar();
    
    return result;
}

// ============================================================================
// Messaging Handler
// ============================================================================


int handle_messaging_mode(ClientConn *client) {
    printf("\n--- DIRECT MESSAGING MODE ---\n");
    
    printf("Enter receiver username: ");
    char *receiver = read_line();
    if (!receiver || strlen(receiver) == 0) {
        printf("Username cannot be empty!\n");
        if (receiver) free(receiver);
        return 1;
    }
    
    char *trimmed_receiver = receiver;
    while (*trimmed_receiver == ' ' || *trimmed_receiver == '\t') 
        trimmed_receiver++;
    
    char get_offline_cmd[BUFFER_SIZE];
    snprintf(get_offline_cmd, sizeof(get_offline_cmd), "GET_OFFLINE_MSG %s", trimmed_receiver);
    send_message(client, get_offline_cmd);

    sleep(1);
    check_server_messages(client);

    printf("\n--- Chatting with: %s ---\n", trimmed_receiver);
    printf("--- Type 'exit' to leave chat ---\n\n");
    
    printf("[\033[32mYou\033[0m]: ");
    fflush(stdout);
    
    fd_set read_fds;
    struct timeval timeout;
    char input_buffer[MAX_MESSAGE_LENGTH];
    
    while (client->connected) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(client->sockfd, &read_fds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int max_fd = (client->sockfd > STDIN_FILENO) ? client->sockfd : STDIN_FILENO;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            perror("select() error");
            break;
        }
        
        if (FD_ISSET(client->sockfd, &read_fds)) {
            char buffer[BUFFER_SIZE];
            int bytes_received = recv(client->sockfd, buffer, sizeof(buffer) - 1, 0);
            
            if (bytes_received <= 0) {
                if (bytes_received == 0) {
                    printf("\nServer disconnected!\n");
                } else {
                    perror("recv() error");
                }
                client->connected = 0;
                break;
            }
            
            buffer[bytes_received] = '\0';
            
            if (!stream_buffer_append(client->recv_buffer, buffer, bytes_received)) {
                fprintf(stderr, "Buffer overflow\n");
                break;
            }
            
            char *message;
            while ((message = stream_buffer_extract_message(client->recv_buffer)) != NULL) {

                if (strstr(message, "GROUP_INVITE_NOTIFICATION")) {
                    printf("\r\033[K"); 
                    display_group_invite_notification(message);
                    printf("[\033[32mYou\033[0m]: ");
                    fflush(stdout);
                }
                else if (strstr(message, "OFFLINE_NOTIFICATION")) {
                    printf("\r\033[K");
                    display_offline_notification(message);
                    printf("[\033[32mYou\033[0m]: ");
                    fflush(stdout);
                }
                else if (strstr(message, "GROUP_KICK_NOTIFICATION")) {
                    printf("\r\033[K");
                    display_group_kick_notification(message);
                    printf("[\033[32mYou\033[0m]: ");
                    fflush(stdout);
                }

                else if (strstr(message, "NEW_MESSAGE from") && strstr(message, trimmed_receiver)) {
                    char *msg_start = strstr(message, ": ");
                    if (msg_start) {
                        msg_start += 2;
                        printf("\r\033[K");
                        printf("[\033[36m%s\033[0m]: %s\n", trimmed_receiver, msg_start);
                        printf("[\033[32mYou\033[0m]: ");
                        fflush(stdout);
                    }
                }
                if (strstr(message, "303")) {
                    printf("\r\033[K");
                    printf("User '%s' not found!\n", trimmed_receiver);
                    printf("[\033[32mYou\033[0m]: ");
                    fflush(stdout);
                }
                if (strstr(message, "404")) {
                    printf("\r\033[K");
                    printf("User '%s' is offline. Message will be delivered when they are online.\n", trimmed_receiver);
                    printf("[\033[32mYou\033[0m]: ");
                    fflush(stdout);
                }
                if (strstr(message, "403")) {
                    printf("\r\033[K");
                    printf("You are not friends with '%s'. Cannot send message.\n", trimmed_receiver);
                    printf("[\033[32mYou\033[0m]: ");
                    fflush(stdout);
                }
                free(message);
            }
        }
        
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(input_buffer, sizeof(input_buffer), stdin) != NULL) {
                size_t len = strlen(input_buffer);
                if (len > 0 && input_buffer[len - 1] == '\n') {
                    input_buffer[len - 1] = '\0';
                    len--;
                }
                
                if (strcmp(input_buffer, "exit") == 0) {
                    printf("\nExiting chat with %s...\n", trimmed_receiver);
                    break;
                }
                
                if (len == 0) {
                    printf("[\033[32mYou\033[0m]: ");
                    fflush(stdout);
                    continue;
                }
                
                if (len > MAX_MESSAGE_LENGTH - 1) {
                    printf("\r\033[K");
                    printf("Message too long! Maximum %d characters.\n", MAX_MESSAGE_LENGTH - 1);
                    printf("[\033[32mYou\033[0m]: ");
                    fflush(stdout);
                    continue;
                }
                
                char message[BUFFER_SIZE];
                snprintf(message, BUFFER_SIZE, "MSG %s %s", trimmed_receiver, input_buffer);
                send_message(client, message);
                
                printf("\r\033[K");
                printf("[\033[32mYou\033[0m]: ");
                fflush(stdout);
            }
        }
    }
    
    free(receiver);
    return 0;
}

// ============================================================================
// Group Chat Handlers
// ============================================================================

void handle_group_create(ClientConn *client) {
    printf("\n--- CREATE GROUP ---\n");
    printf("Enter group name: ");
    char *group_name = read_line();
    if (!group_name || strlen(group_name) == 0) {
        printf("Group name cannot be empty!\n");
        if (group_name) free(group_name);
        return;
    }
    
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "GROUP_CREATE %s", group_name);
    send_message(client, message);
    
    handle_server_response(client);
    free(group_name);
}

void handle_group_invite(ClientConn *client) {
    printf("\n--- INVITE TO GROUP ---\n");
    printf("Enter group name: ");
    char *group_name = read_line();
    if (!group_name || strlen(group_name) == 0) {
        printf("Group name cannot be empty!\n");
        if (group_name) free(group_name);
        return;
    }
    
    printf("Enter username: ");
    char *username = read_line();
    if (!username || strlen(username) == 0) {
        printf("Username cannot be empty!\n");
        free(group_name);
        if (username) free(username);
        return;
    }
    
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "GROUP_INVITE %s %s", group_name, username);
    send_message(client, message);
    
    handle_server_response(client);
    free(group_name);
    free(username);
}

void handle_group_join(ClientConn *client) {
    /**
    TO-DO 
    */
}

void handle_group_leave(ClientConn *client) {
    printf("\n--- LEAVE GROUP ---\n");
    printf("Enter group name: ");
    char *group_name = read_line();
    if (!group_name || strlen(group_name) == 0) {
        printf("Group name cannot be empty!\n");
        if (group_name) free(group_name);
        return;
    }
    
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "GROUP_LEAVE %s", group_name);
    send_message(client, message);
    
    handle_server_response(client);
    free(group_name);
}

void handle_group_kick(ClientConn *client) {
    printf("\n--- KICK FROM GROUP ---\n");
    printf("Enter group name: ");
    char *group_name = read_line();
    if (!group_name || strlen(group_name) == 0) {
        printf("Group name cannot be empty!\n");
        if (group_name) free(group_name);
        return;
    }
    
    printf("Enter username: ");
    char *username = read_line();
    if (!username || strlen(username) == 0) {
        printf("Username cannot be empty!\n");
        free(group_name);
        if (username) free(username);
        return;
    }
    
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "GROUP_KICK %s %s", group_name, username);
    send_message(client, message);
    
    handle_server_response(client);
    free(group_name);
    free(username);
}

void handle_group_msg(ClientConn *client) {
    /**
    TO-DO 
    */
}

// ============================================================================
// Main Loop
// ============================================================================

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: ./chat_client IP_Addr Port_Number\n");
        return 1;
    }
    
    char *server_addr_str = argv[1];
    int server_port = atoi(argv[2]);

    int client_sock;
    struct sockaddr_in server_addr;

    if ((client_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket() error");
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    
    if (inet_pton(AF_INET, server_addr_str, &server_addr.sin_addr) <= 0) {
        perror("inet_pton() error");
        exit(1);
    }
    
    if (connect(client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect() error");
        exit(1);
    }

    printf("\n========================================\n");
    printf("       Chat Client - Network Project\n");
    printf("========================================\n");
    printf("Connected to server %s:%d\n", server_addr_str, server_port);

    global_client.sockfd = client_sock;
    global_client.recv_buffer = stream_buffer_create();
    global_client.connected = 1;
    g_socket_fd = client_sock;
    
    if (!global_client.recv_buffer) {
        fprintf(stderr, "Failed to create stream buffer\n");
        close(client_sock);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Waiting for welcome message...\n");
    if (handle_server_response(&global_client) <= 0) {
        fprintf(stderr, "Failed to receive welcome message\n");
        stream_buffer_destroy(global_client.recv_buffer);
        close(client_sock);
        return 1;
    }

    while (global_client.connected) {
        check_server_messages(&global_client);
        
        print_main_menu();
        int choice = get_menu_choice_with_notifications(&global_client);
        if (choice < 0) {
            break;
        }

        switch (choice) {
            case 1: { // Authentication
                int auth_continue = 1;
                while (auth_continue && global_client.connected) {
                    check_server_messages(&global_client);
                    
                    print_auth_menu();
                    int auth_choice = get_menu_choice_with_notifications(&global_client);
                    if (auth_choice < 0) break;
                    
                    switch (auth_choice) {
                        case 1: handle_register(&global_client); break;
                        case 2: handle_login(&global_client); break;
                        case 3: handle_logout(&global_client); break;
                        case 4: auth_continue = 0; break;
                        default: printf("Invalid choice!\n");
                    }
                }
                break;
            }
            
            case 2: { // Friend Management
                int friend_continue = 1;
                while (friend_continue && global_client.connected) {
                    check_server_messages(&global_client);
                    
                    print_friend_menu();
                    int friend_choice = get_menu_choice_with_notifications(&global_client);
                    if (friend_choice < 0) break;
                    
                    switch (friend_choice) {
                        case 1: handle_friend_req(&global_client); break;
                        case 2: handle_friend_accept(&global_client); break;
                        case 3: handle_friend_decline(&global_client); break;
                        case 4: handle_friend_remove(&global_client); break;
                        case 5: handle_friend_list(&global_client); break;
                        case 6: friend_continue = 0; break;
                        default: printf("Invalid choice!\n");
                    }
                }
                break;
            }
            
            case 3: { // Send Message
                handle_messaging_mode(&global_client);
                break;
            }
        
            case 4: { // Group Chat
                int group_continue = 1;
				while(group_continue && global_client.connected) {
					check_server_messages(&global_client);
					
					print_group_menu();
                    int group_choice = get_menu_choice_with_notifications(&global_client);
                    if (group_choice < 0) break;
                    
                    switch (group_choice) {
                        case 1: handle_group_create(&global_client); break;
                        case 2: handle_group_invite(&global_client); break;
                        case 3: handle_group_join(&global_client); break;
                        case 4: handle_group_leave(&global_client); break;
                        case 5: handle_group_kick(&global_client); break;
                        case 6: handle_group_msg(&global_client); break;
                        case 7: group_continue = 0; break;
                        default: printf("Invalid choice!\n");
                    }
				}
                break;
            }
            
            case 5: // Exit
                printf("Closing connection...\n");
                if (global_client.recv_buffer) {
                    stream_buffer_destroy(global_client.recv_buffer);
                }
                close(client_sock);
                return 0;
        
            default:
                printf("Invalid choice!\n");
        }
        
        if (!global_client.connected) {
            printf("Server disconnected. Exiting...\n");
            break;
        }
    }

    if (global_client.recv_buffer) {
        stream_buffer_destroy(global_client.recv_buffer);
    }
    close(client_sock);
    return 0;
}
