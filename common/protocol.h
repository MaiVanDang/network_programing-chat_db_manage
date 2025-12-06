#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>

// Protocol constants
#define MAX_MESSAGE_LENGTH 4096
#define MAX_USERNAME_LENGTH 50
#define MAX_PASSWORD_LENGTH 100
#define PROTOCOL_DELIMITER "\r\n"

// Status codes - Success (1xx)
#define STATUS_REGISTER_OK 101
#define STATUS_LOGIN_OK 102
#define STATUS_LOGOUT_OK 103
#define STATUS_FRIEND_REQ_OK 104
#define STATUS_FRIEND_ACCEPT_OK 105
#define STATUS_FRIEND_DECLINE_OK 106
#define STATUS_FRIEND_REMOVE_OK 107
#define STATUS_FRIEND_LIST_OK 108
#define STATUS_MSG_OK 109
#define STATUS_GROUP_CREATE_OK 110
#define STATUS_GROUP_INVITE_OK 111
#define STATUS_GROUP_JOIN_OK 112
#define STATUS_GROUP_LEAVE_OK 113
#define STATUS_GROUP_KICK_OK 114
#define STATUS_GROUP_MSG_OK 115
#define STATUS_OFFLINE_MSG_OK 116
#define STATUS_FRIEND_PENDING_OK 117

// Status codes - Client errors (2xx)
#define STATUS_USERNAME_EXISTS 201
#define STATUS_WRONG_PASSWORD 202

// Status codes - Auth/Session errors (3xx)
#define STATUS_INVALID_USERNAME 301
#define STATUS_INVALID_PASSWORD 302
#define STATUS_USER_NOT_FOUND 303
#define STATUS_ALREADY_LOGGED_IN 304
#define STATUS_NOT_LOGGED_IN 305
#define STATUS_ALREADY_FRIEND 306

// Status codes - Database/Server errors (4xx)
#define STATUS_DATABASE_ERROR 400
#define STATUS_REQUEST_PENDING 401
#define STATUS_NO_PENDING_REQUEST 402
#define STATUS_NOT_FRIEND 403
#define STATUS_USER_OFFLINE 413
#define STATUS_MESSAGE_TOO_LONG 414
#define STATUS_GROUP_EXISTS 415
#define STATUS_INVALID_GROUP_ID 416
#define STATUS_NOT_GROUP_OWNER 417
#define STATUS_ALREADY_IN_GROUP 418
#define STATUS_GROUP_NOT_FOUND 419
#define STATUS_INVITE_REQUIRED 420
#define STATUS_NOT_IN_GROUP 421
#define STATUS_CANNOT_KICK_OWNER 422

// Status codes - System errors (5xx)
#define STATUS_UNDEFINED_ERROR 500

// Command types
typedef enum {
    CMD_REGISTER,
    CMD_LOGIN,
    CMD_LOGOUT,
    CMD_FRIEND_REQ,
    CMD_FRIEND_ACCEPT,
    CMD_FRIEND_DECLINE,
    CMD_FRIEND_REMOVE,
    CMD_FRIEND_LIST,
    CMD_MSG,
    CMD_GROUP_CREATE,
    CMD_GROUP_INVITE,
    CMD_GROUP_JOIN,
    CMD_GROUP_LEAVE,
    CMD_GROUP_KICK,
    CMD_GROUP_MSG,
    CMD_SEND_OFFLINE_MSG,
    CMD_FRIEND_PENDING, // thêm mới để lấy danh sách lời mời kết bạn
    CMD_UNKNOWN
} CommandType;

// Message structure for parsing
typedef struct {
    CommandType cmd_type;
    char username[MAX_USERNAME_LENGTH];
    char password[MAX_PASSWORD_LENGTH];
    char target_user[MAX_USERNAME_LENGTH];
    char group_id[MAX_USERNAME_LENGTH];
    char group_name[MAX_USERNAME_LENGTH];
    char message[MAX_MESSAGE_LENGTH];
    int param_count;
} ParsedCommand;

// Buffer structure for stream processing
typedef struct {
    char data[MAX_MESSAGE_LENGTH * 2];
    size_t length;
    size_t capacity;
} StreamBuffer;

// Function prototypes

// Stream processing functions
StreamBuffer* stream_buffer_create();
void stream_buffer_destroy(StreamBuffer *buffer);
int stream_buffer_append(StreamBuffer *buffer, const char *data, size_t len);
char* stream_buffer_extract_message(StreamBuffer *buffer);

// Protocol parsing functions
CommandType parse_command_type(const char *cmd_str);
ParsedCommand* parse_protocol_message(const char *raw_message);
void free_parsed_command(ParsedCommand *cmd);

// Protocol response builders
char* build_response(int status_code, const char *message);
char* build_simple_response(int status_code);

#endif
