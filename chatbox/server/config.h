#ifndef CHATBOX_SERVER_CONFIG_H
#define CHATBOX_SERVER_CONFIG_H

#include <string>

#include "server/util/logging.h"

namespace chatbox {

struct Config {
    std::string listen_address = "0.0.0.0";
    bool proxy_protocol = false;
    int port = 9100;
    int io_threads = 4;
    int worker_threads = 4;
    int max_connections = 20000;
    int max_conn_per_ip = 20;
    int idle_timeout_sec = 90;
    std::string log_file = "logs/chatbox.log";
    LogLevel log_level = LogLevel::kInfo;
    std::string db_host = "127.0.0.1";
    int db_port = 3306;
    std::string db_user = "chatbox";
    std::string db_name = "chatbox";
    std::string db_password;
    std::string files_dir = "/home/ubuntu/chatbox/files";
    std::string ffmpeg_path = "/usr/bin/ffmpeg";
    std::string ffprobe_path = "/usr/bin/ffprobe";
    bool show_help = false;
};

bool parseArgs(int argc, char** argv, Config& config, std::string& error);
void printUsage(const char* program);

}  // namespace chatbox

#endif  // CHATBOX_SERVER_CONFIG_H
