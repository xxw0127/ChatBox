#include "server/profile/profile_service.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "common/text_utils.h"
#include "server/util/logging.h"

namespace chatbox {

namespace {

constexpr std::size_t kMaxAvatarBytes = 48 * 1024;

bool hasSupportedAvatarMagic(const std::string& avatar) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(avatar.data());
    if (avatar.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        return true;
    }
    if (avatar.size() >= 4 && bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E &&
        bytes[3] == 0x47) {
        return true;
    }
    return false;
}

Frame makeError(uint32_t seq, uint16_t code, const std::string& message) {
    Frame response;
    response.header.flags = kFlagResponse | kFlagError;
    response.header.cmd = kCmdError;
    response.header.seq = seq;
    ByteWriter writer;
    writer.putU16(code);
    writer.putString(message);
    response.body = writer.data();
    return response;
}

Frame makeSuccess(uint16_t command, uint32_t seq) {
    Frame response;
    response.header.flags = kFlagResponse;
    response.header.cmd = command;
    response.header.seq = seq;
    return response;
}

}  // namespace

ProfileService::ProfileService(MysqlConnection& database, SessionManager& sessions)
    : database_(database), sessions_(sessions) {}

ProfileResult ProfileService::handle(const Frame& request, const RoomMember& caller) {
    if (caller.user_id == 0) {
        return ProfileResult{makeError(request.header.seq, 1002, "not authenticated")};
    }
    switch (request.header.cmd) {
        case kCmdProfileGet:
            return handleProfileGet(request, caller);
        case kCmdAvatarGet:
            return handleAvatarGet(request, caller);
        case kCmdProfileUpdate:
            return handleProfileUpdate(request, caller);
        default:
            return ProfileResult{makeError(request.header.seq, 1001, "unknown command")};
    }
}

ProfileResult ProfileService::handleProfileGet(const Frame& request,
                                               const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint64_t target_id = 0;
    if (!reader.getU64(target_id) || target_id == 0) {
        return ProfileResult{makeError(request.header.seq, 1001, "invalid profile target")};
    }

    UserProfile profile;
    bool found = false;
    std::string error;
    if (!database_.getUserProfile(target_id, profile, found, error)) {
        logMessage(LogLevel::kError, "profile", "get profile failed: " + error);
        return ProfileResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!found) {
        return ProfileResult{makeError(request.header.seq, 1008, "user not found")};
    }

    uint8_t relation = 2;
    std::string remark;
    if (!database_.getProfileRelation(caller.user_id, target_id, relation, remark, error)) {
        logMessage(LogLevel::kError, "profile", "get profile relation failed: " + error);
        return ProfileResult{makeError(request.header.seq, 2001, "internal error")};
    }

    ProfileResult result;
    result.response = makeSuccess(kCmdProfileGet, request.header.seq);
    ByteWriter writer;
    writer.putU64(profile.id);
    writer.putString(profile.username);
    writer.putString(profile.nickname);
    writer.putString(profile.signature);
    writer.putU32(profile.avatar_version);
    writer.putString(profile.avatar);
    writer.putU8(sessions_.isOnline(target_id) ? 1 : 0);
    writer.putU8(relation);
    writer.putString(remark);
    result.response.body = writer.data();
    return result;
}

ProfileResult ProfileService::handleAvatarGet(const Frame& request,
                                              const RoomMember&) {
    ByteReader reader(request.body.data(), request.body.size());
    uint64_t target_id = 0;
    if (!reader.getU64(target_id) || target_id == 0) {
        return ProfileResult{makeError(request.header.seq, 1001, "invalid avatar target")};
    }

    uint32_t version = 0;
    std::string avatar;
    bool found = false;
    std::string error;
    if (!database_.getAvatar(target_id, version, avatar, found, error)) {
        logMessage(LogLevel::kError, "profile", "get avatar failed: " + error);
        return ProfileResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!found) {
        return ProfileResult{makeError(request.header.seq, 1008, "user not found")};
    }

    ProfileResult result;
    result.response = makeSuccess(kCmdAvatarGet, request.header.seq);
    ByteWriter writer;
    writer.putU64(target_id);
    writer.putU32(version);
    writer.putString(avatar);
    result.response.body = writer.data();
    return result;
}

