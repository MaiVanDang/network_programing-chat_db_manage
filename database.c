#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PGconn* connect_to_database() {
    char conninfo[256];
    snprintf(conninfo, sizeof(conninfo), 
             "host=%s port=%s user=%s password=%s dbname=%s",
             PG_HOST, PG_PORT, PG_USER, PG_PASS, PG_DBNAME);
    
    printf("Connecting to: %s:%s\n", PG_HOST, PG_PORT);
    
    PGconn *conn = PQconnectdb(conninfo);
    
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "Connection failed: %s\n", PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }
    
    printf("? Connected to PostgreSQL successfully!\n");
    return conn;
}

void disconnect_database(PGconn *conn) {
    if (conn) {
        PQfinish(conn);
        printf("Disconnected from database.\n");
    }
}

int execute_query(PGconn *conn, const char *query) {
    PGresult *res = PQexec(conn, query);
    
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Query failed: %s\n", PQerrorMessage(conn));
        PQclear(res);
        return 0;
    }
    
    PQclear(res);
    return 1;
}

PGresult* execute_query_with_result(PGconn *conn, const char *query) {
    PGresult *res = PQexec(conn, query);
    
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Query failed: %s\n", PQerrorMessage(conn));
        PQclear(res);
        return NULL;
    }
    
    return res;
}

int create_all_tables(PGconn *conn) {
    printf("Creating database tables...\n");
    
    const char *tables[] = {
        // Users table
        "CREATE TABLE IF NOT EXISTS users ("
        "id SERIAL PRIMARY KEY,"
        "username VARCHAR(50) UNIQUE NOT NULL,"
        "password_hash VARCHAR(128) NOT NULL,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "is_online BOOLEAN DEFAULT FALSE"
        ");",
        
        // Friends table
        "CREATE TABLE IF NOT EXISTS friends ("
        "id SERIAL PRIMARY KEY,"
        "user_id INTEGER REFERENCES users(id),"
        "friend_id INTEGER REFERENCES users(id),"
        "status VARCHAR(20) DEFAULT 'pending',"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE(user_id, friend_id)"
        ");",
        
        // Groups table
        "CREATE TABLE IF NOT EXISTS groups ("
        "id SERIAL PRIMARY KEY,"
        "group_name VARCHAR(100) NOT NULL,"
        "creator_id INTEGER REFERENCES users(id),"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");",
        
        // Group members table
        "CREATE TABLE IF NOT EXISTS group_members ("
        "id SERIAL PRIMARY KEY,"
        "group_id INTEGER REFERENCES groups(id),"
        "user_id INTEGER REFERENCES users(id),"
        "role VARCHAR(20) DEFAULT 'member',"
        "joined_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE(group_id, user_id)"
        ");",
        
        // Messages table
        "CREATE TABLE IF NOT EXISTS messages ("
        "id SERIAL PRIMARY KEY,"
        "sender_id INTEGER REFERENCES users(id),"
        "receiver_id INTEGER REFERENCES users(id),"
        "group_id INTEGER REFERENCES groups(id),"
        "content TEXT NOT NULL,"
        "is_delivered BOOLEAN DEFAULT FALSE,"
        "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");",
        
        // Indexes
        "CREATE INDEX IF NOT EXISTS idx_messages_receiver "
        "ON messages(receiver_id, is_delivered);",
        
        "CREATE INDEX IF NOT EXISTS idx_messages_group "
        "ON messages(group_id);",
        
        "CREATE INDEX IF NOT EXISTS idx_friends_user "
        "ON friends(user_id);"
    };
    
    int table_count = sizeof(tables) / sizeof(tables[0]);
    const char *table_names[] = {
        "users", "friends", "groups", "group_members", "messages",
        "idx_messages_receiver", "idx_messages_group", "idx_friends_user"
    };
    
    for (int i = 0; i < table_count; i++) {
        printf("Creating %s... ", table_names[i]);
        if (execute_query(conn, tables[i])) {
            printf("?\n");
        } else {
            printf("?\n");
            return 0;
        }
    }
    
    printf("?? All tables created successfully!\n");
    return 1;
}

int drop_all_tables(PGconn *conn) {
    printf("Dropping all tables...\n");
    
    const char *tables[] = {
        "DROP TABLE IF EXISTS messages CASCADE;",
        "DROP TABLE IF EXISTS group_members CASCADE;",
        "DROP TABLE IF EXISTS groups CASCADE;",
        "DROP TABLE IF EXISTS friends CASCADE;",
        "DROP TABLE IF EXISTS users CASCADE;"
    };
    
    int table_count = sizeof(tables) / sizeof(tables[0]);
    
    for (int i = 0; i < table_count; i++) {
        if (execute_query(conn, tables[i])) {
            printf("? Table dropped\n");
        } else {
            printf("? Failed to drop table\n");
            return 0;
        }
    }
    
    printf("??? All tables dropped successfully!\n");
    return 1;
}

