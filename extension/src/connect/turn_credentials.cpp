#include "netw/connect/turn_credentials.hpp"

#include "godot/http.hpp"
#include "godot/json.hpp"
#include "godot/project_settings.hpp"
#include "godot/variant.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw::connect {

const char *TurnCredentials::URL_SETTING
    = "networked/webrtc/turn_credentials_url";

String TurnCredentials::configured_url() {
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return String();
    }
    if (!settings->has_setting(URL_SETTING)) {
        settings->set_setting(URL_SETTING, String());
    }
    return String(settings->get_setting_with_override(URL_SETTING))
        .strip_edges();
}

void TurnCredentials::split_url(
    const String &p_url,
    String &r_host,
    String &r_path
) {
    const String url = p_url.strip_edges();
    int scheme_end = 0;
    const int marker = int(url.find("://"));
    if (marker >= 0) {
        scheme_end = marker + 3;
    }
    const int slash = int(url.find("/", scheme_end));
    if (slash < 0) {
        r_host = url;
        r_path = "/";
        return;
    }
    r_host = url.substr(0, slash);
    r_path = url.substr(slash);
    if (r_path.is_empty()) {
        r_path = "/";
    }
}

TurnCredentials::Answer TurnCredentials::read_answer(
    int p_code,
    const PackedByteArray &p_body
) {
    Answer answer;
    Ref<JSON> parser;
    parser.instantiate();
    Variant parsed;
    if (parser->parse(gd::utf8_string(p_body)) == OK) {
        parsed = parser->get_data();
    }
    Dictionary content;
    if (parsed.get_type() == Variant::DICTIONARY) {
        content = parsed;
    }
    if (p_code == 200) {
        const Variant listed = content.get("iceServers", Variant());
        if (listed.get_type() == Variant::ARRAY) {
            const Array servers = listed;
            for (int64_t at = 0; at < servers.size(); at++) {
                if (servers[at].get_type() != Variant::DICTIONARY) {
                    continue;
                }
                const Dictionary server = servers[at];
                const Variant urls = server.get("urls", Variant());
                if ((urls.get_type() == Variant::ARRAY
                     && !Array(urls).is_empty())
                    || (urls.get_type() == Variant::STRING
                        && !String(urls).is_empty())) {
                    answer.servers.push_back(server);
                }
            }
        }
        if (!answer.servers.is_empty()) {
            answer.level = log::Level::INFO;
            return answer;
        }
        answer.message
            = "The TURN credential server answered without iceServers. "
              "It must answer {\"iceServers\": [...]}.";
        return answer;
    }
    const Variant error = content.get("error", String());
    const String detail
        = error.get_type() == Variant::STRING ? String(error) : String();
    if (p_code == 404) {
        answer.message = "The TURN credential URL answered 404. Check "
                         "networked/webrtc/turn_credentials_url.";
    } else if (p_code == 403) {
        answer.message = "The TURN credential server refused this game.";
    } else {
        answer.level = log::Level::WARN;
        answer.message = String("The TURN credential server answered ")
            + itos(p_code) + ".";
    }
    if (p_code != 404 && !detail.is_empty()) {
        answer.message += String(" ") + detail;
    }
    return answer;
}

TurnCredentials::~TurnCredentials() {
    gd::http_close(client);
}

Error TurnCredentials::begin(const String &p_url, int64_t p_now_msec) {
    const String url = p_url.strip_edges();
    if ((!url.begins_with("https://") && !url.begins_with("http://"))
        || url.get_slice("://", 1).get_slice("/", 0).is_empty()) {
        refuse(
            log::Level::ERROR,
            "networked/webrtc/turn_credentials_url is not an absolute URL. "
            "Set it to an https address."
        );
        return ERR_INVALID_PARAMETER;
    }
    client = gd::new_http_client();
    split_url(url, host, path);
    body = PackedByteArray();
    fetched_servers = Array();
    reason = String();
    started_msec = p_now_msec;
    int port = -1;
    String connect_host = host;
    const int scheme_end = int(host.find("://")) + 3;
    const int bracket = int(host.rfind("]"));
    const int colon = int(host.rfind(":"));
    if (colon > (bracket >= 0 ? bracket : scheme_end)) {
        port = int(host.substr(colon + 1).to_int());
        connect_host = host.substr(0, colon);
    }
    const Error err = gd::http_connect(client, connect_host, port);
    if (err != OK) {
        unreachable();
        return err;
    }
    stage = PHASE_CONNECTING;
    return OK;
}

void TurnCredentials::poll(int64_t p_now_msec) {
    if (stage == PHASE_IDLE || stage == PHASE_READY || stage == PHASE_FAILED) {
        return;
    }
    if (double(p_now_msec - started_msec) > budget_msec) {
        refuse(
            log::Level::WARN,
            "The TURN credential server did not answer within 5 seconds. "
            "Check that it is deployed."
        );
        return;
    }
    gd::http_poll(client);
    const int status = gd::http_status(client);
    if (gd::http_status_is_fault(status)) {
        unreachable();
        return;
    }
    if (stage == PHASE_CONNECTING) {
        if (status != gd::HTTP_CONNECTED) {
            return;
        }
        const Error err = gd::http_request(
            client,
            gd::HTTP_METHOD_GET,
            path,
            PackedStringArray()
        );
        if (err != OK) {
            unreachable();
            return;
        }
        stage = PHASE_REQUESTING;
        return;
    }
    if (stage == PHASE_REQUESTING) {
        if (status == gd::HTTP_REQUESTING) {
            return;
        }
        if (!gd::http_has_response(client)) {
            unreachable();
            return;
        }
        stage = PHASE_READING;
    }
    read_body();
    if (gd::http_status(client) != gd::HTTP_BODY) {
        const Answer answer = read_answer(gd::http_response_code(client), body);
        if (answer.servers.is_empty()) {
            refuse(answer.level, answer.message);
            return;
        }
        settle(answer.servers);
    }
}

void TurnCredentials::read_body() {
    while (gd::http_status(client) == gd::HTTP_BODY) {
        const PackedByteArray chunk = gd::http_read_chunk(client);
        if (chunk.is_empty()) {
            return;
        }
        body.append_array(chunk);
    }
}

void TurnCredentials::settle(const Array &p_servers) {
    fetched_servers = p_servers;
    stage = PHASE_READY;
    gd::http_close(client);
    client = Ref<RefCounted>();
}

void TurnCredentials::unreachable() {
    refuse(
        log::Level::WARN,
        String("The TURN credential server at ") + host
            + " is unreachable. Check the URL and the network."
    );
}

void TurnCredentials::refuse(log::Level p_level, const String &p_reason) {
    reason = p_reason;
    stage = PHASE_FAILED;
    gd::http_close(client);
    client = Ref<RefCounted>();
    log::write_at(
        p_level,
        sys::TRANSPORT,
        p_reason,
        __FUNCTION__,
        __FILE__,
        __LINE__
    );
}

} // namespace netw::connect
