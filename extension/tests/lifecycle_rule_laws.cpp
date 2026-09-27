#include "support/netw_test.h"

#include "support/netw_cells.h"

#include "godot/variant.hpp"
#include "netw/lifecycle/rule.hpp"

namespace TestNetwLifecycleRuleLaws {

using namespace godot;
using netw::lifecycle::Claim;
using netw::lifecycle::Destination;
using netw::lifecycle::Facts;
using netw::lifecycle::Kind;
using netw::lifecycle::Ruling;
using netw::lifecycle::Verdict;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

enum PeerClass {
    SESSION,
    CONFIRMED_CONTROLLER,
    IMMEDIATE_CLAIMANT,
    REQUESTABLE_CLAIMANT,
    OTHER_PEER,
};

enum Base {
    BASE_CURRENT,
    BASE_STALE,
};

const Kind KINDS[] = {Kind::SPAWN, Kind::DESPAWN, Kind::REPARENT};
const char *KIND_NAMES[] = {"spawn", "despawn", "reparent"};

const PeerClass PEERS[] = {
    SESSION,
    CONFIRMED_CONTROLLER,
    IMMEDIATE_CLAIMANT,
    REQUESTABLE_CLAIMANT,
    OTHER_PEER,
};
const char *PEER_NAMES[] = {
    "session",
    "confirmed-controller",
    "immediate-claimant",
    "requestable-claimant",
    "other",
};

const Destination DESTINATIONS[] = {
    Destination::OK,
    Destination::OUTSIDE,
    Destination::INSIDE_MOVER,
    Destination::DYING,
    Destination::UNRESOLVABLE,
};
const char *DESTINATION_NAMES[] = {
    "ok",
    "outside",
    "inside-mover",
    "dying",
    "unresolvable",
};

const Base BASES[] = {BASE_CURRENT, BASE_STALE};
const char *BASE_NAMES[] = {"current", "stale"};

struct RuleCell {
    String label;
    int kind = 0;
    PeerClass peer = SESSION;
    bool declared = false;
    bool predicted = false;
    bool native = false;
    bool player_bound = false;
    bool load_on_spawn = false;
    bool replaying = false;
    bool halted = false;
    int destination = 0;
    Base base = BASE_CURRENT;

    Facts facts() const {
        Facts out;
        out.kind = KINDS[kind];
        out.peer_is_session = peer == SESSION;
        out.peer_is_controller = peer == CONFIRMED_CONTROLLER;
        out.peer_claim = peer == IMMEDIATE_CLAIMANT ? Claim::IMMEDIATE
            : peer == REQUESTABLE_CLAIMANT          ? Claim::REQUESTABLE
                                                    : Claim::NONE;
        out.declared = declared;
        out.predicted = predicted;
        out.native = native;
        out.player_bound = player_bound;
        out.load_on_spawn = load_on_spawn;
        out.replaying = replaying;
        out.minting_halted = halted;
        out.destination = DESTINATIONS[destination];
        out.revision = 3;
        out.base = base == BASE_STALE ? 2 : 3;
        return out;
    }

    bool off_session() const {
        return peer != SESSION;
    }

    bool places() const {
        return KINDS[kind] != Kind::DESPAWN;
    }
};

bool halted(const RuleCell &p_cell) {
    return p_cell.halted;
}

bool replaying(const RuleCell &p_cell) {
    return p_cell.replaying;
}

bool unauthorized(const RuleCell &p_cell) {
    if (!p_cell.off_session()) {
        return false;
    }
    if (!p_cell.declared) {
        return true;
    }
    if (KINDS[p_cell.kind] == Kind::SPAWN) {
        return false;
    }
    return p_cell.peer != CONFIRMED_CONTROLLER
        && p_cell.peer != IMMEDIATE_CLAIMANT;
}

bool unavailable(const RuleCell &p_cell) {
    return p_cell.off_session()
        && (p_cell.predicted || p_cell.native || p_cell.player_bound
            || p_cell.load_on_spawn);
}

bool misplaced_by_the_caller(const RuleCell &p_cell) {
    const Destination at = DESTINATIONS[p_cell.destination];
    return p_cell.places()
        && (at == Destination::OUTSIDE || at == Destination::INSIDE_MOVER);
}

bool misplaced_in_flight(const RuleCell &p_cell) {
    const Destination at = DESTINATIONS[p_cell.destination];
    return p_cell.off_session() && p_cell.places()
        && (at == Destination::DYING || at == Destination::UNRESOLVABLE);
}

bool stale(const RuleCell &p_cell) {
    return p_cell.off_session() && KINDS[p_cell.kind] != Kind::SPAWN
        && p_cell.base == BASE_STALE;
}

struct Refusal {
    const char *reason;
    bool (*applies)(const RuleCell &);
    Verdict verdict;
    Error code;
};

const Refusal REFUSALS[] = {
    {"minting is halted for a session transfer",
     &halted,
     Verdict::STATIC,
     ERR_UNAVAILABLE},
    {"the verb was issued during a prediction replay",
     &replaying,
     Verdict::STATIC,
     ERR_BUSY},
    {"the peer is not the session, and neither the confirmed controller of a "
     "declared entity nor its immediate claimant",
     &unauthorized,
     Verdict::STATIC,
     ERR_UNAUTHORIZED},
    {"a controller op on a predicted, native, player-bound or load_on_spawn "
     "entity",
     &unavailable,
     Verdict::STATIC,
     ERR_UNAVAILABLE},
    {"a destination outside the session root or inside the moved entity",
     &misplaced_by_the_caller,
     Verdict::STATIC,
     ERR_INVALID_PARAMETER},
    {"a controller op whose destination is dying or unresolvable at the "
     "session",
     &misplaced_in_flight,
     Verdict::DYNAMIC,
     ERR_INVALID_PARAMETER},
    {"a controller op built on a stale anchor revision",
     &stale,
     Verdict::DYNAMIC,
     ERR_UNAUTHORIZED},
};

const Refusal *first_refusal(const RuleCell &p_cell) {
    for (const Refusal &refusal : REFUSALS) {
        if (refusal.applies(p_cell)) {
            return &refusal;
        }
    }
    return nullptr;
}

struct RuleRun {
    const RuleCell &cell;
    Ruling ruling;
    const Refusal *expected = nullptr;

