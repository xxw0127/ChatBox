#include "server/group/group_service.h"

namespace chatbox {

GroupService::GroupService(MysqlConnection& database, RoomManager& rooms)
    : database_(database), rooms_(rooms) {}

GroupResult GroupService::handle(const Frame& request, const RoomMember& caller) {
    (void)caller;
    return notImplemented(request);
}

GroupResult GroupService::notImplemented(const Frame& request) {
    Frame response;
    response.header.flags = kFlagResponse | kFlagError;
    response.header.cmd = request.header.cmd;
    response.header.seq = request.header.seq;
    ByteWriter writer;
    writer.putU16(1001);
    writer.putString("group service not implemented");
    response.body = writer.data();
    GroupResult result;
    result.response = std::move(response);
    return result;
}

}  // namespace chatbox
