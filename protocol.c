#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// TASK 1: Stream Processing
// ============================================================================

StreamBuffer* stream_buffer_create() {
    StreamBuffer *buffer = (StreamBuffer*)malloc(sizeof(StreamBuffer));
    if (!buffer) return NULL;
    
    buffer->length = 0;
    buffer->capacity = MAX_MESSAGE_LENGTH * 2;
    memset(buffer->data, 0, buffer->capacity);
    
    return buffer;
}

void stream_buffer_destroy(StreamBuffer *buffer) {
    if (buffer) {
        free(buffer);
    }
}

int stream_buffer_append(StreamBuffer *buffer, const char *data, size_t len) {
    if (!buffer || !data) return 0;
    
    if (buffer->length + len >= buffer->capacity) {
        fprintf(stderr, "Buffer overflow: cannot append %zu bytes\n", len);
        return 0;
    }
    
    memcpy(buffer->data + buffer->length, data, len);
    buffer->length += len;
    buffer->data[buffer->length] = '\0';
    
    return 1;
}

char* stream_buffer_extract_message(StreamBuffer *buffer) {
    if (!buffer || buffer->length == 0) return NULL;
    
    // Find delimiter "\r\n"
    char *delim = strstr(buffer->data, PROTOCOL_DELIMITER);
    if (!delim) {
        return NULL;
    }
    
    size_t msg_len = delim - buffer->data;
    
    char *message = (char*)malloc(msg_len + 1);
    if (!message) return NULL;
    
    memcpy(message, buffer->data, msg_len);
    message[msg_len] = '\0';
    
    size_t remaining = buffer->length - msg_len - strlen(PROTOCOL_DELIMITER);
    memmove(buffer->data, delim + strlen(PROTOCOL_DELIMITER), remaining);
    buffer->length = remaining;
    buffer->data[buffer->length] = '\0';
    
    return message;
}

// ============================================================================
// Protocol Parsing Functions
// ============================================================================

CommandType parse_command_type(const char *cmd_str) {
    if (!cmd_str) return CMD_UNKNOWN;
    
    if (strcmp(cmd_str, "REGISTER") == 0) return CMD_REGISTER;
    if (strcmp(cmd_str, "LOGIN") == 0) return CMD_LOGIN;
    if (strcmp(cmd_str, "LOGOUT") == 0) return CMD_LOGOUT;
    if (strcmp(cmd_str, "FRIEND_REQ") == 0) return CMD_FRIEND_REQ;
    if (strcmp(cmd_str, "FRIEND_ACCEPT") == 0) return CMD_FRIEND_ACCEPT;
    if (strcmp(cmd_str, "FRIEND_DECLINE") == 0) return CMD_FRIEND_DECLINE;
    if (strcmp(cmd_str, "FRIEND_REMOVE") == 0) return CMD_FRIEND_REMOVE;
    if (strcmp(cmd_str, "FRIEND_LIST") == 0) return CMD_FRIEND_LIST;
    if (strcmp(cmd_str, "MSG") == 0) return CMD_MSG;
    if (strcmp(cmd_str, "GROUP_CREATE") == 0) return CMD_GROUP_CREATE;
    if (strcmp(cmd_str, "GROUP_INVITE") == 0) return CMD_GROUP_INVITE;
    if (strcmp(cmd_str, "GROUP_JOIN") == 0) return CMD_GROUP_JOIN;
    if (strcmp(cmd_str, "GROUP_LEAVE") == 0) return CMD_GROUP_LEAVE;
    if (strcmp(cmd_str, "GROUP_KICK") == 0) return CMD_GROUP_KICK;
    if (strcmp(cmd_str, "GROUP_MSG") == 0) return CMD_GROUP_MSG;
    if (strcmp(cmd_str, "SEND_OFFLINE_MSG") == 0) return CMD_SEND_OFFLINE_MSG;
    
    return CMD_UNKNOWN;
}

