#include <string>

#include "server/file/file_rules.h"
#include "server/file/file_types.h"
#include "tests/test_framework.h"

namespace {

int testFileSizes() {
    CHECK_TRUE(chatbox::validFileSize(0));
    CHECK_TRUE(chatbox::validFileSize(chatbox::kMaxFileSize));
    CHECK_TRUE(!chatbox::validFileSize(chatbox::kMaxFileSize + 1));
    return 0;
}

int testFileNames() {
    CHECK_TRUE(chatbox::validFileName("a.txt"));
    CHECK_TRUE(chatbox::validFileName("设计稿.png"));
    CHECK_TRUE(!chatbox::validFileName(""));
    CHECK_TRUE(!chatbox::validFileName("   "));
    CHECK_TRUE(!chatbox::validFileName("a\nb"));
    CHECK_EQ(chatbox::sanitizeFileName("a<b>c:d?.txt"),
             std::string("a_b_c_d_.txt"));
    CHECK_EQ(chatbox::sanitizeFileName("CON"), std::string("_CON"));
    CHECK_EQ(chatbox::sanitizeFileName("trailing. "), std::string("trailing"));
    CHECK_EQ(chatbox::sanitizeFileName(""), std::string("file"));
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testFileSizes();
    failures += testFileNames();
    if (failures == 0) {
        std::cout << "test_file_rules: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
