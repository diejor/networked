#include "netw/repl/receive_pass.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

ReceiveResult ReceivePass::run(
    const LocalVector<InboundFrame> &p_frames,
    AdmitFn p_admit,
    ApplyFn p_apply,
    void *p_context
) {
    NETW_ZONE_NC("Receive pass", colors::WIRE);
    NETW_ZONE_VALUE(p_frames.size());
    ReceiveResult out;
    out.verdicts.resize(p_frames.size());

    for (uint32_t at = 0; at < p_frames.size(); ++at) {
        const InboundFrame &frame = p_frames[at];
        Error verdict = p_admit != nullptr
            ? p_admit(frame, p_context)
            : Error::OK;
        if (verdict == Error::OK) {
            verdict = p_apply != nullptr
                ? p_apply(frame, p_context)
                : Error::OK;
        }
        out.verdicts[at] = verdict;
        if (verdict == Error::OK) {
            out.admitted += 1;
        } else {
            out.refused += 1;
        }
    }
    return out;
}

} // namespace netw::repl
