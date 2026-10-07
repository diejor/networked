#include "support/fault_log.h"
#include "support/frame_drive.h"
#include "support/lan_directory_probe.h"
#include "support/netw_cells.h"
#include "support/netw_test.h"

#include <memory>
#include <vector>

#include "godot/class_db.hpp"
#include "godot/time.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/endpoint.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/lan_lobby_directory.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/session.hpp"

using namespace godot;

namespace NetwTests {

namespace {

using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

constexpr int GAMEPLAY_PORT_START = 30700;
constexpr int GAMEPLAY_PORT_END = 30800;
constexpr int CLOSED_PORT = 30899;
constexpr int DISCOVERY_PORT = 30660;
constexpr int DISCOVERY_PORT_COUNT = 3;
constexpr double FRAME = 0.05;
constexpr double LIST_INTERVAL = 1.0;
constexpr int STEP_FRAME_BUDGET = 160;
const int ROW_GONE_FRAMES
    = int((2.0 * LIST_INTERVAL + netw::LANLobbyDirectory::BROWSE_WINDOW)
          / FRAME)
    + 2;
constexpr int SIDES = 3;
constexpr int HOST = 0;
constexpr int A = 1;
constexpr int B = 2;

enum Act {
    ACT_HOST,
    ACT_JOIN,
    ACT_JOIN_DIRECT,
    ACT_JOIN_BLANK,
    ACT_CLIENT_LEAVE,
    ACT_HOST_LEAVE,
    ACT_HOST_CLOSE_PEER,
    ACT_KICK,
    ACT_BROWSING_OFF,
    ACT_BROWSING_ON,
    ACT_JOIN_CLOSED_PORT,
    ACT_CANCEL,
    ACT_ADD_ROW,
};

const char *act_name(Act p_act) {
    switch (p_act) {
        case ACT_HOST:
            return "host";
        case ACT_JOIN:
            return "join";
        case ACT_JOIN_DIRECT:
            return "join_direct";
        case ACT_JOIN_BLANK:
            return "join_blank";
        case ACT_CLIENT_LEAVE:
            return "client_leave";
        case ACT_HOST_LEAVE:
            return "host_leave";
        case ACT_HOST_CLOSE_PEER:
            return "host_close_peer";
        case ACT_KICK:
            return "kick";
        case ACT_BROWSING_OFF:
            return "browsing_off";
        case ACT_BROWSING_ON:
            return "browsing_on";
        case ACT_JOIN_CLOSED_PORT:
            return "join_closed_port";
        case ACT_CANCEL:
            return "cancel";
        case ACT_ADD_ROW:
            return "add_row";
    }
    return "?";
}

const char *side_name(int p_side) {
    return p_side == HOST ? "H" : (p_side == A ? "A" : "B");
}

struct Step {
    Act act;
    int side;
};

struct ScenarioDecl {
    const char *label;
    std::vector<Step> steps;
};

const std::vector<ScenarioDecl> &scenario_decls() {
    static const std::vector<ScenarioDecl> decls = {
        {"S1", {{ACT_HOST, HOST}, {ACT_JOIN, A}, {ACT_CLIENT_LEAVE, A}}},
        {"S2", {{ACT_HOST, HOST}, {ACT_JOIN, A}, {ACT_HOST_LEAVE, HOST}}},
        {"S3",
         {{ACT_HOST, HOST},
          {ACT_JOIN, A},
          {ACT_HOST_CLOSE_PEER, HOST},
          {ACT_HOST, HOST},
          {ACT_JOIN, A}}},
        {"S4", {{ACT_HOST, HOST}, {ACT_JOIN, A}, {ACT_KICK, A}}},
        {"S5",
         {{ACT_HOST, HOST},
          {ACT_JOIN, A},
          {ACT_JOIN, B},
          {ACT_CLIENT_LEAVE, A}}},
        {"S6",
         {{ACT_HOST, HOST},
          {ACT_JOIN, A},
          {ACT_BROWSING_OFF, A},
          {ACT_BROWSING_ON, A},
          {ACT_CLIENT_LEAVE, A}}},
        {"S7", {{ACT_JOIN_CLOSED_PORT, A}, {ACT_HOST, HOST}, {ACT_JOIN, A}}},
        {"S8",
         {{ACT_HOST, HOST}, {ACT_JOIN_DIRECT, A}, {ACT_HOST_LEAVE, HOST}}},
        {"S9", {{ACT_HOST, HOST}, {ACT_CANCEL, A}, {ACT_JOIN, A}}},
        {"S10",
         {{ACT_HOST, HOST},
          {ACT_JOIN, A},
          {ACT_JOIN, B},
          {ACT_HOST_CLOSE_PEER, HOST}}},
        {"S11",
         {{ACT_HOST, HOST},
          {ACT_JOIN, A},
          {ACT_CLIENT_LEAVE, A},
          {ACT_JOIN, A},
          {ACT_HOST_LEAVE, HOST}}},
        {"S12",
         {{ACT_HOST, HOST}, {ACT_JOIN_BLANK, A}, {ACT_CLIENT_LEAVE, A}}},
        {"S13",
         {{ACT_HOST, HOST}, {ACT_JOIN_BLANK, A}, {ACT_HOST_LEAVE, HOST}}},
        {"S14", {{ACT_HOST, HOST}, {ACT_ADD_ROW, A}, {ACT_HOST_LEAVE, HOST}}},
    };
    return decls;
}

struct RowSeen {
    String peer_class;
    String address;
    String display_name;
    String source;
    int status = -1;
    int64_t latency_ms = -1;
    bool listed = false;
};

struct SideRecord {
    int state = -1;
    String join_address;
    bool server_role = false;
    bool browsing = false;
    int failed = 0;
    std::vector<RowSeen> rows;
    std::vector<RowSeen> shown;
};

enum Audit {
    AUDIT_INCOMPLETE,
    AUDIT_SHARED_ADDRESS,
    AUDIT_UNLISTED_SHOWN,
    AUDIT_COUNT,
};

struct StepRecord {
    Step step = {ACT_HOST, HOST};
    bool acted = false;
    bool settled = false;
    int frames = 0;
    int states_before[SIDES] = {-1, -1, -1};
    String hosted_address;
    SideRecord sides[SIDES];
    PackedStringArray errors;
    PackedStringArray warnings;
    int audits[AUDIT_COUNT] = {0, 0, 0};
    String first_audit[AUDIT_COUNT];
};

struct ScenarioRecord {
    String label;
    bool driven = false;
    PackedStringArray open_errors;
    PackedStringArray close_errors;
    std::vector<StepRecord> steps;
};

std::vector<ScenarioRecord> &matrix_records() {
    static std::vector<ScenarioRecord> records;
    return records;
}

struct Browser {
    std::vector<Ref<netw::NetwEndpoint>> shown;
    int failed = 0;
    bool list_visible = true;
    Ref<netw::NetwConnection> connection;
};

Browser &browser(int64_t p_side) {
    static Browser browsers[SIDES];
    return browsers[p_side];
}

void apply_browsing(int64_t p_side) {
    Browser &held = browser(p_side);
    if (held.connection.is_null()) {
        return;
    }
    const bool online
        = held.connection->get_state() == netw::NetwConnection::STATE_ONLINE;
    held.connection->set_browsing(held.list_visible && !online);
}

void on_endpoint_added(const Variant &p_endpoint, int64_t p_side) {
    const Ref<netw::NetwEndpoint> held = p_endpoint;
    browser(p_side).shown.push_back(held);
}

void on_endpoint_removed(const Variant &p_endpoint, int64_t p_side) {
    const Ref<netw::NetwEndpoint> held = p_endpoint;
    std::vector<Ref<netw::NetwEndpoint>> &shown = browser(p_side).shown;
    for (size_t at = 0; at < shown.size(); at++) {
        if (shown[at] == held) {
            shown.erase(shown.begin() + at);
            return;
        }
    }
}

void on_state_changed(int64_t, int64_t p_side) {
    apply_browsing(p_side);
}

void on_failed(int64_t, const String &, int64_t p_side) {
    browser(p_side).failed += 1;
}

RowSeen seen(const Ref<netw::NetwEndpoint> &p_endpoint) {
    RowSeen out;
    out.latency_ms = p_endpoint->get_info()->get_latency_ms();
    out.peer_class = String(p_endpoint->get_peer_class());
    out.address = p_endpoint->get_address();
    out.display_name = p_endpoint->get_display_name();
    out.source = p_endpoint->get_source();
    out.status = int(p_endpoint->get_status());
    out.listed = p_endpoint->get_is_listed();
    return out;
}

String describe(const RowSeen &p_row) {
    return vformat(
        "[%s|%s|%s|%s|%d|%s]",
        p_row.peer_class,
        p_row.address,
        p_row.display_name,
        p_row.source,
        p_row.status,
        p_row.listed ? "listed" : "unlisted"
    );
}

struct Side {
    Ref<netw::NetwMultiplayer> session;
    Ref<netw::NetwConnection> connection;
    netw::LANLobbyDirectory *directory = nullptr;
    int joined_id = 0;
};

class ConnectLifecycleMatrix final : public netw_test::FrameScenario {
    enum Phase {
        PHASE_OPEN,
        PHASE_STEP,
        PHASE_CLOSE,
        PHASE_DONE,
    };

