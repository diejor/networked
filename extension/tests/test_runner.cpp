#define DOCTEST_CONFIG_IMPLEMENT
#include "support/netw_test.h"

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "godot/class_db.hpp"
#include "godot/project_settings.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/file_system_database.hpp"
#include "netw/api/interest_handle.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/colors.hpp"
#include "netw/connect/tracker_client.hpp"
#include "netw/display/pump_stats.hpp"
#include "netw/display/timing.hpp"
#include "netw/interest/relay.hpp"
#include "netw/log.hpp"
#include "netw/predict/frames.hpp"
#include "netw/profile.hpp"
#include "netw/property_set_builder.hpp"
#include "netw/schema_core.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"
#include "netw/spawn/record.hpp"
#include "netw/sync_kernel.hpp"
#include "netw/table/core.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/registry.hpp"
#include "netw/wire/spec.hpp"
#include "support/frame_drive.h"
#include "support/netw_cells.h"
#include "support/netw_reset.h"

using namespace godot;

NETW_INSTALL_RESET_LISTENER();

namespace netw {

namespace {

void probe_default_zone(int64_t value) {
    NETW_ZONE();
    NETW_ZONE_COLOR(colors::TICK);
    NETW_ZONE_VALUE(value);
    NETW_ZONE_TEXT_F("value=%lld", static_cast<long long>(value));
    NETW_ZONE_NAME_F("probe %lld", static_cast<long long>(value));
}

void probe_named_zone() {
    NETW_ZONE_N("Networked named instrumentation probe");
}

void probe_colored_zone() {
    NETW_ZONE_C(colors::TICK);
}

void compile_ambient_profile_macros() {
    if (false) {
        NETW_THREAD("Networked test", 1);
        NETW_LOCKABLE(std::mutex, lock);
        void *pointer = &lock;
        NETW_ALLOC_N(pointer, sizeof(lock), "Networked test lock");
        NETW_FREE_N(pointer, "Networked test lock");
        NETW_APP_INFO("Networked native tests", 22);
    }
}

std::string escape_xml(const char *text) {
    std::string escaped;
    if (text == nullptr) {
        return escaped;
    }
    for (const char *cursor = text; *cursor != '\0'; ++cursor) {
        switch (*cursor) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '\"':
                escaped += "&quot;";
                break;
            case '\'':
                escaped += "&apos;";
                break;
            default:
                escaped += *cursor;
                break;
        }
    }
    return escaped;
}

class NetwJunitReporter final : public doctest::IReporter {
    struct Case {
        std::string file;
        std::string name;
        std::string detail;
        double seconds = 0.0;
        bool failure = false;
        bool error = false;
    };

    const doctest::ContextOptions &options;
    std::vector<Case> cases;
    std::vector<std::string> subcases;

    Case *current() {
        return cases.empty() ? nullptr : &cases.back();
    }

    void append_detail(const std::string &text) {
        Case *test_case = current();
        if (test_case == nullptr || text.empty()) {
            return;
        }
        if (!test_case->detail.empty()) {
            test_case->detail += "\n";
        }
        test_case->detail += text;
    }

    std::string subcase_prefix() const {
        std::string prefix;
        for (const std::string &name : subcases) {
            prefix += name;
            prefix += " / ";
        }
        return prefix;
    }

    std::string active_context() const {
        const int count = get_num_active_contexts();
        if (count == 0) {
            return std::string();
        }
        const doctest::IContextScope *const *scopes = get_active_contexts();
        std::ostringstream stream;
        for (int index = 0; index < count; ++index) {
            stream << "\n  logged: ";
            scopes[index]->stringify(&stream);
        }
        return stream.str();
    }

public:
    explicit NetwJunitReporter(const doctest::ContextOptions &context)
        : options(context) {
    }

    void report_query(const doctest::QueryData &) override {
    }
    void test_run_start() override {
    }

