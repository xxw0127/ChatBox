#include "server/file/file_rules.h"

#include <algorithm>
#include <cctype>

#include "common/text_utils.h"
#include "server/file/file_types.h"

namespace chatbox {

namespace {

constexpr std::size_t kMaxNameCodePoints = 180;

bool isInvalidWindowsChar(unsigned char ch) {
    switch (ch) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '/':
        case '\\':
        case '|':
        case '?':
        case '*':
            return true;
        default:
            return ch < 0x20 || ch == 0x7F;
    }
}

std::string trimTrailingDotsAndSpaces(std::string value) {
    while (!value.empty() &&
           (value.back() == '.' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

std::size_t utf8PrefixBytes(const std::string& value, std::size_t max_codepoints) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while (offset < value.size()) {
        const unsigned char byte = static_cast<unsigned char>(value[offset]);
        std::size_t length = 1;
        if ((byte & 0x80) == 0) {
            length = 1;
        } else if ((byte & 0xE0) == 0xC0) {
            length = 2;
        } else if ((byte & 0xF0) == 0xE0) {
            length = 3;
        } else if ((byte & 0xF8) == 0xF0) {
            length = 4;
        }
        if (offset + length > value.size()) {
            break;
        }
        ++count;
        if (count > max_codepoints) {
            break;
        }
        offset += length;
    }
    return offset;
}

}  // namespace

bool validFileSize(uint64_t size) {
    return size <= kMaxFileSize;
}

bool validFileName(const std::string& name) {
    if (name.empty() || trimAsciiWhitespace(name).empty()) {
        return false;
    }
    if (containsControlCharacters(name)) {
        return false;
    }
    return utf8CodePointCount(name) <= kMaxNameCodePoints;
}

bool isReservedWindowsName(const std::string& stem) {
    std::string upper;
    upper.reserve(stem.size());
    for (const unsigned char ch : stem) {
        upper.push_back(static_cast<char>(std::toupper(ch)));
    }
    if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") {
        return true;
    }
    if (upper.size() == 4 &&
        (upper.rfind("COM", 0) == 0 || upper.rfind("LPT", 0) == 0) &&
        upper[3] >= '1' && upper[3] <= '9') {
        return true;
    }
    return false;
}

std::string sanitizeFileName(const std::string& name) {
    std::string cleaned;
    cleaned.reserve(name.size());
    for (const unsigned char ch : name) {
        cleaned.push_back(isInvalidWindowsChar(ch) ? '_' : static_cast<char>(ch));
    }
    cleaned = trimAsciiWhitespace(cleaned);
    cleaned = trimTrailingDotsAndSpaces(cleaned);
    if (utf8CodePointCount(cleaned) > kMaxNameCodePoints) {
        cleaned = cleaned.substr(0, utf8PrefixBytes(cleaned, kMaxNameCodePoints));
        cleaned = trimTrailingDotsAndSpaces(cleaned);
    }
    if (cleaned.empty()) {
        cleaned = "file";
    }
    const std::size_t dot = cleaned.find('.');
    const std::string stem = dot == std::string::npos ? cleaned : cleaned.substr(0, dot);
    if (isReservedWindowsName(stem)) {
        cleaned.insert(cleaned.begin(), '_');
    }
    return cleaned;
}

}  // namespace chatbox
