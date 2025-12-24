#include "../server/server.h"
#include "../database/database.h"
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

/**
 * @brief Send pending notifications to client upon login
 */
void send_pending_notifications(Server *server, ClientSession *client) {
    if (!server || !client || !client->is_authenticated) return;
    
    char query[512];
    snprintf(query, sizeof(query),
            "SELECT id, notification_type, group_id, sender_username, message, created_at "
            "FROM offline_notifications "
            "WHERE user_id = %d AND notification_type != 'GROUP_MESSAGE' "
            "ORDER BY created_at ASC",
            client->user_id);
    
    PGresult *res = execute_query_with_result(server->db_conn, query);
    if (!res) return;
    
    int count = PQntuples(res);
    if (count == 0) {
        PQclear(res);
        return;
    }
    
    printf("Sending %d pending notification(s) to '%s'\n", count, client->username);
    
    for (int i = 0; i < count; i++) {
        int notif_id = atoi(PQgetvalue(res, i, 0));
        char notification[1024];
        snprintf(notification, sizeof(notification),
                "OFFLINE_NOTIFICATION type=\"%s\" group_id=%d sender=\"%s\" "
                "message=\"%s\" time=\"%s\"",
                PQgetvalue(res, i, 1), atoi(PQgetvalue(res, i, 2)),
                PQgetvalue(res, i, 3), PQgetvalue(res, i, 4), PQgetvalue(res, i, 5));
        
        char *response = build_response(STATUS_OFFLINE_NOTIFICATION, notification);
        
        if (server_send_response(client, response) > 0) {
            char delete_query[256];
            snprintf(delete_query, sizeof(delete_query),
                    "DELETE FROM offline_notifications WHERE id = %d", notif_id);
            execute_query(server->db_conn, delete_query);
        }
        free(response);
    }
    
    PQclear(res);
    printf("All pending notifications processed for '%s'\n", client->username);
}
