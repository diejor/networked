#include "netw/repl/send_pass.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

PassResult SendPass::run(
    const wire::WireRegistry &p_registry,
    LocalVector<wire::FitCandidate> &p_offers,
    int64_t p_max_bits,
    uint16_t p_seq,
    int64_t p_send_id
) {
    NETW_ZONE_NC("Send pass", colors::WIRE);
    NETW_ZONE_VALUE(p_offers.size());
    PassResult out;

    const wire::FitResult fitted
        = wire::WireFitter::fit(p_registry, p_offers, p_max_bits);

    const bool carries = !fitted.packed.is_empty();
    const uint8_t channel
        = carries ? fitted.packed[0].channel_id : uint8_t(0);
    if (carries && !acks.record_send(p_seq, channel, p_send_id)) {
        NETW_WARN_ONCE(
            sys::WIRE,
            "seq %d holds a live ack entry, so a datagram of %d frame(s) is "
            "deferred rather than sent untracked",
            int(p_seq),
            int(fitted.packed.size())
        );
        out.untrackable = true;
        for (uint32_t at = 0; at < fitted.packed.size(); ++at) {
            wire::FitCandidate deferred = fitted.packed[at];
            deferred.accumulated_priority += deferred.priority;
            out.deferred.push_back(deferred);
        }
    } else {
        for (uint32_t at = 0; at < fitted.packed.size(); ++at) {
            out.sent.push_back(fitted.packed[at]);
            out.sent_bits += fitted.packed[at].payload_bits;
        }
    }

    for (uint32_t at = 0; at < fitted.deferred.size(); ++at) {
        out.deferred.push_back(fitted.deferred[at]);
    }
    return out;
}

void SendPass::acknowledge(
    uint16_t p_ack_seq,
    LocalVector<wire::AckEntry> &r_delivered,
    LocalVector<wire::AckEntry> &r_lost
) {
    acks.process_ack(p_ack_seq, r_delivered, r_lost);
}

} // namespace netw::repl
