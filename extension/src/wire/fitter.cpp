#include "netw/wire/fitter.hpp"

#include <algorithm>

#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw::wire {

FitResult WireFitter::fit(
    const WireRegistry &registry,
    godot::LocalVector<FitCandidate> &candidates,
    int64_t max_bits
) {
    NETW_ZONE_NC("WireFitter fit", colors::WIRE);
    NETW_ZONE_VALUE(candidates.size());
    FitResult result;

    std::sort(
        candidates.ptr(),
        candidates.ptr() + candidates.size(),
        [](const FitCandidate &a, const FitCandidate &b) {
            return a.accumulated_priority > b.accumulated_priority;
        }
    );

    int64_t current_bits = 0;
    for (uint32_t i = 0; i < candidates.size(); ++i) {
        FitCandidate &cand = candidates[i];
        const ChannelDecl *decl = registry.find_channel(cand.channel_id);
        if (decl != nullptr && decl->delivery == Delivery::IMMEDIATE) {
            result.packed.push_back(cand);
            current_bits += cand.payload_bits;
            continue;
        }

        if (current_bits + cand.payload_bits <= max_bits) {
            result.packed.push_back(cand);
            current_bits += cand.payload_bits;
        } else {
            cand.accumulated_priority += cand.priority;
            result.deferred.push_back(cand);
        }
    }

    result.total_bits = current_bits;
    return result;
}

} // namespace netw::wire
