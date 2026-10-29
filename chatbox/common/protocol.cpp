#include "common/protocol.h"

#include <algorithm>
#include <limits>

namespace chatbox {

void appendU16(std::string& out, uint16_t value) {
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

void appendU32(std::string& out, uint32_t value) {
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

void ByteWriter::putU8(uint8_t value) {
    data_.push_back(static_cast<char>(value));
}

void ByteWriter::putU16(uint16_t value) {
    appendU16(data_, value);
}

void ByteWriter::putU32(uint32_t value) {
    appendU32(data_, value);
}

void ByteWriter::putU64(uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        data_.push_back(static_cast<char>((value >> shift) & 0xFF));
    }
}

void ByteWriter::putString(const std::string& value) {
    const std::size_t length = std::min<std::size_t>(value.size(), 0xFFFF);
    putU16(static_cast<uint16_t>(length));
    data_.append(value.data(), length);
}

void ByteWriter::putRaw(const std::string& value) {
    data_.append(value);
}

const std::string& ByteWriter::data() const {
    return data_;
}

ByteReader::ByteReader(const char* data, std::size_t size)
    : data_(data), size_(size), offset_(0) {}

bool ByteReader::getU8(uint8_t& value) {
    if (offset_ + 1 > size_) {
        return false;
    }
    value = static_cast<uint8_t>(data_[offset_]);
    ++offset_;
    return true;
}

bool ByteReader::getU16(uint16_t& value) {
    if (offset_ + 2 > size_) {
        return false;
    }
    value = static_cast<uint16_t>(
        (static_cast<uint16_t>(static_cast<uint8_t>(data_[offset_])) << 8) |
        static_cast<uint16_t>(static_cast<uint8_t>(data_[offset_ + 1])));
    offset_ += 2;
    return true;
}

bool ByteReader::getU32(uint32_t& value) {
    if (offset_ + 4 > size_) {
        return false;
    }
    value = (static_cast<uint32_t>(static_cast<uint8_t>(data_[offset_])) << 24) |
            (static_cast<uint32_t>(static_cast<uint8_t>(data_[offset_ + 1])) << 16) |
            (static_cast<uint32_t>(static_cast<uint8_t>(data_[offset_ + 2])) << 8) |
            static_cast<uint32_t>(static_cast<uint8_t>(data_[offset_ + 3]));
    offset_ += 4;
    return true;
}

bool ByteReader::getU64(uint64_t& value) {
    if (offset_ + 8 > size_) {
        return false;
    }
    uint64_t parsed = 0;
    for (int i = 0; i < 8; ++i) {
        parsed = (parsed << 8) | static_cast<uint8_t>(data_[offset_ + i]);
    }
    value = parsed;
    offset_ += 8;
    return true;
}

bool ByteReader::getString(std::string& value) {
    uint16_t length = 0;
    if (!getU16(length)) {
        return false;
    }
    if (offset_ + length > size_) {
        offset_ -= 2;
        return false;
    }
    value.assign(data_ + offset_, length);
    offset_ += length;
    return true;
}

std::size_t ByteReader::remaining() const {
    return size_ - offset_;
}

std::string encodeFrame(const Frame& frame) {
    std::string out;
    out.reserve(kFrameHeaderSize + frame.body.size());
    appendU16(out, frame.header.magic);
    out.push_back(static_cast<char>(frame.header.version));
    out.push_back(static_cast<char>(frame.header.flags));
    appendU16(out, frame.header.cmd);
    appendU32(out, frame.header.seq);
    appendU32(out, static_cast<uint32_t>(frame.body.size()));
    out.append(frame.body);
    return out;
}

bool decodeHeader(const char* data, std::size_t size, FrameHeader& header) {
    if (data == nullptr || size < kFrameHeaderSize) {
        return false;
    }
    ByteReader reader(data, size);
    FrameHeader parsed;
    if (!reader.getU16(parsed.magic) || !reader.getU8(parsed.version) ||
        !reader.getU8(parsed.flags) || !reader.getU16(parsed.cmd) ||
        !reader.getU32(parsed.seq) || !reader.getU32(parsed.body_len)) {
        return false;
    }
    header = parsed;
    return true;
}

bool validateHeader(const FrameHeader& header, uint32_t max_body, std::string& error) {
    if (header.magic != kProtocolMagic) {
        error = "invalid magic";
        return false;
    }
    if (header.version != kProtocolVersion) {
        error = "unsupported protocol version";
        return false;
    }
    if (header.body_len > max_body) {
        error = "frame body too large";
        return false;
    }
    error.clear();
    return true;
}

}  // namespace chatbox