    Phase phase = PHASE_OPEN;
    size_t scenario_at = 0;
    size_t step_at = 0;
    int next_port = GAMEPLAY_PORT_START;
    int hosted_port = 0;
    bool cancelled = false;
    int failed_at_act = 0;
    Side sides[SIDES];
    std::unique_ptr<netw_test::FaultWatch> faults;
    int64_t errors_mark = 0;
    int64_t warnings_mark = 0;

    String hosted_address() const {
        return hosted_port > 0 ? String("127.0.0.1:") + itos(hosted_port)
                               : String();
    }

    String hosted_alias() const {
        return String("localhost:") + itos(hosted_port);
    }

    ScenarioRecord &record() {
        return matrix_records().back();
    }

    StepRecord &current() {
        return record().steps[step_at];
    }

    const Step &step() const {
        return scenario_decls()[scenario_at].steps[step_at];
    }

    int state_of(int p_side) const {
        return int(sides[p_side].connection->get_state());
    }

    void mark_faults() {
        errors_mark = faults->error_lines().size();
        warnings_mark = faults->warning_lines().size();
    }

    PackedStringArray errors_since_mark() const {
        const PackedStringArray all = faults->error_lines();
        return all.slice(errors_mark);
    }

    PackedStringArray warnings_since_mark() const {
        const PackedStringArray all = faults->warning_lines();
        return all.slice(warnings_mark);
    }

