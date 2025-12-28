#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// TASK 1: Stream Processing
// ============================================================================

/**
 * @function stream_buffer_create: Creates and initializes a new StreamBuffer.
 * 
 * @return Pointer to the newly created StreamBuffer, or NULL on failure.
 */
StreamBuffer* stream_buffer_create() {
    StreamBuffer *buffer = (StreamBuffer*)malloc(sizeof(StreamBuffer));
    if (!buffer) return NULL;
    
    buffer->length = 0;
    buffer->capacity = MAX_MESSAGE_LENGTH * 2;
    memset(buffer->data, 0, buffer->capacity);
    
    return buffer;
}

/**
 * @function stream_buffer_destroy: Frees the memory allocated for a StreamBuffer.
 * 
 * @param buffer Pointer to the StreamBuffer to be destroyed.
 * 
 * @return void
 */
void stream_buffer_destroy(StreamBuffer *buffer) {
    if (buffer) {
        free(buffer);
    }
}

/**
 * @function stream_buffer_append: Appends data to the StreamBuffer.
 * 
 * @param buffer Pointer to the StreamBuffer.
 * @param data Pointer to the data to append.
 * @param len Length of the data to append.
 * 
 * @return 1 on success, 0 on failure (e.g., buffer overflow).
 */
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

/**
 * @function stream_buffer_extract_message: Extracts a complete protocol message from the StreamBuffer.
 * 
 * @param buffer Pointer to the StreamBuffer.
 * 
 * @return Pointer to the extracted message (dynamically allocated), or NULL if no complete message is found.
 */
char* stream_buffer_extract_message(StreamBuffer *buffer) {
    if (!buffer || buffer->length == 0) return NULL;
    
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

/**
 * @function parse_command_type: Parses the command type from a string.
 * 
 * @param cmd_str Pointer to the command string.
 * 
 * @return Corresponding CommandType enum value.
 */
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
    if (strcmp(cmd_str, "GROUP_SEND_OFFLINE_MSG") == 0) return CMD_GROUP_SEND_OFFLINE_MSG;
    if (strcmp(cmd_str, "GROUP_APPROVE") == 0) return CMD_GROUP_APPROVE;
    if (strcmp(cmd_str, "GROUP_REJECT") == 0) return CMD_GROUP_REJECT;
    if (strcmp(cmd_str, "LIST_JOIN_REQUESTS") == 0) return CMD_LIST_JOIN_REQUESTS;
    if (strcmp(cmd_str, "SEND_OFFLINE_MSG") == 0) return CMD_SEND_OFFLINE_MSG;
    if (strcmp(cmd_str, "FRIEND_PENDING") == 0) return CMD_FRIEND_PENDING;
    if (strcmp(cmd_str, "GET_OFFLINE_MSG") == 0) return CMD_GET_OFFLINE_MSG;

    return CMD_UNKNOWN;
}

/**
 * @function parse_protocol_message: Parses a raw protocol message into a ParsedCommand structure.
 * 
 * @param raw_message Pointer to the raw protocol message string.
 * 
 * @return Pointer to the ParsedCommand structure (dynamically allocated), or NULL on failure.
 */
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
        
        case CMD_GET_OFFLINE_MSG:
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
        case CMD_LIST_JOIN_REQUESTS:
        case CMD_GROUP_SEND_OFFLINE_MSG:
        case CMD_GROUP_EXIT_MESSAGING:
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->group_name, token, MAX_USERNAME_LENGTH - 1);
                cmd->param_count++;
            }
            break;
            
        case CMD_GROUP_INVITE:
        case CMD_GROUP_KICK:
        case CMD_GROUP_APPROVE:
        case CMD_GROUP_REJECT:
            token = strtok(NULL, " ");
            if (token) {
                strncpy(cmd->group_name, token, MAX_USERNAME_LENGTH - 1);
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
                strncpy(cmd->group_name, token, MAX_USERNAME_LENGTH - 1);
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
        case CMD_FRIEND_PENDING:
            break;
            
        default:
            break;
    }
    
    free(msg_copy);
    return cmd;
}

/**
 * @function free_parsed_command: Frees the memory allocated for a ParsedCommand structure.
 * 
 * @param cmd Pointer to the ParsedCommand structure to be freed.
 * 
 * @return void
 */
void free_parsed_command(ParsedCommand *cmd) {
    if (cmd) {
        free(cmd);
    }
}

// ============================================================================
// Response Builders
// ============================================================================

/**
 * @function build_response: Builds a protocol response message.
 * 
 * @param status_code Integer status code.
 * @param message Pointer to the optional message string.
 * 
 * @return Pointer to the constructed response message (dynamically allocated), or NULL on failure.
 */
char* build_response(int status_code, const char *message) {
    char *response = (char*)malloc(MAX_MESSAGE_LENGTH);
    if (!response) return NULL;
    
    if (message && strlen(message) > 0) {
        snprintf(response, MAX_MESSAGE_LENGTH, "%d %s%s", 
                 status_code, message, PROTOCOL_DELIMITER);
    } else {
        snprintf(response, MAX_MESSAGE_LENGTH, "%d %s", 
                 status_code, PROTOCOL_DELIMITER);
    }
    
    return response;
}

/**
 * @function build_simple_response: Builds a simple protocol response message with only a status code.
 * 
 * @param status_code Integer status code.
 * 
 * @return Pointer to the constructed response message (dynamically allocated), or NULL on failure.
 */
char* build_simple_response(int status_code) {
    return build_response(status_code, "");
}