ParsedCommand* parse_protocol_message(const char *raw_message) {
    if (!raw_message) return NULL;
    
    ParsedCommand *cmd = (ParsedCommand*)malloc(sizeof(ParsedCommand));
    if (!cmd) return NULL;
    
    memset(cmd, 0, sizeof(ParsedCommand));
    cmd->cmd_type = CMD_UNKNOWN;
    cmd->param_count = 0;
    
    char *msg_copy = strdup(raw_message);
    if (!msg_copy) {
        free(cmd);
        return NULL;
    }
    
    char *token = strtok(msg_copy, " ");
    if (!token) {
        free(msg_copy);
        free(cmd);
        return NULL;
    }
    
    cmd->cmd_type = parse_command_type(token);
    
    switch (cmd->cmd_type) {
        case CMD_REGISTER:
        case CMD_LOGIN:
            // REGISTER <username> <password>
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->username, token, MAX_USERNAME_LENGTH - 1);
                cmd->param_count++;
            }
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->password, token, MAX_PASSWORD_LENGTH - 1);
                cmd->param_count++;
            }
            break;
            
        case CMD_FRIEND_REQ:
        case CMD_FRIEND_ACCEPT:
        case CMD_FRIEND_DECLINE:
        case CMD_FRIEND_REMOVE:
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->target_user, token, MAX_USERNAME_LENGTH - 1);
                cmd->param_count++;
            }
            break;
            
        case CMD_MSG:
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->target_user, token, MAX_USERNAME_LENGTH - 1);
                cmd->param_count++;
            }
            token = strtok(NULL, "");
            if (token) {
                strncpy(cmd->message, token, MAX_MESSAGE_LENGTH - 1);
                cmd->param_count++;
            }
            break;
            
        case CMD_GROUP_CREATE:
        case CMD_GROUP_JOIN:
        case CMD_GROUP_LEAVE:
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->group_id, token, MAX_USERNAME_LENGTH - 1);
                cmd->param_count++;
            }
            break;
            
        case CMD_GROUP_INVITE:
        case CMD_GROUP_KICK:
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->group_id, token, MAX_USERNAME_LENGTH - 1);
                cmd->param_count++;
            }
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->target_user, token, MAX_USERNAME_LENGTH - 1);
                cmd->param_count++;
            }
            break;
            
        case CMD_GROUP_MSG:
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->group_id, token, MAX_USERNAME_LENGTH - 1);
                cmd->param_count++;
            }
            token = strtok(NULL, "");
            if (token) {
                strncpy(cmd->message, token, MAX_MESSAGE_LENGTH - 1);
                cmd->param_count++;
            }
            break;
            
        case CMD_LOGOUT:
        case CMD_FRIEND_LIST:
            break;
            
        default:
            break;
    }
    
    free(msg_copy);
    return cmd;
}

void free_parsed_command(ParsedCommand *cmd) {
    if (cmd) {
        free(cmd);
    }
}

// ============================================================================
// Response Builders
// ============================================================================

char* build_response(int status_code, const char *message) {
    char *response = (char*)malloc(MAX_MESSAGE_LENGTH);
    if (!response) return NULL;
    
    if (message && strlen(message) > 0) {
        snprintf(response, MAX_MESSAGE_LENGTH, "%d %s%s", 
                 status_code, message, PROTOCOL_DELIMITER);
    } else {
        snprintf(response, MAX_MESSAGE_LENGTH, "%d OK%s", 
                 status_code, PROTOCOL_DELIMITER);
    }
    
    return response;
}

char* build_simple_response(int status_code) {
    return build_response(status_code, "");
}

// ============================================================================
// Validation Functions
// ============================================================================

int validate_username(const char *username) {
    if (!username) return 0;
    
    size_t len = strlen(username);
    if (len < 3 || len > MAX_USERNAME_LENGTH) return 0;
    
    // Check for valid characters (alphanumeric and underscore)
    for (size_t i = 0; i < len; i++) {
        if (!isalnum(username[i]) && username[i] != '_') {
            return 0;
        }
    }
    
    return 1;
}

int validate_password(const char *password) {
    if (!password) return 0;
    
    size_t len = strlen(password);
    if (len < 6 || len > MAX_PASSWORD_LENGTH) return 0;
    
    return 1;
}