    int pick_port() {
        for (; next_port < GAMEPLAY_PORT_END; next_port++) {
            const Ref<MultiplayerPeer> trial(
                ClassDB::instantiate(StringName("ENetMultiplayerPeer"))
            );
            if (trial.is_null()) {
                return 0;
            }
            const Error bound = Error(
                int(trial->call(StringName("create_server"), next_port))
            );
            if (bound == OK) {
                trial->close();
                return next_port++;
            }
        }
        return 0;
    }

    void open_world() {
        matrix_records().push_back(ScenarioRecord());
        record().label = scenario_decls()[scenario_at].label;
        hosted_port = 0;
        mark_faults();
        for (int at = 0; at < SIDES; at++) {
            Side &side = sides[at];
            side.session.instantiate();
            side.connection = side.session->get_connection();
            side.directory = memnew(netw::LANLobbyDirectory);
            side.directory->set_port(DISCOVERY_PORT);
            side.directory->set_port_count(DISCOVERY_PORT_COUNT);
            side.directory->set_list_interval(LIST_INTERVAL);
            netw::LANLobbyDirectoryProbe::query_loopback(side.directory);
            side.directory->service_entered(side.session.ptr());
            side.session->service_register(side.directory, nullptr);
            Browser &held = browser(at);
            held = Browser();
            held.connection = side.connection;
            side.connection->connect(
                StringName("endpoint_added"),
                callable_mp_static(&on_endpoint_added).bind(int64_t(at))
            );
            side.connection->connect(
                StringName("endpoint_removed"),
                callable_mp_static(&on_endpoint_removed).bind(int64_t(at))
            );
            side.connection->connect(
                StringName("state_changed"),
                callable_mp_static(&on_state_changed).bind(int64_t(at))
            );
            side.connection->connect(
                StringName("failed"),
                callable_mp_static(&on_failed).bind(int64_t(at))
            );
            apply_browsing(at);
        }
        record().open_errors = errors_since_mark();
    }

