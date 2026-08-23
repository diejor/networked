// The link simulator against its golden.
//
// Delay, loss, reordering and duplication are all one random stream apiece,
// and a stream drawn one time too many or seeded one bit differently still
// produces plausible-looking traffic. The only thing that catches that is an
// exact delivery order, which is what the golden holds: three scenarios over
// several seeds, each an order this simulator is required to reproduce byte
// for byte.
//
// The golden lives in the project, so this file runs in the tier that can read
// res:// and carries no [Hosted] tag.

#include "support/netw_test.h"

#include "godot/file_access.hpp"
#include "netw/api/loopback.hpp"

namespace TestNetwLoopbackPermutation {

using namespace godot;
using netw::LocalLinkConditions;
using netw::LocalLoopbackSession;
using netw::LocalMultiplayerPeer;

constexpr const char *GOLDEN
    = "res://tests/native/goldens/transport_permutations.txt";
constexpr double PERIOD_MS = 1000.0 / 60.0;
constexpr int PACKET_COUNT = 8;
constexpr int MIXED_PACKET_COUNT = 12;

struct Pair {
    Ref<LocalLoopbackSession> session;
    Ref<LocalMultiplayerPeer> server;
    Ref<LocalMultiplayerPeer> client;
};

Pair connected_pair() {
    Pair pair;
    pair.session.instantiate();
    pair.server = pair.session->get_server_peer();
    pair.client = pair.session->create_client_peer();
    pair.session->poll();
    return pair;
}

void send_unreliable(const Pair &p_pair, int p_count, bool p_poll_between) {
    p_pair.client->set_target_peer(1);
    p_pair.client->set_transfer_mode(MultiplayerPeer::TRANSFER_MODE_UNRELIABLE);
    for (int value = 0; value < p_count; ++value) {
        PackedByteArray bytes;
        bytes.push_back(uint8_t(value));
#if defined(NETW_TIER_MODULE)
        p_pair.client->put_packet(bytes.ptr(), bytes.size());
#else
        p_pair.client->put_packet(bytes);
#endif
        if (p_poll_between) {
            p_pair.session->poll();
        }
    }
}

Vector<int> drain(const Ref<LocalMultiplayerPeer> &p_peer) {
    Vector<int> values;
    while (p_peer->get_available_packet_count() > 0) {
#if defined(NETW_TIER_MODULE)
        const uint8_t *buffer = nullptr;
        int size = 0;
        p_peer->get_packet(&buffer, size);
        values.push_back(size > 0 ? int(buffer[0]) : -1);
#else
        values.push_back(int(p_peer->get_packet()[0]));
#endif
    }
    return values;
}

Vector<int> first_occurrences(const Vector<int> &p_values) {
    Vector<int> result;
    for (const int value : p_values) {
        if (!result.has(value)) {
            result.push_back(value);
        }
    }
    return result;
}

String as_text(const Vector<int> &p_values) {
    String text;
    for (const int value : p_values) {
        if (!text.is_empty()) {
            text += String(",");
        }
        text += String::num_int64(value);
    }
    return text;
}

Vector<int> run_reorder(int64_t p_seed) {
    Pair pair = connected_pair();
    Ref<LocalLinkConditions> conditions = LocalLinkConditions::create(p_seed);
    conditions->set_jitter_ms(4.0 * PERIOD_MS);
    conditions->set_reorder(1.0);
    pair.session->set_link_conditions(pair.server.ptr(), conditions);

    send_unreliable(pair, PACKET_COUNT, true);
    for (int poll = 0; poll < 10; ++poll) {
        pair.session->poll();
    }
    const Vector<int> order = first_occurrences(drain(pair.server));
    pair.session->reset();
    return order;
}

Vector<int> run_clear_flush(int64_t p_seed) {
    Pair pair = connected_pair();
    Ref<LocalLinkConditions> conditions = LocalLinkConditions::create(p_seed);
    conditions->set_latency_ms(11.0 * PERIOD_MS);
    conditions->set_jitter_ms(4.0 * PERIOD_MS);
    conditions->set_reorder(1.0);
    pair.session->set_link_conditions(pair.server.ptr(), conditions);

    send_unreliable(pair, PACKET_COUNT, false);
    pair.session->poll();
    pair.session->clear_link_conditions(pair.server.ptr());
    const Vector<int> order = drain(pair.server);
    pair.session->reset();
    return order;
}

Vector<int> run_mixed(int64_t p_seed) {
    Pair pair = connected_pair();
    Ref<LocalLinkConditions> conditions = LocalLinkConditions::create(p_seed);
    conditions->set_jitter_ms(4.0 * PERIOD_MS);
    conditions->set_reorder(1.0);
    conditions->set_duplicate(0.5);
    conditions->set_packet_loss(0.25);
    pair.session->set_link_conditions(pair.server.ptr(), conditions);

    send_unreliable(pair, MIXED_PACKET_COUNT, true);
    for (int poll = 0; poll < MIXED_PACKET_COUNT; ++poll) {
        pair.session->poll();
    }
    const Vector<int> order = drain(pair.server);
    pair.session->reset();
    return order;
}

Vector<int> run(const String &p_scenario, int64_t p_seed) {
    if (p_scenario == String("reorder")) {
        return run_reorder(p_seed);
    }
    if (p_scenario == String("clear_flush")) {
        return run_clear_flush(p_seed);
    }
    if (p_scenario == String("mixed")) {
        return run_mixed(p_seed);
    }
    FAIL("unknown scenario in the golden");
    return Vector<int>();
}

TEST_CASE(
    "[Networked][Transport] The link simulator reproduces the recorded "
    "delivery orders"
) {
    Ref<FileAccess> file = FileAccess::open(GOLDEN, FileAccess::READ);
    REQUIRE(file.is_valid());
    if (file.is_null()) {
        return;
    }

    int rows = 0;
    while (!file->eof_reached()) {
        const String line = file->get_line().strip_edges();
        if (line.is_empty() || line.begins_with("#")) {
            continue;
        }
        const PackedStringArray parts = line.split(" ", false);
        REQUIRE(parts.size() == 3);

        // Captured through char arrays rather than pointers: this tier's
        // printer streams a `const char *` as a pointer and dies doing it, and
        // it only does so on a FAILING row, so the crash wears a passing
        // instrument's face until the day the instrument is needed.
        NETW_FORMAT_TEXT(label, line.utf8().get_data());
        CAPTURE(label);

        const String produced = as_text(run(parts[0], parts[1].to_int()));
        NETW_FORMAT_TEXT(actual, produced.utf8().get_data());
        CAPTURE(actual);
        CHECK(bool(produced == parts[2]));
        rows += 1;
    }
    file->close();

    // A golden nobody read is a golden that passes.
    CHECK(rows > 0);
}

} // namespace TestNetwLoopbackPermutation
