#include "server.h"
#include "../database/database.h"
#include "../common/router.h" 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#endif
#include <time.h>

// ============================================================================
// Logging Functions
// ============================================================================

void log_activity(const char *username, const char *cmd_code, const char *cmd_detail, 
                  const char *result_code, const char *result_detail) {
    FILE *log_file = fopen("log.txt", "a");
    if (!log_file) {
        perror("Failed to open log.txt");
        return;
    }
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%d/%m/%Y %H:%M:%S", tm_info);
    
    // Format: [timestamp]$username$command_code:command_detail$result_code:result_detail
    fprintf(log_file, "[%s]$%s$%s:%s$%s:%s\n", 
            timestamp, 
            username ? username : "Guest", 
            cmd_code ? cmd_code : "UNKNOWN",
            cmd_detail ? cmd_detail : "",
            result_code ? result_code : "ERROR",
            result_detail ? result_detail : "");
    
    fclose(log_file);
}

// ============================================================================
// TASK 2
// ============================================================================

/**
 * @functon server_create: Create and initialize a new server instance
 * 
 * @param port The port number on which the server will listen for incoming connections
 * 
 * @return Pointer to the created Server instance, or NULL on failure
 */
Server* server_create(int port) {
    Server *server = (Server*)malloc(sizeof(Server));
    if (!server) {
        perror("Failed to allocate server");
        return NULL;
    }
    
    memset(server, 0, sizeof(Server));
    server->running = 0;
    server->max_fd = 0;
    FD_ZERO(&server->master_set);
    FD_ZERO(&server->read_fds);
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        server->clients[i] = NULL;
    }
    
    // Step 1: Contruct a TCP socket to listen connection request
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        perror("Socket creation failed");
        free(server);
        return NULL;
    }
    
    int opt = 1;
    if (setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        close(server->listen_fd);
        free(server);
        return NULL;
    }
    
    // Step 2: Bind address to socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    
    
    if (bind(server->listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server->listen_fd);
        free(server);
        return NULL;
    }
    
    // Step 3: Listen request from clients
    if (listen(server->listen_fd, BACKLOG) < 0) {
        perror("Listen failed");
        close(server->listen_fd);
        free(server);
        return NULL;
    }
    
    FD_SET(server->listen_fd, &server->master_set);
    server->max_fd = server->listen_fd;
    
    server->db_conn = connect_to_database();
    if (!server->db_conn) {
        fprintf(stderr, "Failed to connect to database\n");
        close(server->listen_fd);
        free(server);
        return NULL;
    }
    
    printf("Server created on port %d\n", port);
    return server;
}

/**
 * @function server_destroy: Clean up and free resources associated with the server
 * 
 * @param server Pointer to the Server instance to be destroyed
 * 
 * @return void
 */
void server_destroy(Server *server) {
    if (!server) return;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i]) {
            client_session_destroy(server->clients[i]);
            server->clients[i] = NULL;
        }
    }
    
    if (server->listen_fd >= 0) {
        close(server->listen_fd);
    }
    
    if (server->db_conn) {
        disconnect_database(server->db_conn);
    }
    
    free(server);
    printf("Server destroyed\n");
}

/**
 * @function server_start: Start the server to accept and handle client connections
 * 
 * @param server Pointer to the Server instance to be started
 * 
 * @return 1 on success, 0 on failure
 */
int server_start(Server *server) {
    if (!server) return 0;
    
    server->running = 1;
    printf("Server started and listening...\n");
    return 1;
}

/**
 * @function server_stop: Stop the server from accepting new connections and processing data
 * 
 * @param server Pointer to the Server instance to be stopped
 * 
 * @return void
 */
void server_stop(Server *server) {
    if (!server) return;
    
    server->running = 0;
    printf("Server stopping...\n");
}

/**
 * @function server_run: Main loop to run the server, accepting connections and processing data
 * 
 * @param server Pointer to the Server instance to be run
 * 
 * @return void
 */
