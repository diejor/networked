#include "netw/repl/send_pass.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

PassResult SendPass::run(
    const wire::WireRegistry &p_registry,
    int p_peer,
    LocalVector<wire::FitCandidate> &p_offers,
    int64_t p_max_bits
) {
    NETW_ZONE_NC("Send pass", colors::WIRE);
    NETW_ZONE_VALUE(p_offers.size());
    PassResult out;

    const wire::FitResult fitted
        = wire::WireFitter::fit(p_registry, p_offers, p_max_bits);

    for (uint32_t at = 0; at < fitted.packed.size(); ++at) {
        out.sent.push_back(fitted.packed[at]);
        out.sent_bits += fitted.packed[at].payload_bits;
    }
    for (uint32_t at = 0; at < fitted.deferred.size(); ++at) {
        out.deferred.push_back(fitted.deferred[at]);
    }
    books[p_peer];
    return out;
}

bool SendPass::record_datagram(
    int p_peer,
    uint16_t p_seq,
    uint16_t p_frames,
    int64_t p_bits
) {
    return books[p_peer].record_send(p_seq, p_frames, p_bits);
}

void SendPass::acknowledge(
    int p_peer,
    uint16_t p_ack_seq,
    uint32_t p_history,
    LocalVector<wire::AckEntry> &r_delivered,
    LocalVector<wire::AckEntry> &r_lost
) {
    wire::AckBook *book = books.getptr(p_peer);
    if (book == nullptr) {
        r_delivered.clear();
        r_lost.clear();
        return;
    }
    book->process_ack(p_ack_seq, p_history, r_delivered, r_lost);
}

void SendPass::forget(int p_peer) {
    books.erase(p_peer);
}

int SendPass::outstanding() const {
    int total = 0;
    for (const KeyValue<int, wire::AckBook> &entry : books) {
        total += entry.value.active_count();
    }
    return total;
}

int SendPass::outstanding(int p_peer) const {
    const wire::AckBook *book = books.getptr(p_peer);
    return book == nullptr ? 0 : book->active_count();
}

} // namespace netw::repl
