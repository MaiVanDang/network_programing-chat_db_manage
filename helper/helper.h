#ifndef HELPER_H
#define HELPER_H
#include "../server/server.h"

void send_and_free(ClientSession *client, char *response);
void send_pending_notifications(Server *server, ClientSession *client);

#endif