    void close_world() {
        mark_faults();
        for (int at = SIDES - 1; at >= 0; at--) {
            Side &side = sides[at];
            if (side.session.is_null()) {
                continue;
            }
            side.connection->set_browsing(false);
            side.session->service_unregister(side.directory, nullptr);
            side.directory->service_exiting(side.session.ptr());
            memdelete(side.directory);
            side.directory = nullptr;
            side.session->NETW_API_VIRTUAL(set_multiplayer_peer)(
                Ref<MultiplayerPeer>()
            );
            side.session->embed_dispose();
            browser(at) = Browser();
            side.connection.unref();
            side.session.unref();
            side.joined_id = 0;
        }
        record().close_errors = errors_since_mark();
        record().driven = true;
    }

    void pump() {
        for (int at = 0; at < SIDES; at++) {
            Side &side = sides[at];
            if (side.session.is_null()) {
                continue;
            }
            side.session->NETW_API_VIRTUAL(poll)();
            side.session->connect_plane().on_poll(FRAME);
            netw::LANLobbyDirectoryProbe::tick(side.directory, FRAME);
        }
    }

    Ref<netw::NetwEndpoint> lan_row(int p_side) const {
        const TypedArray<netw::NetwEndpoint> rows
            = sides[p_side].connection->endpoints();
        for (int64_t at = 0; at < rows.size(); at++) {
            const Ref<netw::NetwEndpoint> row = rows[at];
            if (row.is_valid() && row->get_source() == String("LAN")
                && row->get_address() == hosted_address()) {
                return row;
            }
        }
        return Ref<netw::NetwEndpoint>();
    }

    bool added_row_online(int p_side) const {
        const TypedArray<netw::NetwEndpoint> rows
            = sides[p_side].connection->endpoints();
        for (int64_t at = 0; at < rows.size(); at++) {
            const Ref<netw::NetwEndpoint> row = rows[at];
            if (row->get_source().is_empty()
                && row->get_address() == hosted_alias()
                && row->get_status() == netw::NetwEndpoint::STATUS_ONLINE) {
                return true;
            }
        }
        return false;
    }

    bool lists_only_hosted(int p_side, bool p_hosting) const {
        const TypedArray<netw::NetwEndpoint> rows
            = sides[p_side].connection->endpoints();
        int listed = 0;
        int hosted = 0;
        for (int64_t at = 0; at < rows.size(); at++) {
            const Ref<netw::NetwEndpoint> row = rows[at];
            if (row->get_source() != String("LAN")) {
                continue;
            }
            listed += 1;
            hosted += row->get_address() == hosted_address() ? 1 : 0;
        }
        return p_hosting ? listed == 1 && hosted == 1 : listed == 0;
    }

    bool seats(int p_peer) const {
        if (p_peer <= 1) {
            return false;
        }
        const PackedInt32Array seated
            = sides[HOST].session->NETW_API_VIRTUAL(get_peer_ids)();
        return seated.has(p_peer);
    }

    void prepare(int p_side) {
        sides[p_side].session->session_prepare_join(
            StringName(side_name(p_side)),
            Array()
        );
    }

    Ref<netw::NetwEndpoint> added_row(int p_side, const String &p_address) {
        return sides[p_side].connection->add(
            StringName("ENetMultiplayerPeer"),
            p_address,
            String()
        );
    }

