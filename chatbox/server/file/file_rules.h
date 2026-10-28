#ifndef CHATBOX_SERVER_FILE_FILE_RULES_H
#define CHATBOX_SERVER_FILE_FILE_RULES_H

#include <cstdint>
#include <string>

namespace chatbox {

bool validFileSize(uint64_t size);
bool validFileName(const std::string& name);
std::string sanitizeFileName(const std::string& name);
bool isReservedWindowsName(const std::string& stem);

}  // namespace chatbox

#endif  // CHATBOX_SERVER_FILE_FILE_RULES_H
