#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/carrier_row.hpp"
#include "netw/repl/link_governor.hpp"
#include "netw/replication_send.hpp"

using namespace godot;

namespace TestNetwReplAckSentinel {

using godot::LocalVector;
using netw::CarrierRow;
using netw::ReplicationSend;
using netw::repl::AckReport;
using netw::repl::LinkGovernor;

const int64_t PEER = 4;
const int64_t NO_SEQUENCE = -1;
const int64_t FRAMES = 1;
const int64_t BITS = 800;
const int64_t FULL_BITS = 9600;
const int64_t PAST_THE_ACK_HISTORY = 100;

LocalVector<CarrierRow> no_rows() {
    return LocalVector<CarrierRow>();
}

AckReport acknowledge(ReplicationSend &r_send, int64_t p_seq) {
    return r_send.acknowledge(PEER, p_seq, 0, 0.0, 0.0, 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a datagram with no sequence is not ack tracked"
) {
    ReplicationSend send;

    const bool refused
        = !send.commit(PEER, NO_SEQUENCE, no_rows(), FRAMES, BITS);
    CHECK(refused);
    NETW_CHECK_EQ(send.outstanding(), int64_t(0));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a sequenceless datagram is never reported lost"
) {
    ReplicationSend send;
    send.commit(PEER, NO_SEQUENCE, no_rows(), FRAMES, BITS);

    const AckReport report = acknowledge(send, PAST_THE_ACK_HISTORY);

    NETW_CHECK_EQ(int64_t(report.lost), int64_t(0));
}

TEST_CASE(
    "[Networked][Repl][Hosted] reliable traffic alone leaves the link good"
) {
    ReplicationSend send;
    for (int at = 0; at < int(LinkGovernor::WINDOW) * 4; ++at) {
        send.commit(PEER, NO_SEQUENCE, no_rows(), FRAMES, BITS);
        acknowledge(send, int64_t(at) + 1);
    }

    const bool it_stayed_good = send.link_mode(PEER) == LinkGovernor::GOOD;
    CHECK(it_stayed_good);
    NETW_CHECK_EQ(send.link_budget_bits(PEER, FULL_BITS), FULL_BITS);
}

} // namespace TestNetwReplAckSentinel
