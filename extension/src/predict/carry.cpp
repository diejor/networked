#include "netw/predict/carry.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"

namespace netw {

namespace predict {

void CarryTrack::resize(int p_count) {
    fields.resize(uint32_t(p_count < 0 ? 0 : p_count));
    for (uint32_t at = 0; at < fields.size(); ++at) {
        fields[at] = CarryFieldStats();
    }
}

const CarryFieldStats *CarryTrack::field(int p_field) const {
    return p_field >= 0 && p_field < int(fields.size())
        ? &fields[uint32_t(p_field)]
        : nullptr;
}

CarryVerdict CarryTrack::judge(
    int p_field,
    int p_schedule,
    const CarryProbe &p_probe
) {
    NETW_ZONE_NC("NetwPredict carry judge", colors::PREDICTION);
    NETW_ERR_COND_V(
        p_field < 0 || p_field >= int(fields.size()),
        CarryVerdict::DECLINED,
        "prediction",
        "Carry evidence names field %d outside width %d.",
        p_field,
        int(fields.size())
    );
    CarryFieldStats &stats = fields[uint32_t(p_field)];
    if (stats.retired) {
        stats.declined += 1;
        return CarryVerdict::RETIRED_ALREADY;
    }
    if (p_schedule != int(Schedule::FRAME)) {
        stats.declined += 1;
        stats.retired = true;
        NETW_WARN(
            "prediction",
            "carry_step field=%d retired because its schedule is not FRAME.",
            p_field
        );
        return CarryVerdict::RETIRED_SCHEDULE;
    }
    if (!p_probe.pure) {
        stats.declined += 1;
        stats.retired = true;
        NETW_WARN(
            "prediction",
            "carry_step field=%d retired after writing live state.",
            p_field
        );
        return CarryVerdict::RETIRED_IMPURE;
    }
    if (!p_probe.same_type || !p_probe.finite || !p_probe.within_envelope) {
        stats.declined += 1;
        NETW_DEBUG(
            "prediction",
            "carry_step field=%d declined type=%d finite=%d envelope=%d",
            p_field,
            int(p_probe.same_type),
            int(p_probe.finite),
            int(p_probe.within_envelope)
        );
        return CarryVerdict::DECLINED;
    }
    if (!p_probe.faithful) {
        stats.declined += 1;
        stats.infidelity += 1;
        if (stats.infidelity >= CARRY_INFIDELITY_LIMIT) {
            stats.retired = true;
            NETW_WARN(
                "prediction",
                "carry_step field=%d retired after %d failed replays.",
                p_field,
                stats.infidelity
            );
            return CarryVerdict::RETIRED_INFIDELITY;
        }
        NETW_DEBUG(
            "prediction",
            "carry_step field=%d replayed unfaithfully, run=%d",
            p_field,
            stats.infidelity
        );
        return CarryVerdict::UNFAITHFUL;
    }
    stats.carried += 1;
    return CarryVerdict::CARRIED;
}

CarryVerdict CarryTrack::decline(int p_field, int p_schedule) {
    NETW_ERR_COND_V(
        p_field < 0 || p_field >= int(fields.size()),
        CarryVerdict::DECLINED,
        "prediction",
        "Carry refusal names field %d outside width %d.",
        p_field,
        int(fields.size())
    );
    CarryFieldStats &stats = fields[uint32_t(p_field)];
    stats.declined += 1;
    if (stats.retired) {
        return CarryVerdict::RETIRED_ALREADY;
    }
    if (p_schedule != int(Schedule::FRAME)) {
        stats.retired = true;
        NETW_WARN(
            "prediction",
            "carry_step field=%d retired because its schedule is not FRAME.",
            p_field
        );
        return CarryVerdict::RETIRED_SCHEDULE;
    }
    return CarryVerdict::DECLINED;
}

} // namespace predict

} // namespace netw
