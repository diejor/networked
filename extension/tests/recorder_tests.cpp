#include "support/netw_test.h"

#include "godot/ref_counted.hpp"
#include "support/netw_recorder.h"

namespace TestNetwRecorder {

using namespace godot;
using netw_test::Recorder;

Dictionary argument(const String &name, Variant::Type type) {
    Dictionary declared;
    declared["name"] = name;
    declared["type"] = int(type);
    return declared;
}

Ref<RefCounted> source_with_signals() {
    Ref<RefCounted> made;
    made.instantiate();
    Array carries_a_peer;
    carries_a_peer.push_back(argument("peer", Variant::INT));
    netw::gd::add_user_signal(made.ptr(), "peer_joined", carries_a_peer);
    netw::gd::add_user_signal(made.ptr(), "peer_left", carries_a_peer);
    netw::gd::add_user_signal(made.ptr(), "session_closed", Array());
    return made;
}

TEST_CASE("[Networked][Recorder][Hosted] a recorder counts what it saw") {
    Ref<RefCounted> source = source_with_signals();
    Recorder recorder(source.ptr(), {"peer_joined", "peer_left"});

    NETW_CHECK_EQ(recorder.count("peer_joined"), 0);

    source->emit_signal("peer_joined", 7);
    source->emit_signal("peer_joined", 9);
    source->emit_signal("peer_left", 7);

    NETW_CHECK_EQ(recorder.count("peer_joined"), 2);
    NETW_CHECK_EQ(recorder.count("peer_left"), 1);
    NETW_CHECK_EQ(recorder.count("session_closed"), 0);
}

TEST_CASE(
    "[Networked][Recorder][Hosted] a recorder keeps each emission's "
    "arguments"
) {
    Ref<RefCounted> source = source_with_signals();
    Recorder recorder(source.ptr(), {"peer_joined"});

    source->emit_signal("peer_joined", 7);
    source->emit_signal("peer_joined", 9);

    REQUIRE(recorder.args("peer_joined").size() == 1);
    CHECK(recorder.args("peer_joined")[0] == Variant(7));
    CHECK(recorder.args("peer_joined", 1)[0] == Variant(9));
    CHECK(recorder.args("peer_joined", 2).is_empty());
}

TEST_CASE(
    "[Networked][Recorder][Hosted] a recorder keeps order across signals"
) {
    Ref<RefCounted> source = source_with_signals();
    Recorder recorder(
        source.ptr(),
        {"peer_joined", "peer_left", "session_closed"}
    );

    source->emit_signal("peer_joined", 7);
    source->emit_signal("peer_left", 7);
    source->emit_signal("session_closed");

    const Vector<StringName> expected
        = {"peer_joined", "peer_left", "session_closed"};
    CHECK(recorder.order() == expected);
}

TEST_CASE("[Networked][Recorder][Hosted] clear forgets without disconnecting") {
    Ref<RefCounted> source = source_with_signals();
    Recorder recorder(source.ptr(), {"peer_joined"});

    source->emit_signal("peer_joined", 7);
    recorder.clear();
    NETW_CHECK_EQ(recorder.count("peer_joined"), 0);

    source->emit_signal("peer_joined", 9);
    NETW_CHECK_EQ(recorder.count("peer_joined"), 1);
    CHECK(recorder.args("peer_joined")[0] == Variant(9));
}

TEST_CASE("[Networked][Recorder][Hosted] a destroyed recorder disconnects") {
    Ref<RefCounted> source = source_with_signals();
    {
        Recorder recorder(source.ptr(), {"peer_joined"});
        source->emit_signal("peer_joined", 7);
        NETW_CHECK_EQ(recorder.count("peer_joined"), 1);
    }
    source->emit_signal("peer_joined", 9);

    Recorder second(source.ptr(), {"peer_joined"});
    source->emit_signal("peer_joined", 11);
    NETW_CHECK_EQ(second.count("peer_joined"), 1);
    CHECK(second.args("peer_joined")[0] == Variant(11));
}

} // namespace TestNetwRecorder
