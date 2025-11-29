#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "server.h"

Server *g_server = NULL;

void signal_handler(int signum) {
    printf("\n? Received signal %d, shutting down...\n", signum);
    if (g_server) {
        server_stop(g_server);
    }
}

int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("       Chat Server Starting...         \n");
    printf("========================================\n\n");
    
    int port = PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number: %s\n", argv[1]);
            return 1;
        }
    }
    
    g_server = server_create(port);
    if (!g_server) {
        fprintf(stderr, "Failed to create server\n");
        return 1;
    }
    
    signal(SIGINT, signal_handler); 
    signal(SIGTERM, signal_handler); 
    
    printf("\n");
    printf("========================================\n");
    printf("  Server Information\n");
    printf("========================================\n");
    printf("  Port:          %d\n", port);
    printf("  Max Clients:   %d\n", MAX_CLIENTS);
    printf("  Protocol:      Text-based (\\r\\n)\n");
    printf("========================================\n\n");
    
    printf("Waiting for connections...\n");
    printf("Press Ctrl+C to stop the server\n\n");
    
    server_run(g_server);
    
    server_destroy(g_server);
    g_server = NULL;
    
    printf("\n========================================\n");
    printf("       Server Shutdown Complete        \n");
    printf("========================================\n");
    
    return 0;
}
