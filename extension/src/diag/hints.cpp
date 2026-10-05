#include "netw/diag/hints.hpp"

#if defined(DEBUG_ENABLED)

#include <atomic>

#include "godot/os.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw::diag {

namespace {

struct NamedHint {
    const char *name;
    Hint bit;
};

constexpr NamedHint known_hints[] = {
    {"prediction", HINT_PREDICTION},
};

std::atomic<uint32_t> configured_mask = 0;

String requested_list(
    const String &p_environment,
    const PackedStringArray &p_args
) {
    if (!p_environment.is_empty()) {
        return p_environment;
    }
    const String flag = "--netw-hints=";
    for (int at = 0; at < p_args.size(); ++at) {
        const String arg = p_args[at];
        if (arg.begins_with(flag)) {
            return arg.substr(flag.length());
        }
    }
    return String();
}

String known_names() {
    PackedStringArray names;
    for (const NamedHint &each : known_hints) {
        names.push_back(each.name);
    }
    return String(", ").join(names);
}

} // namespace

HintRequest parse_hints(
    const String &p_environment,
    const PackedStringArray &p_args
) {
    HintRequest request;
    const PackedStringArray names
        = requested_list(p_environment, p_args).split(",", false);
    for (int at = 0; at < names.size(); ++at) {
        const String name = names[at].strip_edges().to_lower();
        if (name.is_empty() || name == "none") {
            continue;
        }
        bool matched = false;
        for (const NamedHint &each : known_hints) {
            if (name == each.name) {
                request.mask |= each.bit;
                matched = true;
            }
        }
        if (!matched) {
            request.unknown.push_back(name);
        }
    }
    return request;
}

void configure_hints() {
    const HintRequest request = parse_hints(
        OS::get_singleton()->get_environment("NETW_HINTS"),
        gd::cmdline_args()
    );
    if (!request.unknown.is_empty()) {
        NETW_WARN(
            sys::SESSION,
            "Unknown hint '%s' in --netw-hints or NETW_HINTS. Use one of %s.",
            String("', '").join(request.unknown),
            known_names()
        );
    }
    configured_mask.store(request.mask, std::memory_order_relaxed);
}

uint32_t hints() {
    return configured_mask.load(std::memory_order_relaxed);
}

} // namespace netw::diag

#endif