void server_run(Server *server) {
    if (!server) return;
    
    server_start(server);
    
    while (server->running) {
        server->read_fds = server->master_set;
        
        // Time to check server running status
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(server->max_fd + 1, &server->read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("Select error");
            break;
        }
        
        if (activity == 0) {
            continue;
        }
        
        if (FD_ISSET(server->listen_fd, &server->read_fds)) {
            server_accept_connection(server);
        }
        
        for (int i = 0; i < MAX_CLIENTS; i++) {
            ClientSession *client = server->clients[i];
            if (!client) continue;
            
            if (FD_ISSET(client->socket_fd, &server->read_fds)) {
                if (server_receive_data(server, client) <= 0) {
                    printf("Client disconnected: fd=%d\n", client->socket_fd);
                    server_remove_client(server, client->socket_fd);
                }
            }
        }
    }
    
    printf("Server stopped\n");
}

/**
 * @function server_accept_connection: Accept a new client connection
 * 
 * @param server Pointer to the Server instance
 * 
 * @return The file descriptor of the accepted client socket, or -1 on failure
 */
int server_accept_connection(Server *server) {
    if (!server) return -1;
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int client_fd = accept(server->listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("Accept failed");
        return -1;
    }
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    printf("New connection from %s:%d (fd=%d)\n", 
           client_ip, ntohs(client_addr.sin_port), client_fd);
    
    if (!server_add_client(server, client_fd)) {
        fprintf(stderr, "Failed to add client, rejecting connection\n");
        close(client_fd);
        return -1;
    }
    
    // Store client IP in session
    ClientSession *client = server_get_client_by_fd(server, client_fd);
    if (client) {
        strncpy(client->client_ip, client_ip, INET_ADDRSTRLEN - 1);
        client->client_ip[INET_ADDRSTRLEN - 1] = '\0';
    }
    
    char *welcome = build_response(100, "Welcome to chat server");
    server_send_response(client, welcome);
    free(welcome);
    
    log_activity("Guest", "CONNECT", client_ip, "100", "Connection accepted");
    
    return client_fd;
}

/**
 * @function server_receive_data: Receive data from a client and process messages
 * 
 * @param server Pointer to the Server instance
 * @param client Pointer to the ClientSession instance
 * 
 * @return Number of bytes received, or -1 on error
 */
int server_receive_data(Server *server, ClientSession *client) {
    if (!server || !client) return -1;
    
    char buffer[MAX_MESSAGE_LENGTH];
    int bytes_received = recv(client->socket_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            printf("Client %d closed connection\n", client->socket_fd);
        } else {
            perror("Recv error");
        }
        return bytes_received;
    }
    
    buffer[bytes_received] = '\0';
    printf("Received %d bytes from fd=%d: %s\n", bytes_received, client->socket_fd, buffer);
    
    if (!stream_buffer_append(client->recv_buffer, buffer, bytes_received)) {
        fprintf(stderr, "Buffer overflow for client %d\n", client->socket_fd);
        return -1;
    }
    
    char *message;
    while ((message = stream_buffer_extract_message(client->recv_buffer)) != NULL) {
        printf("Processing message from fd=%d: %s\n", client->socket_fd, message);
        server_handle_client_message(server, client, message);
        free(message);
    }
    
    client->last_activity = time(NULL);
    
    return bytes_received;
}

/**
 * @function server_send_response: Send a response message to a client
 * 
 * @param client Pointer to the ClientSession instance
 * @param response The response message to send
 * 
 * @return Number of bytes sent, or -1 on error
 */
int server_send_response(ClientSession *client, const char *response) {
    if (!client || !response) return -1;
    
    // Parse status code from response (format: "STATUS_CODE message\r\n")
    int status_code = 0;
    if (sscanf(response, "%d", &status_code) == 1) {
        client->last_response_code = status_code;
    }
    
    int len = strlen(response);
    int sent = send(client->socket_fd, response, len, 0);
    
    if (sent < 0) {
        perror("Send error");
        return -1;
    }
    
    printf("Sent to fd=%d: %s", client->socket_fd, response);
    return sent;
}

// ============================================================================
// Client Session Management
// ============================================================================

