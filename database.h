#ifndef DATABASE_H
#define DATABASE_H

#include <libpq-fe.h>

// Database configuration - LOCAL WSL
#define PG_HOST "localhost"
#define PG_PORT "5432"
#define PG_USER "rin"
#define PG_PASS "admin"
#define PG_DBNAME "network"

// Function prototypes
PGconn* connect_to_database();
void disconnect_database(PGconn *conn);
void print_connection_info(PGconn *conn);
int execute_query(PGconn *conn, const char *query);
PGresult* execute_query_with_result(PGconn *conn, const char *query);

// Table management
int create_all_tables(PGconn *conn);
int drop_all_tables(PGconn *conn);

// User management
int user_exists(PGconn *conn, const char *username);
int register_user(PGconn *conn, const char *username, const char *password);
int verify_login(PGconn *conn, const char *username, const char *password);

// Data viewing
void show_users(PGconn *conn);
void show_friends(PGconn *conn);
void show_groups(PGconn *conn);
void show_group_members(PGconn *conn);

#endif