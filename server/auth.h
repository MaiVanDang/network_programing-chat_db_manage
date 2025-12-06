#ifndef AUTH_H
#define AUTH_H

void handle_register_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_login_command(Server *server, ClientSession *client, ParsedCommand *cmd);
void handle_logout_command(Server *server, ClientSession *client, ParsedCommand *cmd);

// Validation functions
int validate_username(const char *username);
int validate_password(const char *password);

#endif
