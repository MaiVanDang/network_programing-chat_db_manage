#include "../server/server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Send response and free memory - Helper chung
 */
void send_and_free(ClientSession *client, char *response) {
    server_send_response(client, response);
    free(response);
}