    void test_run_end(const doctest::TestRunStats &stats) override {
        int failures = 0;
        int errors = 0;
        for (const Case &test_case : cases) {
            failures += test_case.failure ? 1 : 0;
            errors += test_case.error ? 1 : 0;
        }
        FILE *output = std::fopen(options.out.c_str(), "wb");
        if (output == nullptr) {
            return;
        }
        std::fprintf(output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        std::fprintf(
            output,
            "<testsuites tests=\"%u\" failures=\"%d\">\n",
            stats.numTestCasesPassingFilters,
            failures
        );
        std::fprintf(
            output,
            "  <testsuite name=\"networked-native\" tests=\"%u\" "
            "failures=\"%d\" errors=\"%d\">\n",
            stats.numTestCasesPassingFilters,
            failures,
            errors
        );
        for (const Case &test_case : cases) {
            const std::string file = escape_xml(test_case.file.c_str());
            const std::string name = escape_xml(test_case.name.c_str());
            std::fprintf(
                output,
                "    <testcase classname=\"%s\" name=\"%s\" "
                "time=\"%.9f\">",
                file.c_str(),
                name.c_str(),
                test_case.seconds
            );
            if (test_case.failure || test_case.error) {
                const char *tag = test_case.error ? "error" : "failure";
                const std::string detail = escape_xml(test_case.detail.c_str());
                std::fprintf(output, "<%s>%s</%s>", tag, detail.c_str(), tag);
            }
            std::fprintf(output, "</testcase>\n");
        }
        std::fprintf(output, "  </testsuite>\n</testsuites>\n");
        std::fclose(output);
    }

    void test_case_start(const doctest::TestCaseData &data) override {
        cases.push_back({data.m_file.c_str(), data.m_name});
        subcases.clear();
    }

    void test_case_reenter(const doctest::TestCaseData &) override {
        subcases.clear();
    }

    void test_case_end(const doctest::CurrentTestCaseStats &stats) override {
        Case *test_case = current();
        if (test_case == nullptr) {
            return;
        }
        test_case->seconds = stats.seconds;
        if (!stats.testCaseSuccess && !test_case->error) {
            test_case->failure = true;
        }
    }

    void test_case_exception(
        const doctest::TestCaseException &exception
    ) override {
        Case *test_case = current();
        if (test_case == nullptr) {
            return;
        }
        test_case->error = true;
        append_detail(
            subcase_prefix() + exception.error_string.c_str() + active_context()
        );
    }

    void subcase_start(const doctest::SubcaseSignature &signature) override {
        subcases.push_back(signature.m_name.c_str());
    }

    void subcase_end() override {
        if (!subcases.empty()) {
            subcases.pop_back();
        }
    }

    void log_assert(const doctest::AssertData &assertion) override {
        if (!assertion.m_failed) {
            return;
        }
        Case *test_case = current();
        if (test_case == nullptr) {
            return;
        }
        test_case->failure = true;
        std::string text = subcase_prefix();
        text += assertion.m_expr;
        text += ": ";
        text += assertion.m_decomp.c_str();
        append_detail(text + active_context());
    }

    void log_message(const doctest::MessageData &message) override {
        if ((message.m_severity & doctest::assertType::is_warn) != 0) {
            return;
        }
        Case *test_case = current();
        if (test_case == nullptr) {
            return;
        }
        test_case->failure = true;
        append_detail(
            subcase_prefix() + message.m_string.c_str() + active_context()
        );
    }

    void test_case_skipped(const doctest::TestCaseData &) override {
    }
};

DOCTEST_REGISTER_REPORTER("netw-junit", 0, NetwJunitReporter);

} // namespace

int run_native_tests(
    const String &filter,
    const String &report_path,
    const String &cells_path
) {
    const String global_path
        = ProjectSettings::get_singleton()->globalize_path(report_path);
    const CharString path = global_path.utf8();
    std::filesystem::path file_path(path.get_data());
    std::filesystem::create_directories(file_path.parent_path());

    doctest::Context context;
    context.setOption("reporters", "netw-junit");
    context.setOption("out", path.get_data());
    context.setOption("no-version", true);
    if (!filter.is_empty()) {
        const CharString filter_text = filter.utf8();
        context.setOption("test-case", filter_text.get_data());
    }
    const int code = context.run();
    if (!cells_path.is_empty()) {
        const CharString cells = ProjectSettings::get_singleton()
                                     ->globalize_path(cells_path)
                                     .utf8();
        std::filesystem::create_directories(
            std::filesystem::path(cells.get_data()).parent_path()
        );
        netw_test::Cells::write(cells.get_data());
    }
    return code;
}

void NetwNativeTests::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("run", "filter", "report_path", "cells_path"),
        &NetwNativeTests::run,
        DEFVAL(String()),
        DEFVAL(String("res://reports/native/results.xml")),
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("frame_advance"),
        &NetwNativeTests::frame_advance
    );
    ClassDB::bind_method(
        D_METHOD("instrumentation_probe", "value"),
        &NetwNativeTests::instrumentation_probe
    );
    ClassDB::bind_method(D_METHOD("wire_spec"), &NetwNativeTests::wire_spec);
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("schema_model_clear"),
        &NetwNativeTests::schema_model_clear
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("file_system_database_forget_roots"),
        &NetwNativeTests::file_system_database_forget_roots
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("tracker_book_clear"),
        &NetwNativeTests::tracker_book_clear
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("property_set_keys_of_script", "script", "record"),
        &NetwNativeTests::property_set_keys_of_script
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("scene_enter", "session", "scene", "stem", "owns_its_world"),
        &NetwNativeTests::scene_enter
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("scene_pending_request", "session"),
        &NetwNativeTests::scene_pending_request
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("scene_sync_local_participant", "session"),
        &NetwNativeTests::scene_sync_local_participant
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("scene_ensure_host_view", "session"),
        &NetwNativeTests::scene_ensure_host_view
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("scene_release_host_view", "session"),
        &NetwNativeTests::scene_release_host_view
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("scene_refresh_current", "session"),
        &NetwNativeTests::scene_refresh_current
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("scene_container_meta"),
        &NetwNativeTests::scene_container_meta
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("scene_retire", "session", "scene", "drain_pumps"),
        &NetwNativeTests::scene_retire
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("scene_move_participants", "session", "scene", "peers"),
        &NetwNativeTests::scene_move_participants
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("display_clock_tick", "session", "delta", "tick"),
        &NetwNativeTests::display_clock_tick
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("display_pump", "session", "delta"),
        &NetwNativeTests::display_pump
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD(
            "display_record",
            "session",
            "node",
            "target_property",
            "value",
            "tick",
            "spec",
            "authoring_tick"
        ),
        &NetwNativeTests::display_record
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("display_mark_role_dirty", "session", "entity"),
        &NetwNativeTests::display_mark_role_dirty
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("observe_node_entity_ref", "session", "node_ref"),
        &NetwNativeTests::observe_node_entity_ref
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_is_visible", "entity", "peer_id"),
        &NetwNativeTests::interest_is_visible
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_layer_ids", "entity"),
        &NetwNativeTests::interest_layer_ids
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_leave_policy_for", "entity", "layer_id", "fallback"),
        &NetwNativeTests::interest_leave_policy_for
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_custom_leave_for", "entity", "layer_id"),
        &NetwNativeTests::interest_custom_leave_for
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD(
            "interest_perception_policy_for",
            "entity",
            "layer_id",
            "fallback"
        ),
        &NetwNativeTests::interest_perception_policy_for
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_custom_perception_for", "entity", "layer_id"),
        &NetwNativeTests::interest_custom_perception_for
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_set_report_observers", "entity", "enabled"),
        &NetwNativeTests::interest_set_report_observers
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_reports_observers", "entity"),
        &NetwNativeTests::interest_reports_observers
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_on_observed", "entity", "callback"),
        &NetwNativeTests::interest_on_observed
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_on_enter", "entity", "layer_id", "callback"),
        &NetwNativeTests::interest_on_enter
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_on_leave", "entity", "layer_id", "callback"),
        &NetwNativeTests::interest_on_leave
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD(
            "interest_set_leave_policy",
            "entity",
            "layer_id",
            "policy",
            "custom"
        ),
        &NetwNativeTests::interest_set_leave_policy,
        DEFVAL(Callable())
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD(
            "interest_set_perception_policy",
            "entity",
            "layer_id",
            "policy",
            "custom"
        ),
        &NetwNativeTests::interest_set_perception_policy,
        DEFVAL(Callable())
    );
    ClassDB::bind_static_method(
        "NetwNativeTests",
        D_METHOD("interest_on_unobserved", "entity", "callback"),
        &NetwNativeTests::interest_on_unobserved
    );
}

