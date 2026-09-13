#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/quantize.hpp"
#include "support/netw_call_log.h"

namespace TestNetwPropertyConfig {

using namespace godot;
using netw::NetwInterpolate;
using netw::NetwMemberConfig;
using netw::NetwPropertyConfig;
using netw::NetwPropertySet;
using netw::NetwQuantize;
using netw::NetwQuantizeScalar;
using netw_test::CallLog;

Ref<NetwPropertyConfig> make_config() {
    Ref<NetwPropertyConfig> config;
    config.instantiate();
    return config;
}

Ref<NetwQuantize> make_quantizer() {
    Ref<NetwQuantizeScalar> packer;
    packer.instantiate();
    return packer;
}

Ref<NetwInterpolate> make_interpolator(double p_smoothing) {
    Ref<NetwInterpolate> smoother;
    smoother.instantiate();
    smoother->set_smoothing(p_smoothing);
    return smoother;
}

Variant at(const Array &p_list, int p_index) {
    return p_index < p_list.size() ? p_list[p_index] : Variant();
}

Ref<NetwQuantize> first_quantizer(const Array &p_list) {
    return p_list.is_empty() ? Ref<NetwQuantize>()
                             : Ref<NetwQuantize>(p_list[0]);
}

Array one(const Variant &p_value) {
    Array list;
    list.push_back(p_value);
    return list;
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP1 a config nobody chained anything on is "
    "a property in no per-tick set, volatile, unpersisted and causal, with "
    "every set-level knob and every per-field override unwritten"
) {
    const Ref<NetwPropertyConfig> config = make_config();

    CHECK(config->get_is_property());
    CHECK_FALSE(config->get_in_state_set());
    CHECK_FALSE(config->get_in_input_set());
    CHECK_FALSE(config->get_in_broadcast_set());
    CHECK_FALSE(config->get_is_spawn_state());
    CHECK_FALSE(config->get_is_persisted());
    CHECK_FALSE(config->get_set_masked());
    NETW_CHECK_EQ(int(config->get_lane()), int(NetwPropertySet::VOLATILE));
    NETW_CHECK_EQ(
        int(config->get_property_class()),
        int(NetwPropertySet::CAUSAL)
    );
    NETW_CHECK_EQ(
        int(config->get_set_audience()),
        int(NetwPropertySet::AUDIENCE_PUBLIC)
    );
    CHECK(config->get_carry_channel().is_empty());
    CHECK_FALSE(config->get_explicit_teleport_only());
    CHECK_FALSE(config->get_explicit_reconcile_only());
    NETW_CHECK_CLOSE(config->get_converge_stiffness(), 0.0, 0.0);
    NETW_CHECK_CLOSE(config->get_persist_interval(), 0.0, 0.0);
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP2 an unwritten integer knob reads UNSET "
    "rather than a value, which is what lets a set derived from several "
    "members tell a member that named the knob from one that did not"
) {
    const Ref<NetwPropertyConfig> config = make_config();

    NETW_CHECK_EQ(NetwPropertyConfig::UNSET, -1);
    NETW_CHECK_EQ(config->get_set_trigger(), NetwPropertyConfig::UNSET);
    NETW_CHECK_EQ(config->get_set_heartbeat_ticks(), NetwPropertyConfig::UNSET);
    NETW_CHECK_EQ(config->get_set_window(), NetwPropertyConfig::UNSET);
    NETW_CHECK_ORDER(config->get_epsilon_override(), 0.0, <);
    NETW_CHECK_ORDER(config->get_teleport_at_override(), 0.0, <);
    NETW_CHECK_ORDER(config->get_set_every_tick_interval(), 0.0, <);
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP3 state marks the field into the state "
    "set on the volatile lane triggering on change, and writes nothing else, "
    "so the authoritative kind stays public and unpersisted"
) {
    const Ref<NetwPropertyConfig> config = make_config();

    config->state();

    CHECK(config->get_in_state_set());
    CHECK_FALSE(config->get_in_input_set());
    CHECK_FALSE(config->get_in_broadcast_set());
    NETW_CHECK_EQ(int(config->get_lane()), int(NetwPropertySet::VOLATILE));
    NETW_CHECK_EQ(
        config->get_set_trigger(),
        int64_t(NetwPropertySet::TRIGGER_ON_CHANGE)
    );
    NETW_CHECK_EQ(
        int(config->get_set_audience()),
        int(NetwPropertySet::AUDIENCE_PUBLIC)
    );
    CHECK_FALSE(config->get_is_persisted());
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP4 input narrows its set to the server as "
    "well as marking the field, which is the axis that makes it a command "
    "stream rather than a state set that happens to be client-written"
) {
    const Ref<NetwPropertyConfig> config = make_config();

    config->input();

    CHECK(config->get_in_input_set());
    CHECK_FALSE(config->get_in_state_set());
    CHECK_FALSE(config->get_in_broadcast_set());
    NETW_CHECK_EQ(int(config->get_lane()), int(NetwPropertySet::VOLATILE));
    NETW_CHECK_EQ(
        config->get_set_trigger(),
        int64_t(NetwPropertySet::TRIGGER_ON_CHANGE)
    );
    NETW_CHECK_EQ(
        int(config->get_set_audience()),
        int(NetwPropertySet::AUDIENCE_SERVER_ONLY)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP5 broadcast marks the display set and "
    "leaves the audience public, because a server-only volatile stream is an "
    "input set wearing a costume"
) {
    const Ref<NetwPropertyConfig> config = make_config();

    config->broadcast();

    CHECK(config->get_in_broadcast_set());
    CHECK_FALSE(config->get_in_state_set());
    CHECK_FALSE(config->get_in_input_set());
    NETW_CHECK_EQ(int(config->get_lane()), int(NetwPropertySet::VOLATILE));
    NETW_CHECK_EQ(
        config->get_set_trigger(),
        int64_t(NetwPropertySet::TRIGGER_ON_CHANGE)
    );
    NETW_CHECK_EQ(
        int(config->get_set_audience()),
        int(NetwPropertySet::AUDIENCE_PUBLIC)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP6 every set-level and per-field verb "
    "records the one axis it names, so a declaration reads back exactly what "
    "its chain said and nothing a neighbouring verb wrote"
) {
    const Ref<NetwPropertyConfig> config = make_config();

    config->retained();
    config->derived();
    config->converge(0.4);
    config->epsilon(0.05);
    config->teleport_at(3.0);
    config->teleport_only();
    config->reconcile_only();
    config->every_tick(0.25);
    config->heartbeat(60);
    config->windowed(3);
    config->audience(true);
    config->masked();
    config->persisted(30.0);
    config->on_spawn();

    NETW_CHECK_EQ(int(config->get_lane()), int(NetwPropertySet::RETAINED));
    NETW_CHECK_EQ(
        int(config->get_property_class()),
        int(NetwPropertySet::DERIVED)
    );
    NETW_CHECK_CLOSE(config->get_converge_stiffness(), 0.4, 1e-9);
    NETW_CHECK_CLOSE(config->get_epsilon_override(), 0.05, 1e-9);
    NETW_CHECK_CLOSE(config->get_teleport_at_override(), 3.0, 1e-9);
    CHECK(config->get_explicit_teleport_only());
    CHECK(config->get_explicit_reconcile_only());
    NETW_CHECK_EQ(
        config->get_set_trigger(),
        int64_t(NetwPropertySet::TRIGGER_TICK)
    );
    NETW_CHECK_CLOSE(config->get_set_every_tick_interval(), 0.25, 1e-9);
    NETW_CHECK_EQ(config->get_set_heartbeat_ticks(), 60);
    NETW_CHECK_EQ(config->get_set_window(), 3);
    NETW_CHECK_EQ(
        int(config->get_set_audience()),
        int(NetwPropertySet::AUDIENCE_SERVER_ONLY)
    );
    CHECK(config->get_set_masked());
    CHECK(config->get_is_persisted());
    NETW_CHECK_CLOSE(config->get_persist_interval(), 30.0, 1e-9);
    CHECK(config->get_is_spawn_state());
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP7 carry_step writes its rule through the "
    "installed binder, naming the body the Callable is bound to, because a "
    "rule belongs to one node while a declaration is shared by its script"
) {
    Node *body = memnew(Node);
    const Ref<NetwPropertyConfig> config = make_config();
    config->set_context_name(StringName("spin"));
    const CallLog binder;
    NetwPropertyConfig::set_carry_binder(binder.callable("bind"));

    config->carry_step(Callable(body, StringName("get_name")));

    NETW_CHECK_EQ(binder.count("bind"), 1);
    const Array bound = binder.args("bind");
    NETW_CHECK_EQ(bound.size(), 3);
    const Variant first = at(bound, 0);
    Object *named = first;
    CHECK(bool(Object::cast_to<Node>(named) == body));
    CHECK(bool(StringName(at(bound, 1)) == StringName("spin")));
    NetwPropertyConfig::set_carry_binder(Callable());
    memdelete(body);
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP8 a step that is invalid, or valid but "
    "bound to something that is not a Node, binds NOTHING and leaves the "
    "config as it was, because there is no body such a rule could advance"
) {
    Ref<RefCounted> loose;
    loose.instantiate();
    const Ref<NetwPropertyConfig> config = make_config();
    const CallLog binder;
    NetwPropertyConfig::set_carry_binder(binder.callable("bind"));

    config->carry_step(Callable());
    config->carry_step(Callable(loose.ptr(), StringName("get_class")));

    NETW_CHECK_EQ(binder.count("bind"), 0);
    CHECK(config->get_carry_channel().is_empty());
    CHECK_FALSE(config->get_in_state_set());
    NetwPropertyConfig::set_carry_binder(Callable());
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP9 carry_along and carry_step are "
    "independent declarations, so a field can name a rate channel without a "
    "rule and a rule without a channel"
) {
    Node *body = memnew(Node);
    const Ref<NetwPropertyConfig> stepped = make_config();
    const Ref<NetwPropertyConfig> carried = make_config();
    const CallLog binder;
    NetwPropertyConfig::set_carry_binder(binder.callable("bind"));

    stepped->carry_step(Callable(body, StringName("get_name")));
    carried->carry_along(StringName("velocity"));

    CHECK(stepped->get_carry_channel().is_empty());
    CHECK(bool(carried->get_carry_channel() == StringName("velocity")));
    NETW_CHECK_EQ(binder.count("bind"), 1);
    NetwPropertyConfig::set_carry_binder(Callable());
    memdelete(body);
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP10 a chain through an inherited verb "
    "answers this very config while its declared type is the base, which is "
    "why a property declaration orders its property-only verbs last"
) {
    Object *emitter = memnew(Object);
    const Ref<NetwPropertyConfig> config = make_config();

    const Ref<NetwMemberConfig> answered
        = config->defer_until(Signal(emitter, StringName("ready")));

    CHECK(
        bool(
            Object::cast_to<NetwPropertyConfig>(answered.ptr()) == config.ptr()
        )
    );
    CHECK(bool(answered->get_class() == String("NetwPropertyConfig")));
    memdelete(emitter);
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP11 the inherited packing declarations "
    "hold on a property and a kind mark leaves them standing, so quantize "
    "and interpolate mean here exactly what they mean on any member"
) {
    const Ref<NetwPropertyConfig> config = make_config();
    config->set_context_name(StringName("position"));
    config->set_context_type(1);
    const Ref<NetwQuantize> packer = make_quantizer();
    const Ref<NetwInterpolate> smoother = make_interpolator(0.25);
    const CallLog reader;
    NetwMemberConfig::set_member_types_reader(
        reader.answering("types", one(int(Variant::VECTOR3)))
    );

    config->quantize(one(packer));
    config->interpolate(one(smoother));
    config->state();

    NETW_CHECK_EQ(config->get_quantizers().size(), 1);
    NETW_CHECK_EQ(config->get_interpolators().size(), 1);
    CHECK(bool(first_quantizer(config->get_quantizers()) == packer));
    NETW_CHECK_EQ(reader.count("types"), 2);
    NETW_CHECK_EQ(int64_t(at(reader.args("types"), 2)), 1);
    CHECK(config->get_in_state_set());
    NetwMemberConfig::set_member_types_reader(Callable());
}

TEST_CASE(
    "[Networked][Session][Hosted] PRP12 every property verb answers the very "
    "config it was called on, which is what lets one _init chain a field's "
    "whole declaration in a single expression"
) {
    const Ref<NetwPropertyConfig> config = make_config();

    CHECK(bool(config->state() == config));
    CHECK(bool(config->input() == config));
    CHECK(bool(config->broadcast() == config));
    CHECK(bool(config->volatile_lane() == config));
    CHECK(bool(config->retained() == config));
    CHECK(bool(config->causal() == config));
    CHECK(bool(config->derived() == config));
    CHECK(bool(config->cosmetic() == config));
    CHECK(bool(config->converge(0.4) == config));
    CHECK(bool(config->carry_along(StringName("velocity")) == config));
    CHECK(bool(config->teleport_only() == config));
    CHECK(bool(config->epsilon(0.05) == config));
    CHECK(bool(config->teleport_at(3.0) == config));
    CHECK(bool(config->reconcile_only() == config));
    CHECK(bool(config->every_tick(0.0) == config));
    CHECK(bool(config->on_change() == config));
    CHECK(bool(config->heartbeat(60) == config));
    CHECK(bool(config->windowed(3) == config));
    CHECK(bool(config->audience(true) == config));
    CHECK(bool(config->masked() == config));
    CHECK(bool(config->persisted(0.0) == config));
    CHECK(bool(config->on_spawn() == config));
}

} // namespace TestNetwPropertyConfig