/**
 * @function client_session_create: Create and initialize a new client session
 * 
 * @param socket_fd The socket file descriptor associated with the client
 * 
 * @return Pointer to the created ClientSession instance, or NULL on failure
 */
ClientSession* client_session_create(int socket_fd) {
    ClientSession *session = (ClientSession*)malloc(sizeof(ClientSession));
    if (!session) return NULL;
    
    memset(session, 0, sizeof(ClientSession));
    session->socket_fd = socket_fd;
    session->user_id = -1;
    session->is_authenticated = 0;
    session->last_response_code = 0;
    session->client_ip[0] = '\0';
    session->recv_buffer = stream_buffer_create();
    session->last_activity = time(NULL);
    
    if (!session->recv_buffer) {
        free(session);
        return NULL;
    }
    
    return session;
}

/**
 * @function client_session_destroy: Clean up and free resources associated with a client session
 * 
 * @param session Pointer to the ClientSession instance to be destroyed
 * 
 * @return void
 */
void client_session_destroy(ClientSession *session) {
    if (!session) return;
    
    if (session->socket_fd >= 0) {
        close(session->socket_fd);
    }
    
    if (session->recv_buffer) {
        stream_buffer_destroy(session->recv_buffer);
    }
    
    free(session);
}

/**
 * @function server_add_client: Add a new client session to the server
 * 
 * @param server Pointer to the Server instance
 * @param socket_fd The socket file descriptor of the new client
 * 
 * @return 1 on success, 0 on failure
 */
int server_add_client(Server *server, int socket_fd) {
    if (!server) return 0;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i] == NULL) {
            ClientSession *session = client_session_create(socket_fd);
            if (!session) return 0;
            
            server->clients[i] = session;
            
            FD_SET(socket_fd, &server->master_set);
            if (socket_fd > server->max_fd) {
                server->max_fd = socket_fd;
            }
            
            printf("Client added: fd=%d, slot=%d\n", socket_fd, i);
            return 1;
        }
    }
    
    fprintf(stderr, "Server full, cannot add more clients\n");
    return 0;
}

/**
 * @function server_remove_client: Remove a client session from the server
 * 
 * @param server Pointer to the Server instance
 * @param socket_fd The socket file descriptor of the client to be removed
 * 
 * @return void
 */
void server_remove_client(Server *server, int socket_fd) {
    if (!server) return;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i] && server->clients[i]->socket_fd == socket_fd) {
            ClientSession *client = server->clients[i];
            
            if (client->is_authenticated && client->user_id > 0) {
                char query[256];
                snprintf(query, sizeof(query),
                        "UPDATE users SET is_online = FALSE WHERE id = %d",
                        client->user_id);
                execute_query(server->db_conn, query);
                printf("User %s logged out (disconnected)\n", client->username);
            }
            
            FD_CLR(socket_fd, &server->master_set);
            
            client_session_destroy(client);
            server->clients[i] = NULL;
            
            printf("Client removed: fd=%d, slot=%d\n", socket_fd, i);
            return;
        }
    }
}

/**
 * @function server_get_client_by_fd: Retrieve a client session by its socket file descriptor
 * 
 * @param server Pointer to the Server instance
 * @param socket_fd The socket file descriptor of the client
 * 
 * @return Pointer to the ClientSession instance, or NULL if not found
 */
ClientSession* server_get_client_by_fd(Server *server, int socket_fd) {
    if (!server) return NULL;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i] && server->clients[i]->socket_fd == socket_fd) {
            return server->clients[i];
        }
    }
    
    return NULL;
}

/**
 * @function server_get_client_by_username: Retrieve a client session by its username
 * 
 * @param server Pointer to the Server instance
 * @param username The username of the client
 * 
 * @return Pointer to the ClientSession instance, or NULL if not found
 */
ClientSession* server_get_client_by_username(Server *server, const char *username) {
    if (!server || !username) return NULL;
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i] && 
            server->clients[i]->is_authenticated &&
            strcmp(server->clients[i]->username, username) == 0) {
            return server->clients[i];
        }
    }
    
    return NULL;
}