    explicit RuleRun(const RuleCell &p_cell)
        : cell(p_cell), ruling(netw::lifecycle::rule(p_cell.facts())),
          expected(first_refusal(p_cell)) {
    }
};

typedef LawRowFor<RuleRun> RuleLaw;

LawVerdict law_code(const RuleRun &p_run) {
    const Error wanted = p_run.expected != nullptr ? p_run.expected->code : OK;
    if (p_run.ruling.code != wanted) {
        return law_broken(
            "the rule answered %d where %s names %d",
            int(p_run.ruling.code),
            p_run.expected != nullptr ? p_run.expected->reason : "admission",
            int(wanted)
        );
    }
    return law_held();
}

LawVerdict law_kind(const RuleRun &p_run) {
    const Verdict wanted
        = p_run.expected != nullptr ? p_run.expected->verdict : Verdict::ADMIT;
    if (p_run.ruling.verdict != wanted) {
        return law_broken(
            "the rule ruled kind %d where %s names %d",
            int(p_run.ruling.verdict),
            p_run.expected != nullptr ? p_run.expected->reason : "admission",
            int(wanted)
        );
    }
    return law_held();
}

LawVerdict law_session_arbitrates(const RuleRun &p_run) {
    if (!p_run.cell.off_session() && p_run.ruling.verdict == Verdict::DYNAMIC) {
        return law_broken("the session's own op was refused dynamically");
    }
    return law_held();
}

const RuleLaw L_CODE = {
    "code",
    "every cell answers the first refusal it earns, or OK",
    &law_code,
};

const RuleLaw L_KIND = {
    "kind",
    "a refusal the author could have known is static, one only the session "
    "can know is dynamic",
    &law_kind,
};

const RuleLaw L_SESSION_ARBITRATES = {
    "session-arbitrates",
    "the session's own op is never refused as a race",
    &law_session_arbitrates,
};

const RuleLaw LAWS[] = {L_CODE, L_KIND, L_SESSION_ARBITRATES};

RuleCell cell_at(int p_index) {
    RuleCell cell;
    int at = p_index;
    cell.kind = at % 3;
    at /= 3;
    cell.peer = PEERS[at % 5];
    at /= 5;
    cell.declared = at % 2 == 1;
    at /= 2;
    cell.predicted = at % 2 == 1;
    at /= 2;
    cell.native = at % 2 == 1;
    at /= 2;
    cell.player_bound = at % 2 == 1;
    at /= 2;
    cell.load_on_spawn = at % 2 == 1;
    at /= 2;
    cell.replaying = at % 2 == 1;
    at /= 2;
    cell.halted = at % 2 == 1;
    at /= 2;
    cell.destination = at % 5;
    at /= 5;
    cell.base = BASES[at % 2];
    cell.label = vformat(
        "%s/%s/%s%s%s%s%s%s%s/%s/%s",
        KIND_NAMES[cell.kind],
        PEER_NAMES[cell.peer],
        cell.declared ? "declared" : "undeclared",
        cell.predicted ? "+predicted" : "",
        cell.native ? "+native" : "",
        cell.player_bound ? "+player-bound" : "",
        cell.load_on_spawn ? "+load-on-spawn" : "",
        cell.replaying ? "+replaying" : "",
        cell.halted ? "+halted" : "",
        DESTINATION_NAMES[cell.destination],
        BASE_NAMES[cell.base]
    );
    return cell;
}

constexpr int CELL_COUNT = 3 * 5 * 2 * 2 * 2 * 2 * 2 * 2 * 2 * 5 * 2;

TEST_CASE(
    "[Networked][Lifecycle][Hosted][Law] every cell of the structure rule "
    "answers the code and the static or dynamic kind its facts earn"
) {
    int refused_dynamically = 0;
    for (int index = 0; index < CELL_COUNT; index++) {
        const RuleCell cell = cell_at(index);
        const RuleRun run(cell);
        if (run.ruling.verdict == Verdict::DYNAMIC) {
            refused_dynamically += 1;
        }
        for (const RuleLaw &law : LAWS) {
            NETW_CELL(law, cell);
            NETW_LAW_HOLDS(law, run);
        }
    }
    CHECK(refused_dynamically > 0);
}

TEST_CASE(
    "[Networked][Lifecycle][Hosted][Law] with nothing declared the session "
    "authors every kind and every other peer is refused as unauthorized"
) {
    for (const Kind kind : KINDS) {
        CHECK(netw::lifecycle::rule(kind, true).admitted());
        const Ruling refused = netw::lifecycle::rule(kind, false);
        CHECK(refused.verdict == Verdict::STATIC);
        CHECK(refused.code == ERR_UNAUTHORIZED);
    }
}

} // namespace TestNetwLifecycleRuleLaws
