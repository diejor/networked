#include "support/netw_test.h"

#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/role_facts.hpp"
#include "netw/predict/engine.hpp"
#include "netw/sim/resolve.hpp"
#include "netw/sim/row.hpp"

namespace TestNetwSimResolveLaws {

using namespace godot;
using netw::NetwPredict;
using netw::NetwPredictionEngine;
using netw::NetwPredictionHandle;
using netw::sim::Facts;
using netw::sim::Mode;
using netw::sim::Replicas;
using netw::sim::Rows;

const int64_t SESSION_AUTHORITY = 1;
const int64_t NOBODY = 0;

enum Closure {
    OPEN,
    FALLBACK,
    DELAY_CLOSED,
    CLOSED,
    ANY_CLOSURE,
};

enum Selection {
    UNSELECTED,
    SELECTED,
    ANY_SELECTION,
};

enum Inputs {
    INPUTLESS,
    INPUT_ROWS,
};

struct Peers {
    int64_t local;
    int64_t controller;
};

const Peers SERVER_STEERS = {1, 0};
const Peers SERVER_HOSTS_A_CLIENT = {1, 2};
const Peers CLIENT_STEERS = {2, 2};
const Peers CLIENT_WATCHES_THE_SERVER = {2, 0};
const Peers CLIENT_WATCHES_A_CLIENT = {3, 2};

const Peers EVERY_PEERING[] = {
    SERVER_STEERS,
    SERVER_HOSTS_A_CLIENT,
    CLIENT_STEERS,
    CLIENT_WATCHES_THE_SERVER,
    CLIENT_WATCHES_A_CLIENT,
};

struct Cell {
    const char *label;
    Peers peers;
    Inputs inputs;
    Closure closure;
    Selection selection;
    Mode mode;
    NetwPredict::InputSource source;
    NetwPredict::Role role;
    netw::display::Role display_role;
};

const Cell PARITY[] = {
    {"a controller-0 entity on the server authors and draws authority",
     SERVER_STEERS,
     INPUT_ROWS,
     ANY_CLOSURE,
     ANY_SELECTION,
     Mode::AUTHORITY,
     NetwPredict::INPUT_SOURCE_LOCAL,
     NetwPredict::ROLE_HOST_LOCAL,
     netw::display::ROLE_AUTHORITY},
    {"an inputless controller-0 entity on the server",
     SERVER_STEERS,
     INPUTLESS,
     ANY_CLOSURE,
     ANY_SELECTION,
     Mode::AUTHORITY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_HOST_LOCAL,
     netw::display::ROLE_AUTHORITY},
    {"the server consumes a client's commands",
     SERVER_HOSTS_A_CLIENT,
     INPUT_ROWS,
     ANY_CLOSURE,
     ANY_SELECTION,
     Mode::AUTHORITY,
     NetwPredict::INPUT_SOURCE_RECEIVED,
     NetwPredict::ROLE_CONSUME,
     netw::display::ROLE_AUTHORITY},
    {"the server runs an inputless client-controlled entity",
     SERVER_HOSTS_A_CLIENT,
     INPUTLESS,
     ANY_CLOSURE,
     ANY_SELECTION,
     Mode::AUTHORITY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_HOST_LOCAL,
     netw::display::ROLE_AUTHORITY},
    {"a client predicts what it controls",
     CLIENT_STEERS,
     INPUT_ROWS,
     OPEN,
     ANY_SELECTION,
     Mode::PREDICT,
     NetwPredict::INPUT_SOURCE_LOCAL,
     NetwPredict::ROLE_PREDICT,
     netw::display::ROLE_PREDICTED},
    {"a closed controller stops predicting and keeps its local commands",
     CLIENT_STEERS,
     INPUT_ROWS,
     CLOSED,
     ANY_SELECTION,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_LOCAL,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"an inputless entity is a proxy on its controller",
     CLIENT_STEERS,
     INPUTLESS,
     OPEN,
     UNSELECTED,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"an inputless entity is active on its controller when selected",
     CLIENT_STEERS,
     INPUTLESS,
     OPEN,
     SELECTED,
     Mode::ACTIVE,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_SIMULATE,
     netw::display::ROLE_PREDICTED},
    {"a closed inputless entity on its controller",
     CLIENT_STEERS,
     INPUTLESS,
     CLOSED,
     ANY_SELECTION,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"an unselected copy of the server's entity",
     CLIENT_WATCHES_THE_SERVER,
     INPUT_ROWS,
     ANY_CLOSURE,
     UNSELECTED,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"a selected copy of the server's entity",
     CLIENT_WATCHES_THE_SERVER,
     INPUT_ROWS,
     OPEN,
     SELECTED,
     Mode::ACTIVE,
     NetwPredict::INPUT_SOURCE_PREDICTED,
     NetwPredict::ROLE_SIMULATE,
     netw::display::ROLE_PREDICTED},
    {"a closed selected copy of the server's entity stays a proxy",
     CLIENT_WATCHES_THE_SERVER,
     INPUT_ROWS,
     CLOSED,
     SELECTED,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"an unselected inputless copy of the server's entity",
     CLIENT_WATCHES_THE_SERVER,
     INPUTLESS,
     OPEN,
     UNSELECTED,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"a selected inputless copy of the server's entity runs live",
     CLIENT_WATCHES_THE_SERVER,
     INPUTLESS,
     OPEN,
     SELECTED,
     Mode::ACTIVE,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_SIMULATE,
     netw::display::ROLE_PREDICTED},
    {"a closed inputless copy of the server's entity",
     CLIENT_WATCHES_THE_SERVER,
     INPUTLESS,
     CLOSED,
     ANY_SELECTION,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"an unselected copy of another client's entity",
     CLIENT_WATCHES_A_CLIENT,
     INPUT_ROWS,
     ANY_CLOSURE,
     UNSELECTED,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"a selected copy of another client's entity",
     CLIENT_WATCHES_A_CLIENT,
     INPUT_ROWS,
     OPEN,
     SELECTED,
     Mode::ACTIVE,
     NetwPredict::INPUT_SOURCE_PREDICTED,
     NetwPredict::ROLE_SIMULATE,
     netw::display::ROLE_PREDICTED},
    {"a closed selected copy of another client's entity stays a proxy",
     CLIENT_WATCHES_A_CLIENT,
     INPUT_ROWS,
     CLOSED,
     SELECTED,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"an unselected inputless copy of another client's entity",
     CLIENT_WATCHES_A_CLIENT,
     INPUTLESS,
     OPEN,
     UNSELECTED,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
    {"a selected inputless copy of another client's entity runs live",
     CLIENT_WATCHES_A_CLIENT,
     INPUTLESS,
     OPEN,
     SELECTED,
     Mode::ACTIVE,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_SIMULATE,
     netw::display::ROLE_PREDICTED},
    {"a closed inputless copy of another client's entity",
     CLIENT_WATCHES_A_CLIENT,
     INPUTLESS,
     CLOSED,
     ANY_SELECTION,
     Mode::PROXY,
     NetwPredict::INPUT_SOURCE_NONE,
     NetwPredict::ROLE_REMOTE,
     netw::display::ROLE_REMOTE},
};

bool closure_matches(Closure p_row, Closure p_concrete) {
    switch (p_row) {
        case ANY_CLOSURE:
            return true;
        case CLOSED:
            return p_concrete != OPEN;
        default:
            return p_row == p_concrete;
    }
}

bool selection_matches(Selection p_row, Selection p_concrete) {
    return p_row == ANY_SELECTION || p_row == p_concrete;
}

bool same_peers(const Peers &p_a, const Peers &p_b) {
    return p_a.local == p_b.local && p_a.controller == p_b.controller;
}

struct Concrete {
    Peers peers;
    Inputs inputs;
    Closure closure;
    Selection selection;
};

bool covers(const Cell &p_cell, const Concrete &p_point) {
    return same_peers(p_cell.peers, p_point.peers)
        && p_cell.inputs == p_point.inputs
        && closure_matches(p_cell.closure, p_point.closure)
        && selection_matches(p_cell.selection, p_point.selection);
}

struct Resolved {
    Mode mode = Mode::NONE;
    int source = -1;
    int role = -1;
    int display_role = -1;
};

Resolved resolve_through_prediction(const Concrete &p_point) {
    const bool authority = p_point.peers.local == SESSION_AUTHORITY;
    const bool controls = p_point.peers.controller != NOBODY
        && p_point.peers.controller == p_point.peers.local;
    const bool steers_as_authority
        = authority && p_point.peers.controller == NOBODY;

    NetwPredictionEngine pool;
    const int64_t slot = pool.open();
    Ref<NetwPredictionHandle> handle;
    handle.instantiate();
    pool.declare_axes(
        slot,
        authority,
        controls || steers_as_authority,
        p_point.inputs == INPUTLESS
    );
    pool.set_fallback_latched(slot, p_point.closure == FALLBACK);
    if (p_point.closure == DELAY_CLOSED) {
        handle->set_recovery_policy(NetwPredict::RECOVERY_POLICY_DELAY_CLOSED);
    }
    Facts facts = pool.resolution_facts(slot, handle);
    facts.selection_count = p_point.selection == SELECTED ? 1 : 0;

    Resolved out;
    out.mode = netw::sim::resolve(facts);
    out.role = pool.apply_mode(slot, handle, out.mode);
    out.source = int(handle->get_input_source());
    out.display_role = netw::display::role_for_mode(out.mode, true, controls);
    return out;
}

TEST_CASE(
    "[Networked][Sim][Hosted][Law] every cell of the parity matrix resolves "
    "one mode, today's prediction role and the display role the mode derives"
) {
    const Closure CLOSURES[] = {OPEN, FALLBACK, DELAY_CLOSED};
    const Selection SELECTIONS[] = {UNSELECTED, SELECTED};
    const Inputs INPUTS[] = {INPUTLESS, INPUT_ROWS};
    int visited = 0;
    for (const Peers &peers : EVERY_PEERING) {
        for (const Inputs inputs : INPUTS) {
            for (const Closure closure : CLOSURES) {
                for (const Selection selection : SELECTIONS) {
                    const Concrete point = {peers, inputs, closure, selection};
                    const Cell *matched = nullptr;
                    int matches = 0;
                    for (const Cell &cell : PARITY) {
                        if (covers(cell, point)) {
                            matched = &cell;
                            matches += 1;
                        }
                    }
                    NETW_FORMAT_INT(local_text, peers.local);
                    NETW_FORMAT_INT(controller_text, peers.controller);
                    NETW_FORMAT_INT(inputs_text, int(inputs));
                    NETW_FORMAT_INT(closure_text, int(closure));
                    NETW_FORMAT_INT(selection_text, int(selection));
                    CAPTURE(local_text);
                    CAPTURE(controller_text);
                    CAPTURE(inputs_text);
                    CAPTURE(closure_text);
                    CAPTURE(selection_text);
                    NETW_REQUIRE_EQ(matches, 1);
                    NETW_FORMAT_TEXT(label_text, matched->label);
                    CAPTURE(label_text);
                    const Resolved read = resolve_through_prediction(point);
                    NETW_CHECK_EQ(int(read.mode), int(matched->mode));
                    NETW_CHECK_EQ(read.source, int(matched->source));
                    NETW_CHECK_EQ(read.role, int(matched->role));
                    NETW_CHECK_EQ(
                        read.display_role,
                        int(matched->display_role)
                    );
                    visited += 1;
                }
            }
        }
    }
    NETW_CHECK_EQ(visited, 60);
}

TEST_CASE(
    "[Networked][Sim][Hosted][Law] a closed selected copy with input rows "
    "is a proxy like every other closed copy"
) {
    const Concrete point
        = {CLIENT_WATCHES_THE_SERVER, INPUT_ROWS, DELAY_CLOSED, SELECTED};
    const Resolved read = resolve_through_prediction(point);
    NETW_CHECK_EQ(int(read.mode), int(Mode::PROXY));
    NETW_CHECK_EQ(read.role, int(NetwPredict::ROLE_REMOTE));
    NETW_CHECK_EQ(read.display_role, int(netw::display::ROLE_REMOTE));
}

struct AuthorRow {
    const char *label;
    Facts facts;
    Mode mode;
};

Facts declared() {
    Facts facts;
    facts.declared = true;
    return facts;
}

Facts steered_here() {
    Facts facts = declared();
    facts.controller_here = true;
    return facts;
}

Facts steered_elsewhere() {
    return declared();
}

Facts steered_by_nobody_on_the_session_authority() {
    Facts facts = declared();
    facts.controller_is_nobody = true;
    facts.session_authority_here = true;
    return facts;
}

Facts state_rows_steered_here() {
    Facts facts = steered_here();
    facts.state_rows = true;
    return facts;
}

Facts state_rows_on_the_session_authority() {
    Facts facts = declared();
    facts.state_rows = true;
    facts.session_authority_here = true;
    return facts;
}

Facts claimed_here() {
    Facts facts = declared();
    facts.pending_claim_here = true;
    return facts;
}

Facts claimed_on_a_session_authored_entity() {
    Facts facts = claimed_here();
    facts.state_rows = true;
    return facts;
}

Facts claimed_on_a_free_body() {
    Facts facts = claimed_here();
    facts.controller_is_nobody = true;
    return facts;
}

Facts active_replicas() {
    Facts facts = declared();
    facts.replicas = Replicas::ACTIVE;
    return facts;
}

Facts active_replicas_closed() {
    Facts facts = active_replicas();
    facts.fallback_latched = true;
    return facts;
}

Facts undeclared_but_steered_here() {
    Facts facts;
    facts.controller_here = true;
    facts.session_authority_here = true;
    return facts;
}

TEST_CASE(
    "[Networked][Sim][Hosted][Law] the execution author is the controller "
    "unless the session authors the entity"
) {
    const AuthorRow ROWS[] = {
        {"nothing declared is NONE whoever controls it",
         undeclared_but_steered_here(),
         Mode::NONE},
        {"a controller-authored entity runs on its controller",
         steered_here(),
         Mode::AUTHORITY},
        {"and is a proxy elsewhere", steered_elsewhere(), Mode::PROXY},
        {"controller 0 hands authorship to the session authority",
         steered_by_nobody_on_the_session_authority(),
         Mode::AUTHORITY},
        {"a state row takes authorship from the controller",
         state_rows_steered_here(),
         Mode::PROXY},
        {"and gives it to the session authority",
         state_rows_on_the_session_authority(),
         Mode::AUTHORITY},
        {"a pending claim runs a controller-authored entity",
         claimed_here(),
         Mode::AUTHORITY},
        {"a pending claim never runs a session-authored entity",
         claimed_on_a_session_authored_entity(),
         Mode::PROXY},
        {"a pending claim runs a free body the session was authoring",
         claimed_on_a_free_body(),
         Mode::AUTHORITY},
        {"active replicas run every copy", active_replicas(), Mode::ACTIVE},
        {"a closed active replica is a proxy",
         active_replicas_closed(),
         Mode::PROXY},
    };
    for (const AuthorRow &row : ROWS) {
        NETW_FORMAT_TEXT(label_text, row.label);
        CAPTURE(label_text);
        NETW_CHECK_EQ(int(netw::sim::resolve(row.facts)), int(row.mode));
    }
}

TEST_CASE(
    "[Networked][Sim][Hosted][Law] a session authority change names every "
    "row whose mode it changes and no other"
) {
    RID_Owner<int> owner;
    Rows rows;
    const RID steered = owner.make_rid(0);
    const RID watched = owner.make_rid(1);
    const RID controller_authored = owner.make_rid(2);

    Facts server_body = declared();
    server_body.predicted = true;
    server_body.input_rows = true;
    server_body.session_authority_here = true;
    NETW_CHECK_EQ(
        int(rows.resolve(steered, server_body)),
        int(Mode::AUTHORITY)
    );

    Facts client_copy = server_body;
    client_copy.session_authority_here = false;
    NETW_CHECK_EQ(int(rows.resolve(watched, client_copy)), int(Mode::PROXY));

    Facts driven = steered_here();
    driven.session_authority_here = true;
    NETW_CHECK_EQ(
        int(rows.resolve(controller_authored, driven)),
        int(Mode::AUTHORITY)
    );

    const LocalVector<RID> moved = rows.follow_session_authority(false);
    NETW_REQUIRE_EQ(moved.size(), 1);
    CHECK(moved[0] == steered);

    const LocalVector<RID> again = rows.follow_session_authority(false);
    NETW_CHECK_EQ(again.size(), 0);

    owner.free(steered);
    owner.free(watched);
    owner.free(controller_authored);
}

} // namespace TestNetwSimResolveLaws