void show_users(PGconn *conn) {
    printf("\n=== USERS TABLE ===\n");
    PGresult *res = execute_query_with_result(conn, 
        "SELECT id, username, is_online, created_at FROM users ORDER BY id");
    
    if (!res || PQntuples(res) == 0) {
        printf("No users found.\n");
        if (res) PQclear(res);
        return;
    }
    
    printf("%-5s %-20s %-10s %-20s\n", "ID", "Username", "Online", "Created At");
    printf("------------------------------------------------------------\n");
    
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        const char *is_online = strcmp(PQgetvalue(res, i, 2), "t") == 0 ? "Yes" : "No";
        printf("%-5s %-20s %-10s %-20s\n",
               PQgetvalue(res, i, 0),
               PQgetvalue(res, i, 1),
               is_online,
               PQgetvalue(res, i, 3));
    }
    
    PQclear(res);
}

void show_friends(PGconn *conn) {
    printf("\n=== FRIENDS TABLE ===\n");
    PGresult *res = execute_query_with_result(conn, 
        "SELECT f.id, u1.username, u2.username, f.status, f.created_at "
        "FROM friends f "
        "JOIN users u1 ON f.user_id = u1.id "
        "JOIN users u2 ON f.friend_id = u2.id "
        "ORDER BY f.id");
    
    if (!res || PQntuples(res) == 0) {
        printf("No friends relationships found.\n");
        if (res) PQclear(res);
        return;
    }
    
    printf("%-5s %-15s %-15s %-10s %-20s\n", "ID", "User", "Friend", "Status", "Created At");
    printf("------------------------------------------------------------------------\n");
    
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        printf("%-5s %-15s %-15s %-10s %-20s\n",
               PQgetvalue(res, i, 0),
               PQgetvalue(res, i, 1),
               PQgetvalue(res, i, 2),
               PQgetvalue(res, i, 3),
               PQgetvalue(res, i, 4));
    }
    
    PQclear(res);
}

void show_groups(PGconn *conn) {
    printf("\n=== GROUPS TABLE ===\n");
    PGresult *res = execute_query_with_result(conn, 
        "SELECT g.id, g.group_name, u.username, g.created_at "
        "FROM groups g "
        "JOIN users u ON g.creator_id = u.id "
        "ORDER BY g.id");
    
    if (!res || PQntuples(res) == 0) {
        printf("No groups found.\n");
        if (res) PQclear(res);
        return;
    }
    
    printf("%-5s %-20s %-15s %-20s\n", "ID", "Group Name", "Creator", "Created At");
    printf("----------------------------------------------------------------\n");
    
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        printf("%-5s %-20s %-15s %-20s\n",
               PQgetvalue(res, i, 0),
               PQgetvalue(res, i, 1),
               PQgetvalue(res, i, 2),
               PQgetvalue(res, i, 3));
    }
    
    PQclear(res);
}

void show_group_members(PGconn *conn) {
    printf("\n=== GROUP MEMBERS TABLE ===\n");
    PGresult *res = execute_query_with_result(conn, 
        "SELECT gm.id, g.group_name, u.username, gm.role, gm.joined_at "
        "FROM group_members gm "
        "JOIN groups g ON gm.group_id = g.id "
        "JOIN users u ON gm.user_id = u.id "
        "ORDER BY gm.id");
    
    if (!res || PQntuples(res) == 0) {
        printf("No group members found.\n");
        if (res) PQclear(res);
        return;
    }
    
    printf("%-5s %-15s %-15s %-10s %-20s\n", "ID", "Group", "Member", "Role", "Joined At");
    printf("------------------------------------------------------------------------\n");
    
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        printf("%-5s %-15s %-15s %-10s %-20s\n",
               PQgetvalue(res, i, 0),
               PQgetvalue(res, i, 1),
               PQgetvalue(res, i, 2),
               PQgetvalue(res, i, 3),
               PQgetvalue(res, i, 4));
    }
    
    PQclear(res);
}

void show_messages(PGconn *conn) {
    printf("\n=== MESSAGES TABLE ===\n");
    PGresult *res = execute_query_with_result(conn, 
        "SELECT m.id, u1.username as sender, "
        "COALESCE(u2.username, 'N/A') as receiver, "
        "COALESCE(g.group_name, 'N/A') as groupname, "
        "LEFT(m.content, 30) as content, "
        "m.is_delivered, m.created_at "
        "FROM messages m "
        "JOIN users u1 ON m.sender_id = u1.id "
        "LEFT JOIN users u2 ON m.receiver_id = u2.id "
        "LEFT JOIN groups g ON m.group_id = g.id "
        "ORDER BY m.id");
    
    if (!res || PQntuples(res) == 0) {
        printf("No messages found.\n");
        if (res) PQclear(res);
        return;
    }
    
    printf("%-5s %-12s %-12s %-12s %-32s %-10s %-20s\n", 
           "ID", "Sender", "Receiver", "Group", "Content", "Delivered", "Created At");
    printf("--------------------------------------------------------------------------------------------------------\n");
    
    int rows = PQntuples(res);
    for (int i = 0; i < rows; i++) {
        const char *is_delivered = strcmp(PQgetvalue(res, i, 5), "t") == 0 ? "Yes" : "No";
        printf("%-5s %-12s %-12s %-12s %-32s %-10s %-20s\n",
               PQgetvalue(res, i, 0),
               PQgetvalue(res, i, 1),
               PQgetvalue(res, i, 2),
               PQgetvalue(res, i, 3),
               PQgetvalue(res, i, 4),
               is_delivered,
               PQgetvalue(res, i, 6));
    }
    
    PQclear(res);
}
