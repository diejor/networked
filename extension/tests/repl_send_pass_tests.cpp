#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/repl/send_pass.hpp"

using namespace godot;

namespace TestNetwReplSendPass {

using godot::LocalVector;
using netw::repl::SendPass;
using netw::wire::AckEntry;

const int PEER = 7;
const int OTHER = 9;

TEST_CASE(
    "[Networked][Repl][Hosted] a pass records nothing until the carrier names "
    "the seq"
) {
    SendPass pass;
    NETW_CHECK_EQ(pass.outstanding(), 0);

    REQUIRE(pass.record_datagram(PEER, 1, 3, 900));
    NETW_CHECK_EQ(pass.outstanding(), 1);
    NETW_CHECK_EQ(pass.outstanding(PEER), 1);

    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    pass.acknowledge(PEER, 1, 0, delivered, lost);
    NETW_CHECK_EQ(delivered.size(), 1);
    NETW_CHECK_EQ(delivered[0].frames, 3);
    NETW_CHECK_EQ(delivered[0].bits, 900);
    NETW_CHECK_EQ(pass.outstanding(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] one peer's book never answers for another's"
) {
    SendPass pass;

    REQUIRE(pass.record_datagram(PEER, 1, 1, 300));
    REQUIRE(pass.record_datagram(OTHER, 1, 1, 300));
    NETW_CHECK_EQ(pass.outstanding(), 2);

    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    pass.acknowledge(PEER, 1, 0, delivered, lost);
    NETW_CHECK_EQ(delivered.size(), 1);
    NETW_CHECK_EQ(pass.outstanding(PEER), 0);
    NETW_CHECK_EQ(pass.outstanding(OTHER), 1);

    pass.forget(OTHER);
    NETW_CHECK_EQ(pass.outstanding(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] the delivery history settles the datagrams the "
    "ack itself does not name"
) {
    SendPass pass;

    REQUIRE(pass.record_datagram(PEER, 8, 1, 300));
    REQUIRE(pass.record_datagram(PEER, 9, 1, 300));
    REQUIRE(pass.record_datagram(PEER, 10, 1, 300));

    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    const uint32_t eight_arrived_nine_did_not = 0x2;
    pass.acknowledge(PEER, 10, eight_arrived_nine_did_not, delivered, lost);

    NETW_CHECK_EQ(delivered.size(), 2);
    NETW_CHECK_EQ(lost.size(), 0);
    NETW_CHECK_EQ(pass.outstanding(PEER), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a datagram the history can no longer reach is "
    "reported lost"
) {
    SendPass pass;

    REQUIRE(pass.record_datagram(PEER, 1, 1, 300));

    LocalVector<AckEntry> delivered;
    LocalVector<AckEntry> lost;
    pass.acknowledge(PEER, 40, 0, delivered, lost);

    NETW_CHECK_EQ(delivered.size(), 0);
    NETW_CHECK_EQ(lost.size(), 1);
    NETW_CHECK_EQ(lost[0].seq, 1);
    NETW_CHECK_EQ(pass.outstanding(PEER), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a seq whose slot is still live is not recorded "
    "over"
) {
    SendPass pass;

    REQUIRE(pass.record_datagram(PEER, 1, 1, 300));

    ERR_PRINT_OFF;
    const bool the_colliding_seq_is_refused
        = !pass.record_datagram(PEER, 129, 1, 300);
    ERR_PRINT_ON;
    CHECK(the_colliding_seq_is_refused);
    NETW_CHECK_EQ(pass.outstanding(PEER), 1);
}

} // namespace TestNetwReplSendPass