    bool act() {
        const Step &now = step();
        Side &side = sides[now.side];
        failed_at_act = browser(now.side).failed;
        switch (now.act) {
            case ACT_HOST: {
                if (hosted_port == 0) {
                    hosted_port = pick_port();
                }
                prepare(now.side);
                Dictionary settings;
                settings["port"] = hosted_port;
                side.connection->host(
                    StringName("ENetMultiplayerPeer"),
                    settings
                );
                return true;
            }
            case ACT_JOIN: {
                const Ref<netw::NetwEndpoint> row = lan_row(now.side);
                if (row.is_null()) {
                    return false;
                }
                prepare(now.side);
                side.connection->join(row, Dictionary());
                return true;
            }
            case ACT_JOIN_DIRECT: {
                prepare(now.side);
                side.connection->join(
                    added_row(now.side, hosted_address()),
                    Dictionary()
                );
                return true;
            }
            case ACT_ADD_ROW: {
                added_row(now.side, hosted_alias());
                return true;
            }
            case ACT_JOIN_BLANK: {
                prepare(now.side);
                Dictionary settings;
                settings["port"] = hosted_port;
                side.connection->join(added_row(now.side, String()), settings);
                return true;
            }
            case ACT_CLIENT_LEAVE:
            case ACT_HOST_LEAVE: {
                side.joined_id
                    = side.session->NETW_API_VIRTUAL(get_unique_id)();
                side.session->session_leave();
                return true;
            }
            case ACT_HOST_CLOSE_PEER: {
                side.session->NETW_API_VIRTUAL(set_multiplayer_peer)(
                    Ref<MultiplayerPeer>()
                );
                return true;
            }
            case ACT_KICK: {
                side.joined_id
                    = side.session->NETW_API_VIRTUAL(get_unique_id)();
                const Ref<netw::NetwSession> door
                    = sides[HOST].session->get_session();
                const Ref<netw::NetwPlayer> player
                    = door->player_of(side.joined_id);
                if (player.is_null()) {
                    return false;
                }
                door->kick(player, String("matrix"));
                return true;
            }
            case ACT_BROWSING_OFF:
            case ACT_BROWSING_ON: {
                browser(now.side).list_visible = now.act == ACT_BROWSING_ON;
                apply_browsing(now.side);
                return true;
            }
            case ACT_JOIN_CLOSED_PORT:
            case ACT_CANCEL: {
                cancelled = false;
                prepare(now.side);
                const String closed = "127.0.0.1:" + itos(CLOSED_PORT);
                const Ref<netw::NetwEndpoint> row = added_row(now.side, closed);
                side.connection->join(row, Dictionary());
                return true;
            }
        }
        return true;
    }

    bool views_settled() const {
        const bool hosting
            = state_of(HOST) == netw::NetwConnection::STATE_ONLINE;
        for (int at = 0; at < SIDES; at++) {
            if (at == HOST || !sides[at].connection->is_browsing()) {
                continue;
            }
            if (!lists_only_hosted(at, hosting)) {
                return false;
            }
        }
        return true;
    }

    bool step_settled() {
        const Step &now = step();
        const Side &side = sides[now.side];
        const int online = netw::NetwConnection::STATE_ONLINE;
        const int offline = netw::NetwConnection::STATE_OFFLINE;
        switch (now.act) {
            case ACT_HOST:
                return state_of(HOST) == online
                    && !side.connection->get_join_address().is_empty();
            case ACT_JOIN:
            case ACT_JOIN_DIRECT:
            case ACT_JOIN_BLANK:
                return state_of(now.side) == online
                    && seats(side.session->NETW_API_VIRTUAL(get_unique_id)());
            case ACT_CLIENT_LEAVE:
            case ACT_KICK:
                return state_of(now.side) == offline && !seats(side.joined_id);
            case ACT_HOST_LEAVE:
            case ACT_HOST_CLOSE_PEER:
                return state_of(HOST) == offline && state_of(A) == offline
                    && state_of(B) == offline;
            case ACT_BROWSING_OFF:
            case ACT_BROWSING_ON: {
                const bool wanted = browser(now.side).list_visible
                    && state_of(now.side) != online;
                return side.connection->is_browsing() == wanted;
            }
            case ACT_ADD_ROW:
                return added_row_online(now.side);
            case ACT_JOIN_CLOSED_PORT:
                return state_of(now.side) == offline
                    && browser(now.side).failed > failed_at_act;
            case ACT_CANCEL: {
                const int connecting = netw::NetwConnection::STATE_CONNECTING;
                if (!cancelled && state_of(now.side) == connecting) {
                    side.connection->cancel();
                    cancelled = true;
                }
                return cancelled && state_of(now.side) == offline;
            }
        }
        return true;
    }

    void note(StepRecord &r_step, Audit p_audit, const String &p_what) {
        if (r_step.audits[p_audit] == 0) {
            r_step.first_audit[p_audit] = p_what;
        }
        r_step.audits[p_audit] += 1;
    }

    void audit(StepRecord &r_step) {
        for (int at = 0; at < SIDES; at++) {
            const TypedArray<netw::NetwEndpoint> rows
                = sides[at].connection->endpoints();
            const String who = String(side_name(at)) + " ";
            for (int64_t row = 0; row < rows.size(); row++) {
                const Ref<netw::NetwEndpoint> held = rows[row];
                if (held->get_peer_class() == StringName()
                    || held->get_address().is_empty()) {
                    note(r_step, AUDIT_INCOMPLETE, who + describe(seen(held)));
                }
                for (int64_t other = row + 1; other < rows.size(); other++) {
                    const Ref<netw::NetwEndpoint> twin = rows[other];
                    if (twin->get_source() == held->get_source()
                        && twin->get_address() == held->get_address()) {
                        note(
                            r_step,
                            AUDIT_SHARED_ADDRESS,
                            who + describe(seen(held))
                        );
                    }
                }
            }
            const std::vector<Ref<netw::NetwEndpoint>> &shown
                = browser(at).shown;
            for (const Ref<netw::NetwEndpoint> &held : shown) {
                if (!held->get_is_listed()) {
                    note(
                        r_step,
                        AUDIT_UNLISTED_SHOWN,
                        who + describe(seen(held))
                    );
                }
            }
        }
    }

