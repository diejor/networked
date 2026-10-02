#include "netw/repl/send_pass.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

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
