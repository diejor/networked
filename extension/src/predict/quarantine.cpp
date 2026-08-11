#include "netw/predict/quarantine.hpp"

#include <algorithm>

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::predict {

namespace {

bool witness_clean(int p_bits) {
    return p_bits >= 0
        && (p_bits & ~int(WITNESS_SUPPORT | WITNESS_STATIC)) == 0;
}

template <typename T>
int index_of(const LocalVector<T> &p_rows, int64_t p_basis) {
    for (uint32_t at = 0; at < p_rows.size(); ++at) {
        if (p_rows[at].basis == p_basis) {
            return int(at);
        }
    }
    return -1;
}

template <typename T> void trim_oldest(LocalVector<T> &r_rows) {
    while (int(r_rows.size()) > QUARANTINE_PENDING_LIMIT) {
        uint32_t oldest = 0;
        for (uint32_t at = 1; at < r_rows.size(); ++at) {
            if (r_rows[at].basis < r_rows[oldest].basis) {
                oldest = at;
            }
        }
        r_rows.remove_at(oldest);
    }
}

int witness_at(const LocalVector<QuarantineWitness> &p_rows, int64_t p_basis) {
    const int found = index_of(p_rows, p_basis);
    return found >= 0 ? p_rows[uint32_t(found)].bits : -1;
}

QuarantinePlan begin_reseed(
    Quarantine &r_quarantine,
    const QuarantineState &p_state
) {
    QuarantinePlan out;
    out.payload = p_state.payload;
    out.basis = p_state.basis;
    out.ready = true;
    r_quarantine.reseed_basis = p_state.basis;
    r_quarantine.latched = false;
    r_quarantine.align_pending = true;
    r_quarantine.epoch_confirmed = false;
    r_quarantine.ignore_through = -1;
    NETW_DEBUG("prediction", "quarantine reseed basis=%d", p_state.basis);
    return out;
}

QuarantinePlan select_reseed(Quarantine &r_quarantine) {
    QuarantinePlan out;
    if (!r_quarantine.latched || r_quarantine.clean_run < r_quarantine.target) {
        return out;
    }
    const int64_t clean_start
        = r_quarantine.last_basis - r_quarantine.clean_run + 1;
    const QuarantineState *in_window = nullptr;
    const QuarantineState *newest_clean = nullptr;
    const QuarantineState *newest_unknown = nullptr;
    for (uint32_t at = 0; at < r_quarantine.pending_states.size(); ++at) {
        const QuarantineState &row = r_quarantine.pending_states[at];
        if (row.basis > r_quarantine.last_basis) {
            continue;
        }
        const int bits = witness_at(r_quarantine.witnesses, row.basis);
        if (bits < 0) {
            if (newest_unknown == nullptr
                || row.basis > newest_unknown->basis) {
                newest_unknown = &row;
            }
            continue;
        }
        if (!witness_clean(bits)) {
            continue;
        }
        if (row.basis >= clean_start
            && (in_window == nullptr || row.basis > in_window->basis)) {
            in_window = &row;
        }
        if (newest_clean == nullptr || row.basis > newest_clean->basis) {
            newest_clean = &row;
        }
    }
    if (in_window != nullptr) {
        return begin_reseed(r_quarantine, *in_window);
    }
    if (r_quarantine.wait_anchor < 0) {
        r_quarantine.wait_anchor = r_quarantine.last_basis;
        return out;
    }
    if (r_quarantine.last_basis - r_quarantine.wait_anchor
        < QUARANTINE_RUN_CAP) {
        return out;
    }
    if (newest_clean != nullptr) {
        return begin_reseed(r_quarantine, *newest_clean);
    }
    if (newest_unknown != nullptr) {
        return begin_reseed(r_quarantine, *newest_unknown);
    }
    return out;
}

} // namespace

void Quarantine::enter(
    int p_ack_age,
    int p_flap_multiplier,
    bool p_stream_reconstructed
) {
    NETW_ZONE_NC("NetwPredict enter quarantine", colors::PREDICTION);
    const int base = std::clamp(
        std::max(0, p_ack_age) * QUARANTINE_RESUME_MULTIPLIER,
        QUARANTINE_RESUME_MIN,
        QUARANTINE_RUN_CAP
    );
    target
        = std::min(QUARANTINE_RUN_CAP, base * std::max(1, p_flap_multiplier));
    pending_states.clear();
    witnesses.clear();
    clean_run = 0;
    last_tick = -1;
    last_basis = -1;
    wait_anchor = -1;
    reseed_basis = -1;
    aligned_transition = -1;
    ignore_through = -1;
    latched = true;
    stream_reconstructed = p_stream_reconstructed;
    align_pending = false;
    epoch_confirmed = false;
    probation_pending = false;
    NETW_DEBUG(
        "prediction",
        "quarantine enter ack_age=%d target=%d flap=%d",
        p_ack_age,
        target,
        p_flap_multiplier
    );
}

QuarantinePlan Quarantine::admit_state(
    int64_t p_tick,
    int64_t p_basis,
    const StateRow &p_payload,
    bool p_whole
) {
    stream_reconstructed = stream_reconstructed || p_whole;
    if (!latched || !stream_reconstructed || p_tick <= last_tick || p_basis < 0
        || !p_payload.any()) {
        return QuarantinePlan();
    }
    last_tick = p_tick;
    const int found = index_of(pending_states, p_basis);
    QuarantineState row;
    row.tick = p_tick;
    row.basis = p_basis;
    row.payload = p_payload;
    if (found >= 0) {
        pending_states[uint32_t(found)] = row;
    } else {
        pending_states.push_back(row);
    }
    trim_oldest(pending_states);
    const int bits = witness_at(witnesses, p_basis);
    if (bits >= 0 && p_basis > last_basis) {
        return admit_witness(p_basis, bits);
    }
    return select_reseed(*this);
}

QuarantinePlan Quarantine::admit_witness(int64_t p_basis, int p_bits) {
    const int found = index_of(witnesses, p_basis);
    QuarantineWitness row;
    row.basis = p_basis;
    row.bits = p_bits;
    if (found >= 0) {
        witnesses[uint32_t(found)] = row;
    } else {
        witnesses.push_back(row);
    }
    trim_oldest(witnesses);
    if (!latched || p_basis <= last_basis) {
        return select_reseed(*this);
    }
    if (!witness_clean(p_bits)) {
        clean_run = 0;
        wait_anchor = -1;
    } else if (last_basis >= 0 && p_basis != last_basis + 1) {
        clean_run = 1;
        wait_anchor = -1;
    } else {
        clean_run += 1;
    }
    last_basis = p_basis;
    return select_reseed(*this);
}

void Quarantine::confirm_epoch() {
    if (align_pending) {
        epoch_confirmed = true;
    }
}

QuarantinePlan Quarantine::align(
    int64_t p_transition,
    const StateRow &p_payload,
    int64_t p_ignore_through
) {
    QuarantinePlan out;
    if (!align_pending || !epoch_confirmed || !p_payload.any()) {
        return out;
    }
    out.payload = p_payload;
    out.basis = p_transition;
    out.ready = true;
    aligned_transition = p_transition;
    ignore_through = p_ignore_through;
    align_pending = false;
    epoch_confirmed = false;
    probation_pending = true;
    NETW_DEBUG(
        "prediction",
        "quarantine align transition=%d ignore_through=%d",
        p_transition,
        p_ignore_through
    );
    return out;
}

void Quarantine::adopt_alignment(
    int64_t p_transition,
    int64_t p_ignore_through
) {
    aligned_transition = p_transition;
    ignore_through = p_ignore_through;
    align_pending = false;
    epoch_confirmed = false;
    probation_pending = true;
}

bool Quarantine::admit_post_reseed(int64_t p_basis) {
    if (p_basis <= ignore_through) {
        return false;
    }
    ignore_through = -1;
    return true;
}

bool Quarantine::finish_probation(bool p_corrected) {
    if (!probation_pending) {
        return false;
    }
    probation_pending = false;
    return p_corrected;
}

bool Quarantine::corrections_suppressed() const {
    return probation_pending;
}

} // namespace netw::predict
