#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "database.h"

int main(int argc, char *argv[]) {
    PGconn *conn = connect_to_database();
    if (!conn) {
        return 1;
    }
    
    if (argc > 1 && strcmp(argv[1], "create-tables") == 0) {
        create_all_tables(conn);
    }
    else {
        printf("\n?? Database Manager Ready!\n");
        printf("Use 'make show-users' or other make commands to view data.\n");
    }
    
    disconnect_database(conn);
    return 0;
}