    void settle_record(StepRecord &r_step) {
        for (int at = 0; at < SIDES; at++) {
            const Side &side = sides[at];
            SideRecord &out = r_step.sides[at];
            out.state = state_of(at);
            out.join_address = side.connection->get_join_address();
            out.server_role = side.session->has_server_role();
            out.browsing = side.connection->is_browsing();
            out.failed = browser(at).failed;
            const TypedArray<netw::NetwEndpoint> rows
                = side.connection->endpoints();
            for (int64_t row = 0; row < rows.size(); row++) {
                out.rows.push_back(seen(rows[row]));
            }
            for (const Ref<netw::NetwEndpoint> &held : browser(at).shown) {
                out.shown.push_back(seen(held));
            }
        }
        r_step.errors = errors_since_mark();
        r_step.warnings = warnings_since_mark();
        mark_faults();
    }

    void begin_step() {
        StepRecord opened;
        opened.step = step();
        for (int at = 0; at < SIDES; at++) {
            opened.states_before[at] = state_of(at);
        }
        record().steps.push_back(opened);
        mark_faults();
    }

public:
    bool advance() override {
        if (phase == PHASE_DONE) {
            return false;
        }
        if (!faults) {
            faults = std::make_unique<netw_test::FaultWatch>();
        }
        pump();
        switch (phase) {
            case PHASE_OPEN: {
                open_world();
                step_at = 0;
                begin_step();
                phase = PHASE_STEP;
                return true;
            }
            case PHASE_STEP: {
                StepRecord &now = current();
                now.frames += 1;
                if (!now.acted) {
                    now.acted = act();
                    now.hosted_address = hosted_address();
                }
                audit(now);
                const bool settled
                    = now.acted && step_settled() && views_settled();
                if (!settled && now.frames < STEP_FRAME_BUDGET) {
                    return true;
                }
                now.settled = settled;
                settle_record(now);
                step_at += 1;
                if (!settled
                    || step_at >= scenario_decls()[scenario_at].steps.size()) {
                    phase = PHASE_CLOSE;
                } else {
                    begin_step();
                }
                return true;
            }
            case PHASE_CLOSE: {
                close_world();
                scenario_at += 1;
                if (scenario_at < scenario_decls().size()) {
                    phase = PHASE_OPEN;
                    return true;
                }
                faults.reset();
                phase = PHASE_DONE;
                return false;
            }
            case PHASE_DONE:
                return false;
        }
        return false;
    }
};

NETW_FRAME_SCENARIO(ConnectLifecycleMatrix, connect_lifecycle_matrix);

bool hosting_stops(Act p_act) {
    return p_act == ACT_HOST_LEAVE || p_act == ACT_HOST_CLOSE_PEER;
}

String step_label(const StepRecord &p_step, size_t p_at) {
    return vformat(
        "step %d %s %s",
        int64_t(p_at),
        act_name(p_step.step.act),
        side_name(p_step.step.side)
    );
}

LawVerdict broken_at(
    const StepRecord &p_step,
    size_t p_at,
    const String &p_detail
) {
    const CharString text = (step_label(p_step, p_at) + " " + p_detail).utf8();
    return law_broken("%s", text.get_data());
}

LawVerdict law_settles(const ScenarioRecord &p_run) {
    if (!p_run.driven) {
        return law_broken("the scenario never closed its world");
    }
    const size_t declared = [&]() {
        for (const ScenarioDecl &decl : scenario_decls()) {
            if (p_run.label == String(decl.label)) {
                return decl.steps.size();
            }
        }
        return size_t(0);
    }();
    for (size_t at = 0; at < p_run.steps.size(); at++) {
        const StepRecord &now = p_run.steps[at];
        if (!now.acted) {
            return broken_at(now, at, "never acted");
        }
        if (!now.settled) {
            return broken_at(
                now,
                at,
                vformat("unsettled after %d", now.frames)
            );
        }
    }
    if (p_run.steps.size() != declared) {
        return law_broken(
            "ran %d of %d steps",
            int(p_run.steps.size()),
            int(declared)
        );
    }
    return law_held();
}

LawVerdict law_rows_complete(const ScenarioRecord &p_run) {
    for (size_t at = 0; at < p_run.steps.size(); at++) {
        const StepRecord &now = p_run.steps[at];
        if (now.audits[AUDIT_INCOMPLETE] > 0) {
            return broken_at(now, at, now.first_audit[AUDIT_INCOMPLETE]);
        }
    }
    return law_held();
}

LawVerdict law_no_engine_error(const ScenarioRecord &p_run) {
    if (!p_run.open_errors.is_empty()) {
        const CharString text = p_run.open_errors[0].utf8();
        return law_broken("opening: %s", text.get_data());
    }
    for (size_t at = 0; at < p_run.steps.size(); at++) {
        const StepRecord &now = p_run.steps[at];
        if (!now.errors.is_empty()) {
            return broken_at(
                now,
                at,
                vformat("%d errors, first %s", now.errors.size(), now.errors[0])
            );
        }
        const int64_t allowed = now.step.act == ACT_JOIN_CLOSED_PORT ? 1 : 0;
        if (now.warnings.size() > allowed) {
            return broken_at(
                now,
                at,
                vformat(
                    "%d warnings, first %s",
                    now.warnings.size(),
                    now.warnings[0]
                )
            );
        }
    }
    if (!p_run.close_errors.is_empty()) {
        const CharString text = p_run.close_errors[0].utf8();
        return law_broken("closing: %s", text.get_data());
    }
    return law_held();
}

int expected_state(const StepRecord &p_step, int p_side) {
    const int online = netw::NetwConnection::STATE_ONLINE;
    const int offline = netw::NetwConnection::STATE_OFFLINE;
    const int actor = p_step.step.side;
    switch (p_step.step.act) {
        case ACT_HOST:
            return p_side == HOST ? online : -1;
        case ACT_JOIN:
        case ACT_JOIN_DIRECT:
        case ACT_JOIN_BLANK:
            return p_side == actor || p_side == HOST ? online : -1;
        case ACT_CLIENT_LEAVE:
        case ACT_KICK:
            return p_side == actor ? offline : (p_side == HOST ? online : -1);
        case ACT_HOST_LEAVE:
        case ACT_HOST_CLOSE_PEER:
            return offline;
        case ACT_JOIN_CLOSED_PORT:
        case ACT_CANCEL:
            return p_side == actor ? offline : -1;
        case ACT_ADD_ROW:
            return p_side == HOST ? online : -1;
        case ACT_BROWSING_OFF:
        case ACT_BROWSING_ON:
            return p_step.states_before[p_side];
    }
    return -1;
}

LawVerdict law_state_follows(const ScenarioRecord &p_run) {
    for (size_t at = 0; at < p_run.steps.size(); at++) {
        const StepRecord &now = p_run.steps[at];
        for (int side = 0; side < SIDES; side++) {
            const int wanted = expected_state(now, side);
            if (wanted >= 0 && now.sides[side].state != wanted) {
                return broken_at(
                    now,
                    at,
                    vformat(
                        "%s state %d, wanted %d",
                        side_name(side),
                        now.sides[side].state,
                        wanted
                    )
                );
            }
        }
    }
    return law_held();
}

LawVerdict law_join_address(const ScenarioRecord &p_run) {
    for (size_t at = 0; at < p_run.steps.size(); at++) {
        const StepRecord &now = p_run.steps[at];
        for (int side = 0; side < SIDES; side++) {
            const SideRecord &held = now.sides[side];
            if (held.state == netw::NetwConnection::STATE_OFFLINE
                && !held.join_address.is_empty()) {
                return broken_at(
                    now,
                    at,
                    vformat(
                        "%s offline at '%s'",
                        side_name(side),
                        held.join_address
                    )
                );
            }
            if (held.state == netw::NetwConnection::STATE_ONLINE
                && held.server_role && held.join_address.is_empty()) {
                return broken_at(
                    now,
                    at,
                    vformat("%s hosts with no address", side_name(side))
                );
            }
        }
    }
    return law_held();
}

LawVerdict law_one_row_per_address(const ScenarioRecord &p_run) {
    for (size_t at = 0; at < p_run.steps.size(); at++) {
        const StepRecord &now = p_run.steps[at];
        if (now.audits[AUDIT_SHARED_ADDRESS] > 0) {
            return broken_at(now, at, now.first_audit[AUDIT_SHARED_ADDRESS]);
        }
    }
    return law_held();
}

LawVerdict law_stopped_host_leaves(const ScenarioRecord &p_run) {
    for (size_t at = 0; at < p_run.steps.size(); at++) {
        const StepRecord &now = p_run.steps[at];
        if (!hosting_stops(now.step.act)) {
            continue;
        }
        for (int side = 0; side < SIDES; side++) {
            for (const RowSeen &row : now.sides[side].rows) {
                if (row.source == String("LAN")
                    && row.status == int(netw::NetwEndpoint::STATUS_ONLINE)) {
                    return broken_at(
                        now,
                        at,
                        String(side_name(side)) + " still lists "
                            + describe(row)
                    );
                }
            }
        }
        if (now.settled && now.frames > ROW_GONE_FRAMES) {
            return broken_at(
                now,
                at,
                vformat("rows left after %d frames", now.frames)
            );
        }
    }
    return law_held();
}

LawVerdict law_shown_rows_listed(const ScenarioRecord &p_run) {
    for (size_t at = 0; at < p_run.steps.size(); at++) {
        const StepRecord &now = p_run.steps[at];
        if (now.audits[AUDIT_UNLISTED_SHOWN] > 0) {
            return broken_at(now, at, now.first_audit[AUDIT_UNLISTED_SHOWN]);
        }
    }
    return law_held();
}

LawVerdict law_added_online_rows_report_ping(const ScenarioRecord &p_run) {
    for (size_t at = 0; at < p_run.steps.size(); at++) {
        const StepRecord &now = p_run.steps[at];
        for (int side = 0; side < SIDES; side++) {
            for (const RowSeen &row : now.sides[side].rows) {
                if (row.source.is_empty()
                    && row.status == int(netw::NetwEndpoint::STATUS_ONLINE)
                    && row.latency_ms < 0) {
                    return broken_at(
                        now,
                        at,
                        String(side_name(side)) + " shows no ping on "
                            + describe(row)
                    );
                }
            }
        }
    }
    return law_held();
}

typedef LawRowFor<ScenarioRecord> MatrixLaw;

const MatrixLaw V0 = {
    "V0",
    "every step acts and settles within its frame budget",
    &law_settles,
};
const MatrixLaw V1 = {
    "V1",
    "every row in endpoints has a peer class and an address",
    &law_rows_complete,
};
const MatrixLaw V2 = {
    "V2",
    "no step raises an engine error, and only a declared step warns",
    &law_no_engine_error,
};
const MatrixLaw V3 = {
    "V3",
    "the connection state follows the step on every side it reaches",
    &law_state_follows,
};
const MatrixLaw V4 = {
    "V4",
    "join_address is empty offline and set while hosting online",
    &law_join_address,
};
const MatrixLaw V5 = {
    "V5",
    "no two rows from one source share an address",
    &law_one_row_per_address,
};
const MatrixLaw V6 = {
    "V6",
    "a host that stops hosting leaves every browser within two list intervals",
    &law_stopped_host_leaves,
};
const MatrixLaw V7 = {
    "V7",
    "every row a browser shows is listed",
    &law_shown_rows_listed,
};
const MatrixLaw V8 = {
    "V8",
    "every ONLINE row added with connection.add reports a ping",
    &law_added_online_rows_report_ping,
};
const MatrixLaw LAWS[] = {V0, V1, V2, V3, V4, V5, V6, V7, V8};

} // namespace

TEST_CASE(
    "[Networked][Connect][Frame] a host and two browsers over real ENet and "
    "LAN discovery keep every connection view coherent through each "
    "lifecycle combination"
) {
    const std::vector<ScenarioRecord> &runs = matrix_records();
    NETW_REQUIRE_EQ(int(runs.size()), int(scenario_decls().size()));
    for (const ScenarioRecord &run : runs) {
        for (const MatrixLaw &law : LAWS) {
            NETW_CELL(law, run);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

} // namespace NetwTests