Dictionary NetwNativeTests::run(
    const String &filter,
    const String &report_path,
    const String &cells_path
) {
    Dictionary result;
    result[StringName("filter")] = filter;
    result[StringName("report_path")] = report_path;
    result[StringName("cells_path")] = cells_path;
    result[StringName("exit_code")]
        = run_native_tests(filter, report_path, cells_path);
    result[StringName("cells")] = netw_test::Cells::count();
    return result;
}

bool NetwNativeTests::frame_advance() {
    return netw_test::frame_drive_advance();
}

void NetwNativeTests::schema_model_clear() {
    schema_model::clear();
}

void NetwNativeTests::file_system_database_forget_roots() {
    FileSystemDatabase::forget_claimed_roots();
}

void NetwNativeTests::tracker_book_clear() {
    netw::connect::TrackerBook::shared().clear();
}

TypedArray<StringName> NetwNativeTests::property_set_keys_of_script(
    const Ref<Script> &p_script,
    int64_t p_record
) {
    const Ref<NetwPropertySet> set = property_set_builder::from_script(
        p_script,
        p_record,
        nullptr,
        nullptr
    );
    if (set.is_null()) {
        return TypedArray<StringName>();
    }
    return set->keys();
}

void NetwNativeTests::scene_enter(
    NetwMultiplayer *session,
    const RID &scene,
    const StringName &stem,
    bool owns_its_world
) {
    if (session != nullptr) {
        session->get_scene_core()->scene_enter(scene, stem, owns_its_world);
    }
}

