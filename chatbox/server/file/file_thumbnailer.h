#ifndef CHATBOX_SERVER_FILE_FILE_THUMBNAILER_H
#define CHATBOX_SERVER_FILE_FILE_THUMBNAILER_H

#include <cstdint>
#include <string>

namespace chatbox {

struct ThumbnailResult {
    bool ok = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t duration_ms = 0;
};

bool isThumbnailMime(const std::string& mime);
bool parseProbeCsv(const std::string& csv, uint32_t& width, uint32_t& height,
                   uint32_t& duration_ms);
void fitThumbnail(uint32_t source_width, uint32_t source_height,
                  uint32_t max_width, uint32_t max_height,
                  uint32_t& out_width, uint32_t& out_height);

// Generates a JPEG preview for image/GIF files. Video previews are Phase 3.
class FileThumbnailer {
public:
    FileThumbnailer(std::string ffmpeg_path, std::string ffprobe_path);

    ThumbnailResult generate(const std::string& source_path,
                             const std::string& output_path,
                             const std::string& mime) const;

private:
    bool probe(const std::string& source_path, uint32_t& width,
               uint32_t& height, uint32_t& duration_ms) const;

    std::string ffmpeg_path_;
    std::string ffprobe_path_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_FILE_FILE_THUMBNAILER_H
