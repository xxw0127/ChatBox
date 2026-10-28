#include "server/file/file_thumbnailer.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace chatbox {

namespace {

constexpr uint32_t kThumbMaxWidth = 320;
constexpr uint32_t kThumbMaxHeight = 180;
constexpr int kProbeTimeoutMs = 5000;
constexpr int kGenerateTimeoutMs = 15000;

bool executable(const std::string& path) {
    return !path.empty() && ::access(path.c_str(), X_OK) == 0;
}

// Runs argv[0] with argv without a shell. Never concatenates the path into a
// command string, so file names cannot inject shell syntax.
int runProcess(const std::vector<std::string>& args, bool capture_output,
               int timeout_ms, std::string& output) {
    output.clear();
    if (args.empty() || !executable(args[0])) {
        return -1;
    }
    int pipe_fds[2] = {-1, -1};
    if (capture_output && ::pipe(pipe_fds) != 0) {
        return -1;
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const std::string& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        if (pipe_fds[0] >= 0) {
            ::close(pipe_fds[0]);
            ::close(pipe_fds[1]);
        }
        return -1;
    }
    if (pid == 0) {
        if (capture_output) {
            ::dup2(pipe_fds[1], STDOUT_FILENO);
            ::close(pipe_fds[0]);
            ::close(pipe_fds[1]);
        }
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        ::execv(argv[0], argv.data());
        ::_exit(127);
    }
    if (capture_output) {
        ::close(pipe_fds[1]);
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    int status = 0;
    bool exited = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            exited = true;
            break;
        }
        if (result < 0 && errno != EINTR) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!exited) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
        if (capture_output) {
            ::close(pipe_fds[0]);
        }
        return -1;
    }
    if (capture_output) {
        char buffer[4096];
        ssize_t read = 0;
        while ((read = ::read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
            if (output.size() < 8192) {
                output.append(buffer, static_cast<std::size_t>(read));
            }
        }
        ::close(pipe_fds[0]);
    }
    if (!WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
}

std::vector<std::string> splitComma(const std::string& line) {
    std::vector<std::string> parts;
    std::string current;
    for (const char ch : line) {
        if (ch == ',') {
            parts.push_back(current);
            current.clear();
        } else if (ch != '\r' && ch != '\n') {
            current.push_back(ch);
        }
    }
    parts.push_back(current);
    return parts;
}

}  // namespace

bool isThumbnailMime(const std::string& mime) {
    return mime == "image/png" || mime == "image/jpeg" ||
           mime == "image/jpg" || mime == "image/gif" ||
           mime == "image/webp" || mime == "image/bmp";
}

bool parseProbeCsv(const std::string& csv, uint32_t& width, uint32_t& height,
                   uint32_t& duration_ms) {
    width = 0;
    height = 0;
    duration_ms = 0;
    std::string line = csv;
    const std::size_t newline = line.find('\n');
    if (newline != std::string::npos) {
        line = line.substr(0, newline);
    }
    if (line.empty()) {
        return false;
    }
    const std::vector<std::string> parts = splitComma(line);
    if (parts.size() < 2) {
        return false;
    }
    try {
        width = static_cast<uint32_t>(std::stoul(parts[0]));
        height = static_cast<uint32_t>(std::stoul(parts[1]));
        if (parts.size() > 2 && !parts[2].empty() && parts[2] != "N/A") {
            const double seconds = std::stod(parts[2]);
            if (seconds > 0) {
                duration_ms = static_cast<uint32_t>(seconds * 1000.0);
            }
        }
    } catch (...) {
        width = 0;
        height = 0;
        duration_ms = 0;
        return false;
    }
    return width > 0 && height > 0;
}

void fitThumbnail(uint32_t source_width, uint32_t source_height,
                  uint32_t max_width, uint32_t max_height,
                  uint32_t& out_width, uint32_t& out_height) {
    if (source_width == 0 || source_height == 0) {
        out_width = max_width;
        out_height = max_height;
        return;
    }
    const double scale = std::min(
        static_cast<double>(max_width) / source_width,
        static_cast<double>(max_height) / source_height);
    out_width = static_cast<uint32_t>(
        std::max(1.0, std::min(static_cast<double>(max_width),
                               source_width * std::min(1.0, scale))));
    out_height = static_cast<uint32_t>(
        std::max(1.0, std::min(static_cast<double>(max_height),
                               source_height * std::min(1.0, scale))));
}

FileThumbnailer::FileThumbnailer(std::string ffmpeg_path,
                                 std::string ffprobe_path)
    : ffmpeg_path_(std::move(ffmpeg_path)),
      ffprobe_path_(std::move(ffprobe_path)) {}

bool FileThumbnailer::probe(const std::string& source_path, uint32_t& width,
                            uint32_t& height,
                            uint32_t& duration_ms) const {
    const std::vector<std::string> args = {
        ffprobe_path_, "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height,duration", "-of", "csv=p=0",
        source_path};
    std::string output;
    const int result =
        runProcess(args, true, kProbeTimeoutMs, output);
    if (result != 0) {
        return false;
    }
    return parseProbeCsv(output, width, height, duration_ms);
}

ThumbnailResult FileThumbnailer::generate(const std::string& source_path,
                                          const std::string& output_path,
                                          const std::string& mime) const {
    ThumbnailResult result;
    if (!isThumbnailMime(mime)) {
        return result;
    }
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t duration_ms = 0;
    probe(source_path, width, height, duration_ms);
    uint32_t target_width = kThumbMaxWidth;
    uint32_t target_height = kThumbMaxHeight;
    if (width > 0 && height > 0) {
        fitThumbnail(width, height, kThumbMaxWidth, kThumbMaxHeight,
                     target_width, target_height);
    }
    // MJPEG uses 4:2:0 by default and rejects odd dimensions.
    target_width = std::max(2u, target_width & ~1u);
    target_height = std::max(2u, target_height & ~1u);
    const std::string scale =
        "scale=" + std::to_string(target_width) + ":" +
        std::to_string(target_height);
    const std::vector<std::string> args = {
        ffmpeg_path_, "-v", "error", "-y", "-i", source_path,
        "-frames:v", "1", "-vf", scale, "-pix_fmt", "yuvj420p",
        "-q:v", "4", output_path};
    std::string ignored;
    const int status = runProcess(args, false, kGenerateTimeoutMs, ignored);
    if (status != 0) {
        return result;
    }
    struct stat info {};
    if (::stat(output_path.c_str(), &info) != 0 || info.st_size <= 0) {
        return result;
    }
    result.ok = true;
    result.width = width;
    result.height = height;
    result.duration_ms = duration_ms;
    return result;
}

}  // namespace chatbox