Ref<NetwPromise> NetwNativeTests::scene_pending_request(
    NetwMultiplayer *session
) {
    return session != nullptr ? session->get_scene_core()->get_pending_request()
                              : Ref<NetwPromise>();
}

void NetwNativeTests::scene_sync_local_participant(NetwMultiplayer *session) {
    if (session != nullptr) {
        session->scene_sync_local_participant();
    }
}

void NetwNativeTests::scene_ensure_host_view(NetwMultiplayer *session) {
    if (session != nullptr) {
        session->scene_ensure_host_view();
    }
}

void NetwNativeTests::scene_release_host_view(NetwMultiplayer *session) {
    if (session != nullptr) {
        session->scene_release_host_view();
    }
}

void NetwNativeTests::scene_refresh_current(NetwMultiplayer *session) {
    if (session != nullptr) {
        session->scene_refresh_current();
    }
}

bool NetwNativeTests::rpc_sender_admits(
    NetwMultiplayer *p_session,
    Node *p_node,
    const StringName &p_method,
    int64_t p_sender
) {
    if (p_session == nullptr || p_node == nullptr) {
        return false;
    }
    const Ref<Script> script = p_node->get_script();
    return p_session->rpc_sender_allowed(
        netw::script::model::get_rpc_options(script, p_method),
        script,
        p_method,
        NetwEntity::of(p_node),
        p_node,
        p_sender
    );
}

StringName NetwNativeTests::scene_container_meta() {
    return NetwMultiplayer::scene_container_meta();
}

void NetwNativeTests::scene_retire(
    NetwMultiplayer *session,
    const RID &p_scene,
    int p_drain_pumps
) {
    if (session == nullptr) {
        return;
    }
    session->get_scene_core()->scene_retire(p_scene, p_drain_pumps);
    session->scene_settle_refresh();
}

Ref<NetwGroupPromise> NetwNativeTests::scene_move_participants(
    NetwMultiplayer *session,
    const RID &p_scene,
    const PackedInt32Array &p_peers
) {
    if (session == nullptr) {
        return Ref<NetwGroupPromise>();
    }
    return session->scene_move_participants(p_scene, p_peers);
}

