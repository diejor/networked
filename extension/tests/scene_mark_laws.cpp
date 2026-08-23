#include "support/netw_test.h"

#include "godot/script.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene_mark.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneMarkLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::NetwSceneMark;
using netw_test::CallLog;

Ref<NetwSceneMark> mark_of(
    bool p_marked,
    bool p_gated,
    bool p_session_wide,
    bool p_captured
) {
    Ref<NetwSceneMark> mark;
    mark.instantiate();
    mark->set_marked(p_marked);
    mark->set_gated(p_gated);
    mark->set_session_wide(p_session_wide);
    mark->set_captured(p_captured);
    return mark;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SM1 a session with no mark reader installed "
    "still answers a record, so a door reads an unmarked destination off its "
    "fields instead of testing the answer for null before every read"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    const Ref<NetwSceneMark> mark = core->scene_mark_of(Ref<Script>());

    REQUIRE(mark.is_valid());
    CHECK_FALSE(mark->get_marked());
    CHECK_FALSE(mark->get_captured());
    CHECK_FALSE(mark->is_deny_default());
    CHECK(mark->get_pending_method().is_empty());
    NETW_CHECK_CLOSE(mark->get_deadline(), 0.0, 1e-9);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SM2 a null script is answered a blank mark "
    "without asking the reader, because a destination with no script declared "
    "nothing and the authoring tier has nothing to be keyed by"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog reader;
    core->set_scene_mark_reader(
        reader.answering("read", mark_of(true, true, true, true))
    );

    const Ref<NetwSceneMark> mark = core->scene_mark_of(Ref<Script>());

    NETW_CHECK_EQ(reader.count("read"), 0);
    REQUIRE(mark.is_valid());
    CHECK_FALSE(mark->get_marked());
    CHECK_FALSE(mark->is_deny_default());
}

TEST_CASE(
    "[Networked][Scene][Hosted] SM3 session-wide is deny-default on its own "
    "without the gated opt-in, because a request that replaces every peer's "
    "presentation is never one a default admits"
) {
    CHECK_FALSE(mark_of(true, false, false, false)->is_deny_default());
    CHECK(mark_of(true, true, false, false)->is_deny_default());
    CHECK(mark_of(true, false, true, false)->is_deny_default());
    CHECK(mark_of(true, true, true, false)->is_deny_default());
}

TEST_CASE(
    "[Networked][Scene][Hosted] SM4 an undeclared deadline reads as the "
    "caller's fallback rather than as an instant timeout, so zero is the "
    "unset spelling and only a positive declaration overrides the default"
) {
    const Ref<NetwSceneMark> mark = mark_of(true, false, false, true);

    NETW_CHECK_CLOSE(mark->deadline_or(10.0), 10.0, 1e-9);

    mark->set_deadline(2.5);

    NETW_CHECK_CLOSE(mark->deadline_or(10.0), 2.5, 1e-9);

    mark->set_deadline(-1.0);

    NETW_CHECK_CLOSE(mark->deadline_or(10.0), 10.0, 1e-9);
}

#if defined(NETW_TIER_HOSTED)

constexpr const char *MARKED_SCRIPT
    = "res://tests/support/scene/marked_test_scene.gd";

TEST_CASE(
    "[Networked][Scene] SM5 the reader is asked with the script it "
    "was given and its record is the one handed back, so one reading serves "
    "every door rather than each door reaching into the authoring tier itself"
) {
    const Ref<Script> script
        = ResourceLoader::get_singleton()->load(String(MARKED_SCRIPT));
    REQUIRE(script.is_valid());

    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const Ref<NetwSceneMark> declared = mark_of(true, false, true, true);
    declared->set_pending_method(StringName("_show_loading"));
    declared->set_deadline(2.5);
    const CallLog reader;
    core->set_scene_mark_reader(reader.answering("read", declared));

    const Ref<NetwSceneMark> mark = core->scene_mark_of(script);

    NETW_CHECK_EQ(reader.count("read"), 1);
    const Array asked = reader.args("read");
    REQUIRE(asked.size() == 1);
    const Ref<Script> asked_with = asked[0];
    CHECK(bool(asked_with == script));
    CHECK(bool(mark == declared));
    CHECK(mark->get_marked());
    CHECK(mark->get_captured());
    CHECK(mark->get_session_wide());
    CHECK(mark->is_deny_default());
    CHECK(bool(mark->get_pending_method() == StringName("_show_loading")));
    NETW_CHECK_CLOSE(mark->deadline_or(10.0), 2.5, 1e-9);
}

TEST_CASE(
    "[Networked][Scene] SM6 a reader that answers something other "
    "than a mark reads as a blank mark rather than as a null, so an "
    "authoring tier that answered wrongly refuses instead of felling the door"
) {
    const Ref<Script> script
        = ResourceLoader::get_singleton()->load(String(MARKED_SCRIPT));
    REQUIRE(script.is_valid());

    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog reader;
    core->set_scene_mark_reader(reader.answering("read", Variant()));

    const Ref<NetwSceneMark> mark = core->scene_mark_of(script);

    NETW_CHECK_EQ(reader.count("read"), 1);
    REQUIRE(mark.is_valid());
    CHECK_FALSE(mark->get_marked());
    CHECK_FALSE(mark->is_deny_default());
}

#endif

} // namespace TestNetwSceneMarkLaws
