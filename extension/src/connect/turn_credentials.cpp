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
const char *TurnCredentials::HEADERS_SETTING
    = "networked/webrtc/turn_credentials_headers";

namespace {

struct Cache {
    Array servers;
    bool attempted = false;
};

Cache &cache_slot() {
    static Cache held;
    return held;
}

} // namespace

String TurnCredentials::configured_url() {
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return String();
    }
    return String(settings->get_setting(URL_SETTING, String())).strip_edges();
}

PackedStringArray TurnCredentials::configured_headers() {
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (settings == nullptr) {
        return PackedStringArray();
    }
    const Variant held
        = settings->get_setting(HEADERS_SETTING, PackedStringArray());
    if (held.get_type() == Variant::PACKED_STRING_ARRAY) {
        return held;
    }
    PackedStringArray lines;
    if (held.get_type() == Variant::ARRAY) {
        const Array listed = held;
        for (int64_t at = 0; at < listed.size(); at++) {
            lines.push_back(String(listed[at]));
        }
    }
    return lines;
}

String TurnCredentials::resolve_url(
    const String &p_configured,
    const String &p_origin
) {
    const String configured = p_configured.strip_edges();
    if (configured.is_empty() || !configured.begins_with("/")) {
        return configured;
    }
    const String origin = p_origin.strip_edges();
    if (origin.is_empty()) {
        return String();
    }
    return origin.trim_suffix("/") + configured;
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

Array TurnCredentials::servers_from_body(
    int p_code,
    const PackedByteArray &p_body
) {
    Array servers;
    if (p_code != 200) {
        return servers;
    }
    const Variant parsed = JSON::parse_string(gd::utf8_string(p_body));
    if (parsed.get_type() != Variant::ARRAY) {
        return servers;
    }
    const Array listed = parsed;
    for (int64_t at = 0; at < listed.size(); at++) {
        if (listed[at].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary server = listed[at];
        if (!server.has("urls")) {
            continue;
        }
        servers.push_back(server);
    }
    return servers;
}

const Array &TurnCredentials::cached() {
    return cache_slot().servers;
}

bool TurnCredentials::attempted() {
    return cache_slot().attempted;
}

void TurnCredentials::adopt(const Array &p_servers) {
    cache_slot().servers = p_servers;
    cache_slot().attempted = true;
}

void TurnCredentials::forget() {
    cache_slot().servers = Array();
    cache_slot().attempted = false;
}

TurnCredentials::~TurnCredentials() {
    gd::http_close(client);
}

Error TurnCredentials::begin(
    const String &p_url,
    const PackedStringArray &p_headers,
    int64_t p_now_msec
) {
    cache_slot().attempted = true;
    if (p_url.strip_edges().is_empty()) {
        refuse("no credential url is configured");
        return ERR_INVALID_PARAMETER;
    }
    client = gd::new_http_client();
    if (client.is_null()) {
        refuse("the engine minted no HTTPClient");
        return ERR_CANT_CREATE;
    }
    String host;
    split_url(p_url, host, path);
    headers = p_headers;
    body = PackedByteArray();
    started_msec = p_now_msec;
    int port = -1;
    String connect_host = host;
    int scheme_end = 0;
    const int marker = int(host.find("://"));
    if (marker >= 0) {
        scheme_end = marker + 3;
    }
    const int bracket = int(host.rfind("]"));
    const int colon = int(host.rfind(":"));
    if (colon > (bracket >= 0 ? bracket : scheme_end)) {
        port = int(host.substr(colon + 1).to_int());
        connect_host = host.substr(0, colon);
    }
    const Error err = gd::http_connect(client, connect_host, port);
    if (err != OK) {
        refuse(String("connect refused: ") + itos(int(err)));
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
        refuse("the credential fetch outran its budget");
        return;
    }
    gd::http_poll(client);
    const int status = gd::http_status(client);
    if (gd::http_status_is_fault(status)) {
        refuse(String("the link reported status ") + itos(status));
        return;
    }
    if (stage == PHASE_CONNECTING) {
        if (status != gd::HTTP_CONNECTED) {
            return;
        }
        const Error err
            = gd::http_request(client, gd::HTTP_METHOD_GET, path, headers);
        if (err != OK) {
            refuse(String("request refused: ") + itos(int(err)));
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
            refuse("the host answered no response");
            return;
        }
        stage = PHASE_READING;
    }
    read_body();
    if (gd::http_status(client) != gd::HTTP_BODY) {
        const int code = gd::http_response_code(client);
        const Array servers = servers_from_body(code, body);
        if (servers.is_empty()) {
            refuse(
                String("the host answered ") + itos(code)
                + " with no usable ice server"
            );
            return;
        }
        settle(servers);
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
    adopt(p_servers);
    stage = PHASE_READY;
    gd::http_close(client);
    client = Ref<RefCounted>();
    NETW_INFO(
        sys::TRANSPORT,
        "the credential host answered %d ice server(s)",
        int(p_servers.size())
    );
}

void TurnCredentials::refuse(const String &p_reason) {
    reason = p_reason;
    stage = PHASE_FAILED;
    gd::http_close(client);
    client = Ref<RefCounted>();
    NETW_WARN(
        sys::TRANSPORT,
        "the turn credential fetch failed, so the configured ice servers "
        "stand: %s",
        p_reason
    );
}

} // namespace netw::connect
