#include "transport/transport.h"

namespace chatbox {
namespace clientcore {

Transport::Transport(QObject* parent) : QObject(parent) {}
Transport::~Transport() = default;

}  // namespace clientcore
}  // namespace chatbox
