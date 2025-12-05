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
void print_main_menu();
void print_auth_menu();
void print_friend_menu();
void print_group_menu();

// Handlers
int handle_register(ClientConn *client);
int handle_login(ClientConn *client);
int handle_logout(ClientConn *client);
void handle_friend_req(ClientConn *client);
void handle_friend_accept(ClientConn *client);
void handle_friend_decline(ClientConn *client);
void handle_friend_remove(ClientConn *client);
void handle_friend_list(ClientConn *client);
void handle_send_message(ClientConn *client);
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
    int messages_processed = 0;
    while ((message = stream_buffer_extract_message(client->recv_buffer)) != NULL) {
        printf("[Server] %s\n", message);
        free(message);
        messages_processed++;
    }
    
    return messages_processed;
}

// ============================================================================
// Menu Display Functions
// ============================================================================

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
    printf("Your choice: ");
}

void print_auth_menu() {
    printf("\n");
    printf("=== AUTHENTICATION ===\n");
    printf("1. Register\n");
    printf("2. Login\n");
    printf("3. Logout\n");
    printf("4. Back to main menu\n");
    printf("======================\n");
    printf("Your choice: ");
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
    printf("Your choice: ");
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
    printf("Your choice: ");
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

void handle_friend_req(ClientConn *client) {
    /**
    TO-DO 
    */
}

void handle_friend_accept(ClientConn *client) {
    /**
    TO-DO 
    */
}

void handle_friend_decline(ClientConn *client) {
    /**
    TO-DO 
    */
}

void handle_friend_remove(ClientConn *client) {
    /**
    TO-DO 
    */
}

void handle_friend_list(ClientConn *client) {
    /**
    TO-DO 
    */
}

// ============================================================================
// Messaging Handler
// ============================================================================

void handle_send_message(ClientConn *client) {
    /**
    TO-DO 
    */
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
    /**
    TO-DO 
    */
}

void handle_group_join(ClientConn *client) {
    /**
    TO-DO 
    */
}

void handle_group_leave(ClientConn *client) {
    /**
    TO-DO 
    */
}

void handle_group_kick(ClientConn *client) {
    /**
    TO-DO 
    */
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
        
        int choice;
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            printf("Invalid input!\n");
            continue;
        }
        while (getchar() != '\n');

        switch (choice) {
            case 1: { // Authentication
                int auth_continue = 1;
                while (auth_continue && global_client.connected) {
                    check_server_messages(&global_client);
                    
                    print_auth_menu();
                    int auth_choice;
                    if (scanf("%d", &auth_choice) != 1) {
                        while (getchar() != '\n');
                        printf("Invalid input!\n");
                        continue;
                    }
                    while (getchar() != '\n');
                    
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
                /**
			    TO-DO 
			    */
                break;
            }
            
            case 3:{ // Send Message
                /**
			    TO-DO 
			    */
			}
                break;
        
            case 4: { // Group Chat
                int group_continue = 1;
				while(group_continue && global_client.connected) {
					check_server_messages(&global_client);
					
					print_group_menu();
                    int group_choice;
                    if (scanf("%d", &group_choice) != 1) {
                        while (getchar() != '\n');
                        printf("Invalid input!\n");
                        continue;
                    }
                    
                    while (getchar() != '\n');
                    
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
