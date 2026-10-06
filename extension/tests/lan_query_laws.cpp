#include "support/netw_test.h"

#include "netw/connect/lan_query.hpp"

namespace TestNetwLanQuery {

using namespace godot;
namespace lan = netw::connect::lan;

TEST_CASE(
    "[Networked][Connect][Hosted] a LAN reply is read only by the game and the "
    "query it answers, so another game's host or a stray packet lists nothing"
) {
    Ref<netw::NetwServerInfo> info;
    info.instantiate();
    const PackedByteArray reply = lan::encode_reply(7, 11, 99, 30301, info);

    lan::Reply read;
    CHECK(lan::decode_reply(reply, 7, 11, read));
    NETW_CHECK_EQ(read.gameplay_port, int64_t(30301));
    CHECK_FALSE(lan::decode_reply(reply, 8, 11, read));
    CHECK_FALSE(lan::decode_reply(reply.slice(0, 20), 7, 11, read));
}

} // namespace TestNetwLanQuery
