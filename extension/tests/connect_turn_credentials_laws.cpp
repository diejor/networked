#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/project_settings.hpp"
#include "godot/variant.hpp"
#include "netw/connect/turn_credentials.hpp"

namespace TestNetwConnectTurnCredentials {

using namespace godot;
using netw::connect::TurnCredentials;

PackedByteArray bytes_of(const char *p_text) {
    return String(p_text).to_utf8_buffer();
}

TEST_CASE(
    "[Networked][Connect][Hosted] a credential url splits into the host the "
    "client dials and the path it requests, and the scheme stays on the host "
    "so an https url selects TLS and port 443 without either being named"
) {
    String host;
    String path;

    TurnCredentials::split_url("https://relay.example/ice", host, path);
    CHECK(host == String("https://relay.example"));
    CHECK(path == String("/ice"));

    TurnCredentials::split_url("https://relay.example", host, path);
    CHECK(host == String("https://relay.example"));
    CHECK(path == String("/"));

    TurnCredentials::split_url("https://relay.example/", host, path);
    CHECK(host == String("https://relay.example"));
    CHECK(path == String("/"));

    TurnCredentials::split_url(
        "  http://relay.example:8080/ice?token=1  ",
        host,
        path
    );
    CHECK(host == String("http://relay.example:8080"));
    CHECK(path == String("/ice?token=1"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a credential answer carries ICE servers "
    "only in iceServers and each refusal names its cause and remedy"
) {
    netw_test::FaultWatch faults;
    struct Row {
        int code;
        const char *body;
        int servers;
        netw::log::Level level;
        const char *message;
    };
    using netw::log::Level;
    const Row rows[] = {
        {200,
         "{\"iceServers\":[{\"urls\":[\"turn:relay.example\"],"
         "\"username\":\"u\",\"credential\":\"c\"}]}",
         1,
         Level::INFO,
         ""},
        {200,
         "[{\"urls\":[\"turn:relay.example\"]}]",
         0,
         Level::ERROR,
         "The TURN credential server answered without iceServers. "
         "It must answer {\"iceServers\": [...]}."},
        {200,
         "{\"iceServers\":[]}",
         0,
         Level::ERROR,
         "The TURN credential server answered without iceServers. "
         "It must answer {\"iceServers\": [...]}."},
        {200,
         "{\"iceServers\":[{},42]}",
         0,
         Level::ERROR,
         "The TURN credential server answered without iceServers. "
         "It must answer {\"iceServers\": [...]}."},
        {404,
         "missing",
         0,
         Level::ERROR,
         "The TURN credential URL answered 404. Check "
         "networked/webrtc/turn_credentials_url."},
        {403,
         "{\"error\":\"Add it to ALLOWED_ORIGINS.\"}",
         0,
         Level::ERROR,
         "The TURN credential server refused this game. "
         "Add it to ALLOWED_ORIGINS."},
        {500,
         "{\"error\":\"TURN_KEY_ID is not set.\"}",
         0,
         Level::WARN,
         "The TURN credential server answered 500. TURN_KEY_ID is not set."},
        {429,
         "{\"error\":\"Try again in a minute.\"}",
         0,
         Level::WARN,
         "The TURN credential server answered 429. Try again in a minute."},
        {200,
         "not json",
         0,
         Level::ERROR,
         "The TURN credential server answered without iceServers. "
         "It must answer {\"iceServers\": [...]}."},
    };
    for (const Row &row : rows) {
        const TurnCredentials::Answer answer
            = TurnCredentials::read_answer(row.code, bytes_of(row.body));
        const bool count_matches = answer.servers.size() == row.servers;
        const bool level_matches = answer.level == row.level;
        CHECK(count_matches);
        CHECK(level_matches);
        const bool message_matches = answer.message == String(row.message);
        CHECK(message_matches);
        if (!answer.servers.is_empty()) {
            const Dictionary server = answer.servers[0];
            const bool credentials_carried
                = String(server["credential"]) == String("c")
                && String(server["username"]) == String("u");
            CHECK(credentials_carried);
        }
    }
    NETW_CHECK_EQ(faults.errors_logged(), 0);
    NETW_CHECK_EQ(faults.warnings_logged(), 0);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a credential URL reads feature overrides "
    "so an exported build can fetch while the editor stays offline"
) {
    ProjectSettings *settings = ProjectSettings::get_singleton();
    const String key = TurnCredentials::URL_SETTING;
    const String override_key = key + String(".editor");
    const Variant saved = settings->get_setting(key, Variant());
    const Variant saved_override
        = settings->get_setting(override_key, Variant());
    settings->set_setting(override_key, "http://127.0.0.1:8787/");
    for (const bool base_present : {true, false}) {
        settings->set_setting(
            key,
            base_present ? Variant("https://export.example/") : Variant()
        );
        const bool overridden = TurnCredentials::configured_url()
            == String("http://127.0.0.1:8787/");
        CHECK(overridden);
    }
    settings->set_setting(key, saved);
    settings->set_setting(override_key, saved_override);
}

} // namespace TestNetwConnectTurnCredentials

#if defined(NETW_TIER_HOSTED)
#include "support/frame_drive.h"

#include <memory>

#include "godot/class_db.hpp"
#include "godot/time.hpp"
#include "netw/api/webrtc_signaler.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/webrtc_transport.hpp"

namespace netw::connect {

struct TurnCredentialProbe {
    static bool fetching(const WebRTCTransport &p_transport) {
        return p_transport.credentials != nullptr;
    }

    static bool built(const WebRTCTransport &p_transport) {
        return p_transport.session_core != nullptr;
    }

    static int server_count(const WebRTCTransport &p_transport) {
        return p_transport.session_core == nullptr
            ? -1
            : int(p_transport.session_core->ice_servers.size());
    }

    static void expire(WebRTCTransport &p_transport) {
        if (p_transport.credentials != nullptr) {
            p_transport.credentials->budget_msec = 1;
            p_transport.drive_credential_fetch(
                int64_t(godot::Time::get_singleton()->get_ticks_msec()) + 2
            );
        }
    }
};

} // namespace netw::connect

namespace TestNetwConnectTurnCredentials {

using netw::connect::TurnCredentialProbe;
using netw::connect::WebRTCTransport;

struct CredentialEvidence {
    bool driven = false;
    bool listening = false;
    bool waited[4] = {};
    bool built[3] = {};
    bool fetched[2] = {};
    bool fallback = false;
    bool cancelled = false;
    int requests = 0;
    int timeout_warnings = 0;
};

CredentialEvidence evidence;

class CredentialSignaler : public netw::NetwWebRTCSignaler {
public:
    Error open(const String &, int64_t) override {
        return OK;
    }
    String room_id() override {
        return "turn-credential-law";
    }
    void close() override {
    }
};

struct CredentialState {
    Ref<RefCounted> server;
    Ref<RefCounted> socket;
    Ref<netw::NetwWebRTCSignaler> signaler;
    WebRTCTransport transport;
    Dictionary settings;
    Variant saved_url;
    String request;
    int attempt = 0;
    int frame = 0;
    int cancellation_frame = 0;
    bool answered = false;
    std::unique_ptr<netw_test::FaultWatch> faults;

    CredentialState() {
        server = netw::gd::instantiate_class(StringName("TCPServer"));
        const Error bound = Error(
            int(server->call(StringName("listen"), 0, String("127.0.0.1")))
        );
        evidence.listening = bound == OK;
        if (!evidence.listening) {
            return;
        }
        const int port = int(server->call(StringName("get_local_port")));
        ProjectSettings *project = ProjectSettings::get_singleton();
        saved_url = project->get_setting(TurnCredentials::URL_SETTING);
        project->set_setting(
            TurnCredentials::URL_SETTING,
            String("http://127.0.0.1:") + itos(port) + "/credentials"
        );
        signaler = Ref<netw::NetwWebRTCSignaler>(memnew(CredentialSignaler));
        settings["signaler"] = signaler;
        settings["ice_servers"] = Array();
        start();
    }

    ~CredentialState() {
        transport.close();
        if (socket.is_valid()) {
            socket->call(StringName("disconnect_from_host"));
        }
        server->call(StringName("stop"));
        ProjectSettings::get_singleton()->set_setting(
            TurnCredentials::URL_SETTING,
            saved_url
        );
    }

    void start() {
        request = String();
        answered = false;
        socket.unref();
        transport.make_peer(
            attempt == 0 ? netw::connect::PEER_MODE_HOST
                         : netw::connect::PEER_MODE_CLIENT,
            "turn-credential-law",
            settings
        );
        evidence.waited[attempt] = TurnCredentialProbe::fetching(transport)
            && !TurnCredentialProbe::built(transport);
    }

    void serve() {
        if (socket.is_null()
            && bool(server->call(StringName("is_connection_available")))) {
            socket = server->call(StringName("take_connection"));
        }
        if (socket.is_null() || answered) {
            return;
        }
        socket->call(StringName("poll"));
        const int count = int(socket->call(StringName("get_available_bytes")));
        if (count > 0) {
            const Array read = socket->call(StringName("get_data"), count);
            request += netw::gd::utf8_string(PackedByteArray(read[1]));
        }
        if (!request.contains("\r\n\r\n")) {
            return;
        }
        evidence.requests += 1;
        answered = true;
        if (attempt >= 2) {
            return;
        }
        const String body = String("{\"iceServers\":[{\"urls\":[")
            + "\"turn:127.0.0.1:9\"],\"username\":\"" + itos(attempt)
            + "\",\"credential\":\"c\"}]}";
        const String response = String("HTTP/1.1 200 OK\r\nContent-Length: ")
            + itos(body.to_utf8_buffer().size())
            + "\r\nConnection: close\r\n\r\n" + body;
        socket->call(StringName("put_data"), response.to_utf8_buffer());
    }

    bool advance() {
        if (!evidence.listening || ++frame > 600) {
            return false;
        }
        if (!evidence.waited[attempt]) {
            return false;
        }
        if (cancellation_frame > 0) {
            transport.poll(1.0 / 60.0);
            if (frame > cancellation_frame + 3) {
                evidence.cancelled = !TurnCredentialProbe::built(transport)
                    && !TurnCredentialProbe::fetching(transport)
                    && transport.effective_ice_servers().is_empty();
                return false;
            }
            return true;
        }
        serve();
        if (answered && attempt == 2) {
            faults = std::make_unique<netw_test::FaultWatch>();
            TurnCredentialProbe::expire(transport);
            evidence.timeout_warnings = int(
                faults->warnings_containing("did not answer within 5 seconds")
            );
            faults.reset();
        } else if (answered && attempt == 3) {
            transport.close();
            cancellation_frame = frame;
            return true;
        } else {
            transport.poll(1.0 / 60.0);
        }
        if (TurnCredentialProbe::fetching(transport)) {
            return true;
        }
        if (attempt < 3) {
            evidence.built[attempt] = TurnCredentialProbe::built(transport);
        }
        if (attempt < 2) {
            const Array servers = transport.effective_ice_servers();
            if (!servers.is_empty()) {
                const Dictionary server_entry = servers[0];
                evidence.fetched[attempt]
                    = String(server_entry["username"]) == itos(attempt)
                    && TurnCredentialProbe::server_count(transport) == 1;
            }
        } else {
            evidence.fallback = transport.effective_ice_servers().is_empty()
                && TurnCredentialProbe::server_count(transport) == 0;
        }
        transport.close();
        ++attempt;
        start();
        return true;
    }
};

struct CredentialScenario : netw_test::FrameScenario {
    std::unique_ptr<CredentialState> state;

    bool advance() override {
        if (state == nullptr) {
            state = std::make_unique<CredentialState>();
        }
        if (state->advance()) {
            return true;
        }
        state.reset();
        evidence.driven = true;
        return false;
    }
};

NETW_FRAME_SCENARIO(CredentialScenario, credential_scenario);

TEST_CASE(
    "[Networked][Connect][Frame] each WebRTC host or join fetches before "
    "building and a timeout or cancellation keeps no fetched credentials"
) {
    CHECK(evidence.driven);
    CHECK(evidence.listening);
    for (const bool waited : evidence.waited) {
        CHECK(waited);
    }
    for (const bool built : evidence.built) {
        CHECK(built);
    }
    for (const bool fetched : evidence.fetched) {
        CHECK(fetched);
    }
    CHECK(evidence.fallback);
    CHECK(evidence.cancelled);
    NETW_CHECK_EQ(evidence.requests, 4);
    NETW_CHECK_EQ(evidence.timeout_warnings, 1);
}

} // namespace TestNetwConnectTurnCredentials
#endif
