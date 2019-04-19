#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <unistd.h>

#include <dirent.h>

#include "../common/connection.h"
#include "../common/debug.h"
#include "../common/exception.h"
#include "../common/protocol.h"

#define KiB (1 << 10)
#define GiB (1 << 30)

#define BUFFER_SIZE     4 * KiB
#define MAX_FILE_SIZE   1 * GiB // CHANGE IT !!

#define CLIENT_ROOT "downloads/"

const string cursor = "> ";
const string greetings = "Bye :(";
const string error = "Something went wrong. Retry later.";

Connection *connection;
istringstream is;

enum CommandType {
    HELP, R_LIST, L_LIST, QUIT, RETR, STOR, DELE, BAD_REQ
};

void send_cmd(string);
void send_file(string, string, size_t);
void recv_response();
void recv_list();
void recv_file(string);

CommandType str2cmd(string);
void parse_cmd(string);
bool check_and_get_file_size(string, size_t&);

void cmd_help();
void cmd_local_list(string);
void cmd_quit();
void cmd_remote_list();
void cmd_allo(string);
void cmd_stor(string);
void cmd_retr(string);
void cmd_dele(string);
void cmd_unknown(string);