display::Runtime *NetwNativeTests::display_runtime_of(
    NetwMultiplayer *session,
    const RID &entity
) {
    if (session == nullptr) {
        return nullptr;
    }
    return session->get_display_book()->runtime_of(entity);
}

display::Runtime *NetwNativeTests::display_runtime_at(
    NetwMultiplayer *session,
    int64_t route
) {
    if (session == nullptr) {
        return nullptr;
    }
    return session->get_display_book()->runtime_at(route);
}

LocalVector<display::Runtime *> NetwNativeTests::display_runtimes(
    NetwMultiplayer *session
) {
    if (session == nullptr) {
        return LocalVector<display::Runtime *>();
    }
    return session->get_display_book()->runtimes();
}

int64_t NetwNativeTests::display_route_of(
    NetwMultiplayer *session,
    const RID &entity
) {
    if (session == nullptr) {
        return 0;
    }
    return session->get_display_book()->route_of(entity);
}

void NetwNativeTests::display_clock_tick(
    NetwMultiplayer *session,
    const double p_delta,
    const int64_t p_tick
) {
    if (session != nullptr) {
        session->display_on_clock_tick(p_delta, p_tick);
    }
}

Error NetwNativeTests::display_pump(NetwMultiplayer *session, double delta) {
    return session != nullptr ? session->display_pump(delta) : OK;
}

void NetwNativeTests::display_pump_runtime(
    NetwMultiplayer *session,
    display::Runtime *runtime,
    const display::Timing &timing
) {
    if (session != nullptr) {
        session->display_pump_runtime(
            runtime,
            timing,
            session->get_display_book()->get_stats()
        );
    }
}

void NetwNativeTests::display_record(
    NetwMultiplayer *session,
    Node *node,
    const StringName &target_property,
    const Variant &value,
    int64_t tick,
    const Ref<NetwInterpolate> &spec,
    bool authoring_tick
) {
    if (session != nullptr) {
        session->display_record(
            node,
            target_property,
            value,
            tick,
            spec,
            authoring_tick
        );
    }
}

bool NetwNativeTests::display_wants_runtime(
    NetwMultiplayer *session,
    Node *owner
) {
    return session != nullptr && session->display_wants_runtime(owner);
}

double NetwNativeTests::display_chase_smooth_time(
    NetwMultiplayer *session,
    display::Runtime *runtime,
    const display::Timing &timing
) {
    return session != nullptr
        ? session->display_chase_smooth_time(runtime, timing)
        : 0.0;
}

void NetwNativeTests::display_absorb_recovery(
    NetwMultiplayer *session,
    display::Runtime *runtime,
    const Dictionary &deltas,
    bool teleported
) {
    if (session != nullptr) {
        session->display_absorb_recovery(runtime, deltas, teleported);
    }
}

void NetwNativeTests::display_mark_role_dirty(
    NetwMultiplayer *session,
    const RID &entity
) {
    if (session != nullptr) {
        session->display_mark_role_dirty(entity);
    }
}

void NetwNativeTests::observe_node_entity_ref(
    NetwMultiplayer *session,
    const Variant &p_node_ref
) {
    if (session != nullptr) {
        session->observe_node_entity_ref(p_node_ref);
    }
}

bool NetwNativeTests::interest_is_visible(NetwEntity *entity, int64_t peer_id) {
    return entity != nullptr && entity->get_interest()->is_visible_to(peer_id);
}

TypedArray<StringName> NetwNativeTests::interest_layer_ids(NetwEntity *entity) {
    return entity != nullptr
        ? entity->get_record()->get_interest_facet().layer_ids()
        : TypedArray<StringName>();
}

int64_t NetwNativeTests::interest_leave_policy_for(
    NetwEntity *entity,
    const StringName &layer_id,
    int64_t fallback
) {
    if (entity == nullptr) {
        return fallback;
    }
    return entity->get_record()
        ->get_interest_facet()
        .declaration()
        ->leave_policy_for(layer_id, int(fallback));
}

Callable NetwNativeTests::interest_custom_leave_for(
    NetwEntity *entity,
    const StringName &layer_id
) {
    return entity != nullptr ? entity->get_record()
                                   ->get_interest_facet()
                                   .declaration()
                                   ->custom_leave_for(layer_id)
                             : Callable();
}

