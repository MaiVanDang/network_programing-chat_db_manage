#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

#define BUFFER_SIZE 4096
#define DELIMITER "\r\n"

int g_socket_fd = -1;

typedef struct {
    int socket_fd;
    char buffer[BUFFER_SIZE];
    size_t buffer_len;
    int connected;
} Client;

Client* client_create();
void client_destroy(Client *client);
int client_connect(Client *client, const char *host, int port);
void client_disconnect(Client *client);
int client_send(Client *client, const char *message);
char* client_receive(Client *client);
void client_run(Client *client);
void print_help();
void signal_handler(int signum);

// ============================================================================
// Signal Handler
// ============================================================================

void signal_handler(int signum) {
    printf("\nReceived signal %d, disconnecting...\n", signum);
    if (g_socket_fd >= 0) {
        close(g_socket_fd);
        g_socket_fd = -1;
    }
    exit(0);
}

// ============================================================================
// Client Functions
// ============================================================================

Client* client_create() {
    Client *client = (Client*)malloc(sizeof(Client));
    if (!client) {
        perror("Failed to allocate client");
        return NULL;
    }
    
    memset(client, 0, sizeof(Client));
    client->socket_fd = -1;
    client->connected = 0;
    
    return client;
}

void client_destroy(Client *client) {
    if (!client) return;
    
    if (client->connected) {
        client_disconnect(client);
    }
    
    free(client);
}

int client_connect(Client *client, const char *host, int port) {
    if (!client) return 0;
    
    printf("Connecting to %s:%d...\n", host, port);
    
    client->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client->socket_fd < 0) {
        perror("Socket creation failed");
        return 0;
    }
    
    g_socket_fd = client->socket_fd;
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(client->socket_fd);
        client->socket_fd = -1;
        return 0;
    }
    
    if (connect(client->socket_fd, (struct sockaddr*)&server_addr, 
                sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(client->socket_fd);
        client->socket_fd = -1;
        return 0;
    }
    
    client->connected = 1;
    printf("Connected successfully!\n\n");
    
    char *welcome = client_receive(client);
    if (welcome) {
        printf("Server: %s\n\n", welcome);
        free(welcome);
    }
    
    return 1;
}

void client_disconnect(Client *client) {
    if (!client || !client->connected) return;
    
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
        client->socket_fd = -1;
        g_socket_fd = -1;
    }
    
    client->connected = 0;
    printf("\nDisconnected from server\n");
}

int client_send(Client *client, const char *message) {
    if (!client || !client->connected || !message) return 0;
    
    char buffer[BUFFER_SIZE];
    snprintf(buffer, sizeof(buffer), "%s%s", message, DELIMITER);
    
    int len = strlen(buffer);
    int sent = send(client->socket_fd, buffer, len, 0);
    
    if (sent < 0) {
        perror("Send failed");
        return 0;
    }
    
    return 1;
}

char* client_receive(Client *client) {
    if (!client || !client->connected) return NULL;
    
    char temp[BUFFER_SIZE];
    int received = recv(client->socket_fd, temp, sizeof(temp) - 1, 0);
    
    if (received <= 0) {
        if (received == 0) {
            printf("\nServer closed connection\n");
        } else {
            perror("Receive failed");
        }
        client->connected = 0;
        return NULL;
    }
    
    temp[received] = '\0';
    
    if (client->buffer_len + received >= BUFFER_SIZE) {
        fprintf(stderr, "Buffer overflow\n");
        return NULL;
    }
    
    memcpy(client->buffer + client->buffer_len, temp, received);
    client->buffer_len += received;
    client->buffer[client->buffer_len] = '\0';
    
    char *delim = strstr(client->buffer, DELIMITER);
    if (!delim) {
        return NULL; 
    }
    
    size_t msg_len = delim - client->buffer;
    char *message = (char*)malloc(msg_len + 1);
    if (!message) {
        perror("Memory allocation failed");
        return NULL;
    }
    
    memcpy(message, client->buffer, msg_len);
    message[msg_len] = '\0';
    
    size_t remaining = client->buffer_len - msg_len - strlen(DELIMITER);
    memmove(client->buffer, delim + strlen(DELIMITER), remaining);
    client->buffer_len = remaining;
    client->buffer[client->buffer_len] = '\0';
    
    return message;
}

// ============================================================================
// UI Functions
// ============================================================================

void print_help() {
    printf("\n=== AVAILABLE COMMANDS ===\n\n");
    
    printf("Authentication:\n");
    printf("  REGISTER <username> <password>  - Register new account\n");
    printf("  LOGIN <username> <password>     - Login to account\n");
    printf("  LOGOUT                          - Logout from account\n\n");
    
    printf("Friend Management:\n");
    printf("  FRIEND_REQ <username>           - Send friend request\n");
    printf("  FRIEND_ACCEPT <username>        - Accept friend request\n");
    printf("  FRIEND_DECLINE <username>       - Decline friend request\n");
    printf("  FRIEND_REMOVE <username>        - Remove friend\n");
    printf("  FRIEND_LIST                     - List all friends\n\n");
    
    printf("Messaging:\n");
    printf("  MSG <username> <message>        - Send private message\n\n");
    
    printf("Group Chat:\n");
    printf("  GROUP_CREATE <groupname>        - Create new group\n");
    printf("  GROUP_INVITE <groupid> <user>   - Invite user to group\n");
    printf("  GROUP_JOIN <groupid>            - Join group\n");
    printf("  GROUP_LEAVE <groupid>           - Leave group\n");
    printf("  GROUP_KICK <groupid> <user>     - Kick user from group\n");
    printf("  GROUP_MSG <groupid> <message>   - Send group message\n\n");
    
    printf("Client Commands:\n");
    printf("  help                            - Show this help\n");
    printf("  quit                            - Exit client\n");
    printf("\n");
}

// ============================================================================
// Main Loop
// ============================================================================

void client_run(Client *client) {
    if (!client || !client->connected) {
        printf("Not connected to server\n");
        return;
    }
    
    printf("Type 'help' for available commands\n\n");
    
    char input[BUFFER_SIZE];
    
    while (client->connected) {
        printf("> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }
        
        if (strlen(input) == 0) {
            continue;
        }
        
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            printf("Goodbye!\n");
            break;
        }
        
        if (strcmp(input, "help") == 0) {
            print_help();
            continue;
        }
        
        if (!client_send(client, input)) {
            printf("Failed to send message\n");
            continue;
        }
        
        char *response = client_receive(client);
        if (response) {
            printf("Server: %s\n", response);
            free(response);
        }
        
        if (!client->connected) {
            printf("Connection lost\n");
            break;
        }
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[]) {
    char *host = "127.0.0.1";
    int port = 8888;
    
    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number: %s\n", argv[2]);
            return 1;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("\n========================================\n");
    printf("       Chat Client - Network Project\n");
    printf("========================================\n\n");
    
    Client *client = client_create();
    if (!client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    if (!client_connect(client, host, port)) {
        client_destroy(client);
        return 1;
    }
    
    client_run(client);
    
    client_disconnect(client);
    client_destroy(client);
    
    return 0;
}
