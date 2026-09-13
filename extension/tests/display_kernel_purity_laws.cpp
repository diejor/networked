#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"

#include "godot/file_access.hpp"
#include "godot/variant.hpp"

namespace TestDisplayKernelPurityLaws {

using namespace godot;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

const char *PUMP_SOURCE = "res://extension/src/display/pump.cpp";

const char *FORBIDDEN[] = {
    "ClockEngine",
    "clock_engine(",
    "->tick(",
    "after_tick",
    "get_tree(",
    "get_node",
    "get_parent(",
    "add_child(",
    "find_children",
    "NetwEntity::of(",
    "Engine::",
    "Time::",
    "OS::",
    "script::model::",
    "replication_config",
    "synchronizers(",
    ".replication_plane()",
    "is_multiplayer_authority",
    "PumpHooks",
    "hooks",
    "emit_signal(",
    "->connect(",
    ".connect(",
    "->disconnect(",
    ".disconnect(",
    nullptr,
};

struct KernelScenario {
    String label;
    const char *signature = nullptr;
    const char *witness = nullptr;
};

KernelScenario kernel(const char *p_label, const char *p_sig, const char *p_w) {
    KernelScenario scenario;
    scenario.label = p_label;
    scenario.signature = p_sig;
    scenario.witness = p_w;
    return scenario;
}

class KernelBody {
    KernelScenario declared;
    String body;
    int source_lines = 0;
    String offence;

public:
    explicit KernelBody(const KernelScenario &p_scenario)
        : declared(p_scenario) {
        const PackedStringArray lines
            = FileAccess::get_file_as_string(PUMP_SOURCE).split("\n");
        source_lines = int(lines.size());
        bool inside = false;
        for (int at = 0; at < lines.size(); at++) {
            if (!inside) {
                inside = lines[at].begins_with(declared.signature);
                continue;
            }
            if (lines[at] == "}") {
                break;
            }
            body += lines[at] + String("\n");
        }
        for (int at = 0; FORBIDDEN[at] != nullptr; at++) {
            if (body.contains(FORBIDDEN[at])) {
                offence = String(FORBIDDEN[at]);
                break;
            }
        }
    }

    const KernelScenario &scenario() const {
        return declared;
    }

    int lines_read() const {
        return source_lines;
    }

    bool carries_its_witness() const {
        return body.contains(declared.witness);
    }

    const String &first_offence() const {
        return offence;
    }
};

typedef LawRowFor<KernelBody> PurityLaw;

LawVerdict law_extracted(const KernelBody &p_body) {
    if (p_body.lines_read() < 2) {
        return law_broken(
            "the pump source read as nothing, so this lint holds over an "
            "empty string"
        );
    }
    if (!p_body.carries_its_witness()) {
        return law_broken(
            "the extracted body carries no '%s', so the region is not the "
            "kernel this scenario names",
            p_body.scenario().witness
        );
    }
    return law_held();
}

LawVerdict law_pure(const KernelBody &p_body) {
    if (!p_body.first_offence().is_empty()) {
        const CharString text = p_body.first_offence().utf8();
        return law_broken("the kernel reaches for '%s'", text.get_data());
    }
    return law_held();
}

const PurityLaw L_EXTRACTED = {
    "extracted",
    "the region read is the kernel the scenario names, not an empty string",
    &law_extracted,
};

const PurityLaw L_PURE = {
    "pure",
    "the kernel takes its time by value and touches nothing ambient",
    &law_pure,
};

const PurityLaw LAWS[] = {L_EXTRACTED, L_PURE};

TEST_CASE(
    "[Networked][Discipline] the display pump kernels stay pure, so the "
    "calculus and the fork-join can drive them with no scene"
) {
    const KernelScenario CORPUS[] = {
        kernel("pump-history", "void pump_history(", "display_lag"),
        kernel("dilate-playhead", "void dilate_playhead(", "starving"),
        kernel(
            "chase-smooth-time",
            "double chase_smooth_time(",
            "predicted_smooth_time"
        ),
        kernel("glide", "double glide(", "chase_glide_time"),
        kernel("take-trace-frame", "bool take_trace_frame(", "trace_interval"),
        kernel("pump-chase", "void pump_chase(", "get_source_prop"),
    };
    for (const KernelScenario &scenario : CORPUS) {
        const KernelBody body(scenario);
        for (const PurityLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, body);
        }
    }
}

TEST_CASE(
    "[Networked][Discipline] a signature no line begins with reds "
    "extracted, so a renamed kernel cannot pass over nothing"
) {
    const KernelScenario scenario
        = kernel("a-kernel-that-left", "void pump_nothing(", "display_lag");
    const KernelBody body(scenario);
    NETW_CELL(L_EXTRACTED, scenario);
    NETW_LAW_BREAKS(L_EXTRACTED, body);
}

} // namespace TestDisplayKernelPurityLaws

#endif