int64_t NetwNativeTests::interest_perception_policy_for(
    NetwEntity *entity,
    const StringName &layer_id,
    int64_t fallback
) {
    if (entity == nullptr) {
        return fallback;
    }
    return entity->get_record()
        ->get_interest_facet()
        .declaration()
        ->perception_policy_for(layer_id, int(fallback));
}

Callable NetwNativeTests::interest_custom_perception_for(
    NetwEntity *entity,
    const StringName &layer_id
) {
    return entity != nullptr ? entity->get_record()
                                   ->get_interest_facet()
                                   .declaration()
                                   ->custom_perception_for(layer_id)
                             : Callable();
}

void NetwNativeTests::interest_set_report_observers(
    NetwEntity *entity,
    bool enabled
) {
    if (entity != nullptr) {
        entity->get_record()
            ->get_interest_facet()
            .declaration()
            ->set_reports_observers(enabled);
    }
}

void NetwNativeTests::interest_queue_observer_awareness(
    NetwMultiplayer *session,
    const StringName &layer_id,
    NetwEntity *entity,
    int64_t observer_peer,
    int kind
) {
    if (session != nullptr) {
        session->interest_queue_observer_awareness(
            layer_id,
            Ref<NetwEntity>(entity),
            observer_peer,
            kind
        );
    }
}

Array NetwNativeTests::interest_awareness_drain(NetwMultiplayer *session) {
    return session != nullptr ? session->interest_awareness_drain() : Array();
}

bool NetwNativeTests::interest_reports_observers(NetwEntity *entity) {
    return entity != nullptr
        && entity->get_record()
               ->get_interest_facet()
               .declaration()
               ->get_reports_observers();
}

void NetwNativeTests::interest_on_observed(
    NetwEntity *entity,
    const Callable &callback
) {
    if (entity != nullptr) {
        entity->get_record()->get_interest_facet().on_observed(callback);
    }
}

void NetwNativeTests::interest_on_enter(
    NetwEntity *entity,
    const StringName &layer_id,
    const Callable &callback
) {
    if (entity != nullptr) {
        entity->get_record()->get_interest_facet().on_enter(layer_id, callback);
    }
}

void NetwNativeTests::interest_on_leave(
    NetwEntity *entity,
    const StringName &layer_id,
    const Callable &callback
) {
    if (entity != nullptr) {
        entity->get_record()->get_interest_facet().on_leave(layer_id, callback);
    }
}

void NetwNativeTests::interest_set_leave_policy(
    NetwEntity *entity,
    const StringName &layer_id,
    int64_t policy,
    const Callable &custom
) {
    if (entity != nullptr) {
        entity->get_record()->get_interest_facet().set_leave_policy(
            layer_id,
            int(policy),
            custom
        );
    }
}

void NetwNativeTests::interest_set_perception_policy(
    NetwEntity *entity,
    const StringName &layer_id,
    int64_t policy,
    const Callable &custom
) {
    if (entity != nullptr) {
        entity->get_record()->get_interest_facet().set_perception_policy(
            layer_id,
            int(policy),
            custom
        );
    }
}

void NetwNativeTests::interest_on_unobserved(
    NetwEntity *entity,
    const Callable &callback
) {
    if (entity != nullptr) {
        entity->get_record()->get_interest_facet().on_unobserved(callback);
    }
}

Dictionary NetwNativeTests::wire_spec() const {
    return wire::spec_document();
}

void NetwNativeTests::instrumentation_probe(int64_t value) const {
    NETW_ZONE_NC("Networked instrumentation probe", colors::TICK);
    NETW_ZONE_VALUE(value);
#if defined(NETW_PROFILING)
    const CharString text = String::num_int64(value).utf8();
    NETW_ZONE_TEXT(text.get_data(), text.length());
#endif
    NETW_TICK_MARK();
    NETW_TRACE(netw::sys::TEST, "Networked native trace probe");
    probe_default_zone(value);
    probe_named_zone();
    probe_colored_zone();
    compile_ambient_profile_macros();
}

} // namespace netw
