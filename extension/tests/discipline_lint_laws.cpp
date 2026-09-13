#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"

#include <godot_cpp/classes/dir_access.hpp>

#include "godot/file_access.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace TestNetwDisciplineLintLaws {

using namespace godot;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

const char *ADDON_ROOT = "res://addons/networked";
const char *CONNECT_ROOT = "res://addons/networked/gdscript/connect";
enum Plant {
    PLANT_NONE,
    PLANT_A_PATTERN_THAT_MATCHES_EVERY_CALL,
    PLANT_A_PATTERN_EVERY_SCRIPT_CARRIES,
};

struct LintScenario {
    String label;
    const char *root = ADDON_ROOT;
    const char *patterns[10] = {nullptr};
    bool exempts_a_comment = false;
};

LintScenario no_native_name_is_shadowed() {
    LintScenario scenario;
    scenario.label = "no-native-name-is-shadowed";
    scenario.root = ADDON_ROOT;
    scenario.patterns[0] = "func is_server(";
    scenario.patterns[1] = "func get_unique_id(";
    scenario.patterns[2] = "func has_multiplayer_peer(";
    return scenario;
}

LintScenario the_kit_reaches_no_core_private() {
    LintScenario scenario;
    scenario.label = "the-kit-reaches-no-core-private";
    scenario.root = CONNECT_ROOT;
    scenario.patterns[0] = "._session";
    scenario.patterns[1] = "._replication";
    scenario.patterns[2] = "._interest";
    scenario.patterns[3] = "._liveness";
    scenario.patterns[4] = "._roster";
    scenario.patterns[5] = "._clock";
    scenario.patterns[6] = "._embedding";
    scenario.patterns[7] = "._persistence";
    scenario.patterns[8] = "._display.";
    scenario.exempts_a_comment = true;
    return scenario;
}

void walk(const String &p_root, PackedStringArray &r_files) {
    const Ref<DirAccess> dir = DirAccess::open(p_root);
    if (dir.is_null()) {
        return;
    }
    const PackedStringArray entries = dir->get_files();
    for (int index = 0; index < entries.size(); index++) {
        if (entries[index].ends_with(".gd")) {
            r_files.push_back(p_root.path_join(entries[index]));
        }
    }
    const PackedStringArray folders = dir->get_directories();
    for (int index = 0; index < folders.size(); index++) {
        walk(p_root.path_join(folders[index]), r_files);
    }
}

class LintRun {
    LintScenario declared;
    Plant planted = PLANT_NONE;
    int files_read = 0;
    int offences = 0;
    String first_offence;

    String pattern_at(int p_index) const {
        const String written(declared.patterns[p_index]);
        if (planted == PLANT_A_PATTERN_EVERY_SCRIPT_CARRIES) {
            return String("func ");
        }
        if (planted == PLANT_A_PATTERN_THAT_MATCHES_EVERY_CALL) {
            return written.trim_prefix("func ");
        }
        return written;
    }

public:
    LintRun(const LintScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        const String root(declared.root);
        PackedStringArray files;
        walk(root, files);
        files_read = int(files.size());

        for (int at = 0; at < files.size(); at++) {
            const PackedStringArray lines
                = FileAccess::get_file_as_string(files[at]).split("\n");
            for (int line = 0; line < lines.size(); line++) {
                if (declared.exempts_a_comment
                    && lines[line].strip_edges().begins_with("#")) {
                    continue;
                }
                for (int index = 0; index < 10; index++) {
                    if (declared.patterns[index] == nullptr) {
                        break;
                    }
                    if (!lines[line].contains(pattern_at(index))) {
                        continue;
                    }
                    offences++;
                    if (first_offence.is_empty()) {
                        first_offence = files[at] + ":" + itos(line + 1);
                    }
                }
            }
        }
    }

    const LintScenario &scenario() const {
        return declared;
    }

    int files() const {
        return files_read;
    }

    int offenders() const {
        return offences;
    }

    const String &first() const {
        return first_offence;
    }
};

typedef LawRowFor<LintRun> LintLaw;

LawVerdict law_swept(const LintRun &p_run) {
    if (p_run.files() < 1) {
        return law_broken(
            "the root holds no GDScript, so this reads green over nothing"
        );
    }
    return law_held();
}

LawVerdict law_clean(const LintRun &p_run) {
    if (p_run.offenders() != 0) {
        const CharString text = p_run.first().utf8();
        return law_broken(
            "%d lines break the rule, the first at %s",
            p_run.offenders(),
            text.get_data()
        );
    }
    return law_held();
}

const LintLaw L_SWEPT = {
    "swept",
    "the rule was read against a tree that actually holds GDScript",
    &law_swept,
};

const LintLaw L_CLEAN = {
    "clean",
    "no line under the root breaks the rule the scenario names",
    &law_clean,
};

const LintLaw LAWS[] = {L_SWEPT, L_CLEAN};

TEST_CASE("[Networked][Discipline] the GDScript discipline laws hold") {
    const LintScenario CORPUS[] = {
        no_native_name_is_shadowed(),
        the_kit_reaches_no_core_private(),
    };
    for (const LintScenario &scenario : CORPUS) {
        const LintRun run(scenario);
        for (const LintLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Discipline] a pattern that matches every call site reds "
    "clean"
) {
    const LintScenario scenario = no_native_name_is_shadowed();
    const LintRun run(scenario, PLANT_A_PATTERN_THAT_MATCHES_EVERY_CALL);
    NETW_CELL(L_CLEAN, scenario);
    NETW_LAW_BREAKS(L_CLEAN, run);
}

TEST_CASE(
    "[Networked][Discipline] a pattern every script carries reds clean "
    "over the kit"
) {
    const LintScenario scenario = the_kit_reaches_no_core_private();
    const LintRun run(scenario, PLANT_A_PATTERN_EVERY_SCRIPT_CARRIES);
    NETW_CELL(L_CLEAN, scenario);
    NETW_LAW_BREAKS(L_CLEAN, run);
}

} // namespace TestNetwDisciplineLintLaws

#endif
