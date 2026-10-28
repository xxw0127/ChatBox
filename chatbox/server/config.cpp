#include "server/config.h"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>

namespace chatbox {

namespace {

bool parseInt(const std::string& text, int min_value, int max_value, int& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0' || parsed < min_value ||
        parsed > max_value) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

}  // namespace

bool parseArgs(int argc, char** argv, Config& config, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            config.show_help = true;
            return true;
        }
        if (arg == "--proxy-protocol") {
            config.proxy_protocol = true;
            continue;
        }
        if (arg == "--port" || arg == "--io-threads" || arg == "--worker-threads" ||
            arg == "--max-connections" || arg == "--max-conn-per-ip" ||
            arg == "--idle-timeout" || arg == "--log-file" || arg == "--log-level" ||
            arg == "--listen" || arg == "--db-host" || arg == "--db-port" ||
            arg == "--db-user" || arg == "--db-name" ||
            arg == "--files-dir" || arg == "--ffmpeg-path" ||
            arg == "--ffprobe-path") {
            if (i + 1 >= argc) {
                error = "missing value for " + arg;
                return false;
            }
            const std::string value = argv[++i];
            if (arg == "--port") {
                if (!parseInt(value, 1, 65535, config.port)) {
                    error = "invalid --port: " + value;
                    return false;
                }
            } else if (arg == "--io-threads") {
                if (!parseInt(value, 1, 64, config.io_threads)) {
                    error = "invalid --io-threads: " + value;
                    return false;
                }
            } else if (arg == "--worker-threads") {
                if (!parseInt(value, 1, 64, config.worker_threads)) {
                    error = "invalid --worker-threads: " + value;
                    return false;
                }
            } else if (arg == "--max-connections") {
                if (!parseInt(value, 1, 1000000, config.max_connections)) {
                    error = "invalid --max-connections: " + value;
                    return false;
                }
            } else if (arg == "--max-conn-per-ip") {
                if (!parseInt(value, 0, 100000, config.max_conn_per_ip)) {
                    error = "invalid --max-conn-per-ip: " + value;
                    return false;
                }
            } else if (arg == "--idle-timeout") {
                if (!parseInt(value, 1, 86400, config.idle_timeout_sec)) {
                    error = "invalid --idle-timeout: " + value;
                    return false;
                }
            } else if (arg == "--log-file") {
                config.log_file = value;
            } else if (arg == "--log-level") {
                if (!parseLogLevel(value, config.log_level)) {
                    error = "invalid --log-level: " + value;
                    return false;
                }
            } else if (arg == "--listen") {
                config.listen_address = value;
            } else if (arg == "--db-host") {
                config.db_host = value;
            } else if (arg == "--db-port") {
                if (!parseInt(value, 1, 65535, config.db_port)) {
                    error = "invalid --db-port: " + value;
                    return false;
                }
            } else if (arg == "--db-user") {
                config.db_user = value;
            } else if (arg == "--db-name") {
                config.db_name = value;
            } else if (arg == "--files-dir") {
                config.files_dir = value;
            } else if (arg == "--ffmpeg-path") {
                config.ffmpeg_path = value;
            } else if (arg == "--ffprobe-path") {
                config.ffprobe_path = value;
            }
            continue;
        }
        error = "unknown argument: " + arg;
        return false;
    }
    return true;
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "  --listen <ip>          bind address (default 0.0.0.0)\n"
              << "  --port <1-65535>       listen port (default 9100)\n"
              << "  --proxy-protocol       accept PROXY v1/v2 header (behind nginx)\n"
              << "  --io-threads <1-64>    IO thread count (default 4)\n"
              << "  --worker-threads <1-64> business worker count (default 4)\n"
              << "  --max-connections <n>  global connection limit (default 20000)\n"
              << "  --max-conn-per-ip <n>  per-IP connection limit, 0=off (default 20)\n"
              << "  --idle-timeout <sec>   heartbeat timeout (default 90)\n"
              << "  --log-file <path>      log file path (default logs/chatbox.log)\n"
              << "  --log-level <level>    debug|info|warn|error (default info)\n"
              << "  --db-host <host>       MySQL host (default 127.0.0.1)\n"
              << "  --db-port <port>       MySQL port (default 3306)\n"
              << "  --db-user <user>       MySQL user (default chatbox)\n"
              << "  --db-name <database>   MySQL database (default chatbox)\n"
              << "  --files-dir <path>     file storage root (default /home/ubuntu/chatbox/files)\n"
              << "  --ffmpeg-path <path>   ffmpeg binary (default /usr/bin/ffmpeg)\n"
              << "  --ffprobe-path <path>  ffprobe binary (default /usr/bin/ffprobe)\n"
              << "  CHATBOX_DB_PASSWORD   MySQL password (environment variable)\n"
              << "  --help, -h             show this help\n";
}

}  // namespace chatbox
