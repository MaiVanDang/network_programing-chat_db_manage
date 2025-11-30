#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "database/database.h"

int main(int argc, char *argv[]) {
    PGconn *conn = connect_to_database();
    if (!conn) {
        return 1;
    }
    
    if (argc > 1) {
        if (strcmp(argv[1], "create-tables") == 0) {
            create_all_tables(conn);
        }
        else if (strcmp(argv[1], "drop-tables") == 0) {
            drop_all_tables(conn);
        }
        else if (strcmp(argv[1], "show-users") == 0) {
            show_users(conn);
        }
        else if (strcmp(argv[1], "show-friends") == 0) {
            show_friends(conn);
        }
        else if (strcmp(argv[1], "show-groups") == 0) {
            show_groups(conn);
        }
        else if (strcmp(argv[1], "show-group-members") == 0) {
            show_group_members(conn);
        }
        else if (strcmp(argv[1], "show-messages") == 0) {
            show_messages(conn);
        }
        else {
            printf("Unknown command: %s\n", argv[1]);
            printf("Available commands:\n");
            printf("  create-tables\n");
            printf("  drop-tables\n");
            printf("  show-users\n");
            printf("  show-friends\n");
            printf("  show-groups\n");
            printf("  show-group-members\n");
            printf("  show-messages\n");
        }
    }
    else {
        printf("\n✓ Database Manager Ready!\n");
        printf("Use 'make show-users' or other make commands to view data.\n");
    }
    
    disconnect_database(conn);
    return 0;
}