ProfileResult ProfileService::handleProfileUpdate(const Frame& request,
                                                  const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    std::string nickname;
    std::string signature;
    uint8_t avatar_action = 0;
    if (!reader.getString(nickname) || !reader.getString(signature) ||
        !reader.getU8(avatar_action)) {
        return ProfileResult{makeError(request.header.seq, 1001,
                                       "invalid profile update body")};
    }

    nickname = trimAsciiWhitespace(nickname);
    signature = trimAsciiWhitespace(signature);
    if (nickname.empty() || utf8CodePointCount(nickname) > 32 ||
        containsControlCharacters(nickname)) {
        return ProfileResult{makeError(request.header.seq, 1001, "invalid nickname")};
    }
    if (utf8CodePointCount(signature) > 128 || containsControlCharacters(signature)) {
        return ProfileResult{makeError(request.header.seq, 1001, "invalid signature")};
    }
    if (avatar_action > 2) {
        return ProfileResult{makeError(request.header.seq, 1001, "invalid avatar action")};
    }

    std::string avatar;
    if (avatar_action == 1) {
        if (!reader.getString(avatar)) {
            return ProfileResult{makeError(request.header.seq, 1001,
                                           "missing avatar bytes")};
        }
        if (avatar.empty() || avatar.size() > kMaxAvatarBytes ||
            !hasSupportedAvatarMagic(avatar)) {
            return ProfileResult{makeError(request.header.seq, 1017,
                                           "avatar format not supported or too large")};
        }
    }
    if (reader.remaining() != 0) {
        return ProfileResult{makeError(request.header.seq, 1001,
                                       "invalid profile update body")};
    }

    UserProfile current;
    bool found = false;
    std::string error;
    if (!database_.getUserProfile(caller.user_id, current, found, error)) {
        logMessage(LogLevel::kError, "profile", "get current profile failed: " + error);
        return ProfileResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!found) {
        return ProfileResult{makeError(request.header.seq, 1008, "user not found")};
    }
    const bool nickname_changed = current.nickname != nickname;

    uint32_t version = 0;
    if (!database_.updateUserProfile(caller.user_id, nickname, signature, avatar_action,
                                     avatar, version, error)) {
        logMessage(LogLevel::kError, "profile", "update profile failed: " + error);
        return ProfileResult{makeError(request.header.seq, 2001, "internal error")};
    }

    ProfileResult result;
    result.response = makeSuccess(kCmdProfileUpdate, request.header.seq);
    ByteWriter writer;
    writer.putString(nickname);
    writer.putString(signature);
    writer.putU32(version);
    result.response.body = writer.data();
    result.nickname_changed = nickname_changed;
    result.new_nickname = nickname;

    if (nickname_changed || avatar_action != 0) {
        std::vector<uint64_t> friend_ids;
        if (!database_.listFriendIds(caller.user_id, friend_ids, error)) {
            logMessage(LogLevel::kWarn, "profile",
                       "profile push skipped, list friends failed: " + error);
            return result;
        }

        Frame push;
        push.header.cmd = kCmdProfileUpdatePush;
        push.header.seq = 0;
        ByteWriter push_writer;
        push_writer.putU64(caller.user_id);
        push_writer.putString(nickname);
        push_writer.putU32(version);
        push.body = push_writer.data();
        result.push_frame = encodeFrame(push);

        for (const uint64_t friend_id : friend_ids) {
            if (!sessions_.isOnline(friend_id)) {
                continue;
            }
            result.push_user_ids.push_back(friend_id);
        }
        if (result.push_user_ids.empty()) {
            result.push_frame.clear();
        }
    }
    return result;
}

}  // namespace chatbox
