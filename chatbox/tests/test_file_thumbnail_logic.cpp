#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "server/file/file_thumbnailer.h"
#include "tests/test_framework.h"

namespace {

int testMimeClassification() {
    CHECK_TRUE(chatbox::isThumbnailMime("image/png"));
    CHECK_TRUE(chatbox::isThumbnailMime("image/gif"));
    CHECK_TRUE(!chatbox::isThumbnailMime("video/mp4"));
    CHECK_TRUE(!chatbox::isThumbnailMime("application/pdf"));
    return 0;
}

int testProbeParsing() {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t duration = 0;
    CHECK_TRUE(chatbox::parseProbeCsv("640,480,1.500000\n", width, height,
                                      duration));
    CHECK_EQ(width, 640u);
    CHECK_EQ(height, 480u);
    CHECK_EQ(duration, 1500u);
    CHECK_TRUE(chatbox::parseProbeCsv("320,200\n", width, height, duration));
    CHECK_EQ(duration, 0u);
    CHECK_TRUE(!chatbox::parseProbeCsv("N/A\n", width, height, duration));
    CHECK_TRUE(!chatbox::parseProbeCsv("", width, height, duration));
    return 0;
}

int testFitThumbnail() {
    uint32_t width = 0;
    uint32_t height = 0;
    chatbox::fitThumbnail(640, 480, 320, 180, width, height);
    CHECK_EQ(width, 240u);
    CHECK_EQ(height, 180u);
    chatbox::fitThumbnail(100, 50, 320, 180, width, height);
    CHECK_EQ(width, 100u);
    CHECK_EQ(height, 50u);
    chatbox::fitThumbnail(1000, 100, 320, 180, width, height);
    CHECK_EQ(width, 320u);
    CHECK_EQ(height, 32u);
    return 0;
}

int testMissingFfmpeg() {
    chatbox::FileThumbnailer thumbnailer("/nonexistent/ffmpeg",
                                         "/nonexistent/ffprobe");
    const chatbox::ThumbnailResult result =
        thumbnailer.generate("/tmp/missing.png", "/tmp/missing.jpg",
                             "image/png");
    CHECK_TRUE(!result.ok);
    return 0;
}

int testImageFixture() {
    if (::access("/usr/bin/ffmpeg", X_OK) != 0 ||
        ::access("/usr/bin/ffprobe", X_OK) != 0) {
        std::cout << "ffmpeg not installed; skipping image fixture\n";
        return 0;
    }
    const std::string source = "/tmp/chatbox-thumb-test.png";
    const std::string output = "/tmp/chatbox-thumb-test.jpg";
    std::remove(source.c_str());
    std::remove(output.c_str());
    const std::string create =
        "/usr/bin/ffmpeg -v error -y -f lavfi -i color=c=red:s=64x48 "
        "-frames:v 1 " + source;
    if (std::system(create.c_str()) != 0) {
        std::cout << "cannot create image fixture\n";
        return 0;
    }
    chatbox::FileThumbnailer thumbnailer("/usr/bin/ffmpeg",
                                         "/usr/bin/ffprobe");
    const chatbox::ThumbnailResult result =
        thumbnailer.generate(source, output, "image/png");
    CHECK_TRUE(result.ok);
    CHECK_EQ(result.width, 64u);
    CHECK_EQ(result.height, 48u);
    std::ifstream jpeg(output, std::ios::binary);
    unsigned char header[2] = {0, 0};
    jpeg.read(reinterpret_cast<char*>(header), 2);
    CHECK_EQ(static_cast<int>(header[0]), 0xFF);
    CHECK_EQ(static_cast<int>(header[1]), 0xD8);
    std::remove(source.c_str());
    std::remove(output.c_str());
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testMimeClassification();
    failures += testProbeParsing();
    failures += testFitThumbnail();
    failures += testMissingFfmpeg();
    failures += testImageFixture();
    if (failures == 0) {
        std::cout << "test_file_thumbnail_logic: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
