#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/script.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/quantize.hpp"
#include "netw/entity/control.hpp"
#include "netw/member_packing.hpp"
#include "support/declared_nodes.h"
#include "support/minted_script.h"
#include "support/netw_call_log.h"

namespace TestNetwMemberConfig {

using namespace godot;
using netw::NetwInterpolate;
using netw::NetwMemberConfig;
using netw::NetwQuantize;
using netw::NetwQuantizeScalar;
using netw::entity::Control;
using netw_test::CallLog;

Ref<NetwMemberConfig> make_config() {
    Ref<NetwMemberConfig> config;
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

Ref<NetwInterpolate> first_interpolator(const Array &p_list) {
    return p_list.is_empty() ? Ref<NetwInterpolate>()
                             : Ref<NetwInterpolate>(p_list[0]);
}

Array one(const Variant &p_value) {
    Array list;
    list.push_back(p_value);
    return list;
}

Array pair(const Variant &p_first, const Variant &p_second) {
    Array list;
    list.push_back(p_first);
    list.push_back(p_second);
    return list;
}

int64_t book_policy(
    const Ref<Script> &p_script,
    const StringName &p_name,
    bool p_is_signal
) {
    const StringName key
        = p_is_signal ? Control::emit_book_key() : Control::write_book_key();
    if (p_script.is_null() || !p_script->has_meta(key)) {
        return -1;
    }
    const Dictionary book = p_script->get_meta(key);
    return book.has(p_name) ? int64_t(book[p_name]) : -1;
}

TEST_CASE(
    "[Networked][Session][Hosted] MC1 a config nobody declared anything on "
    "is authority-written, reliable, call-remote, ungated and unpacked, so "
    "a member registered and never chained travels the safe way by default"
) {
    const Ref<NetwMemberConfig> config = make_config();

    NETW_CHECK_EQ(
        int(config->get_write_policy()),
        int(NetwMemberConfig::POLICY_AUTHORITY)
    );
    NETW_CHECK_EQ(
        int(config->get_transfer_mode()),
        int(NetwMemberConfig::TRANSFER_RELIABLE)
    );
    CHECK_FALSE(config->get_is_call_local());
    CHECK_FALSE(config->get_is_controller_only());
    CHECK(config->get_defer_signal_name().is_empty());
    CHECK(config->get_quantizers().is_empty());
    CHECK(config->get_interpolators().is_empty());
    CHECK_FALSE(config->is_policy_declared());
    CHECK_FALSE(config->is_transfer_declared());
    CHECK_FALSE(config->is_interpolation_only());
}

TEST_CASE(
    "[Networked][Session][Hosted] MC2 declaring an axis its own default value "
    "still counts as a declaration, so an author who wrote authority() or "
    "reliable() out loud is not read as an author who wrote nothing"
) {
    const Ref<NetwMemberConfig> policed = make_config();
    policed->interpolate(one(make_interpolator(0.1)));
    policed->authority();

    const Ref<NetwMemberConfig> transferred = make_config();
    transferred->interpolate(one(make_interpolator(0.1)));
    transferred->reliable();

    CHECK(policed->is_policy_declared());
    CHECK(transferred->is_transfer_declared());
    CHECK_FALSE(policed->is_interpolation_only());
    CHECK_FALSE(transferred->is_interpolation_only());
}

TEST_CASE(
    "[Networked][Session][Hosted] MC3 a config carrying interpolators alone "
    "is interpolation-only, and one quantizer ends that, because smoothing a "
    "value another set already replicates is not a second claim on it"
) {
    const Ref<NetwMemberConfig> smoothing = make_config();
    smoothing->interpolate(one(make_interpolator(0.1)));

    CHECK(smoothing->is_interpolation_only());

    smoothing->quantize(one(make_quantizer()));

    CHECK_FALSE(smoothing->is_interpolation_only());
}

TEST_CASE(
    "[Networked][Session][Hosted] MC4 re-declaring interpolators of the same "
    "spec keeps the list the config already holds, so an authoring shell that "
    "pushes freshly built but identical specs changes nothing"
) {
    const Ref<NetwMemberConfig> config = make_config();
    const Ref<NetwInterpolate> declared = make_interpolator(0.25);
    config->interpolate(one(declared));

    config->interpolate(one(make_interpolator(0.25)));

    NETW_CHECK_EQ(config->get_interpolators().size(), 1);
    CHECK(bool(first_interpolator(config->get_interpolators()) == declared));
}

TEST_CASE(
    "[Networked][Session][Hosted] MC5 an interpolator re-declared with a "
    "different spec replaces the standing one, which is what makes MC4's "
    "silence a statement about equality rather than about the first writer"
) {
    const Ref<NetwMemberConfig> config = make_config();
    config->interpolate(one(make_interpolator(0.25)));
    const Ref<NetwInterpolate> replacement = make_interpolator(0.5);

    config->interpolate(one(replacement));

    NETW_CHECK_EQ(config->get_interpolators().size(), 1);
    CHECK(bool(first_interpolator(config->get_interpolators()) == replacement));
}

TEST_CASE(
    "[Networked][Session][Hosted] MC6 an argument that is not a quantizer "
    "leaves the declared list standing, so a typo in one chained call cannot "
    "silently unpack a member that was already declared"
) {
    const Ref<NetwMemberConfig> config = make_config();
    const Ref<NetwQuantize> packer = make_quantizer();
    config->quantize(one(packer));

    config->quantize(one(Variant(7)));

    NETW_CHECK_EQ(config->get_quantizers().size(), 1);
    CHECK(bool(first_quantizer(config->get_quantizers()) == packer));
}

TEST_CASE(
    "[Networked][Session][Hosted] MC7 every fluent verb answers the very "
    "config it was called on, which is what lets one _init chain a member's "
    "whole declaration in a single expression"
) {
    const Ref<NetwMemberConfig> config = make_config();

    CHECK(bool(config->authority() == config));
    CHECK(bool(config->controller() == config));
    CHECK(bool(config->any_peer() == config));
    CHECK(bool(config->reliable() == config));
    CHECK(bool(config->unreliable() == config));
    CHECK(bool(config->call_local() == config));
    CHECK(bool(config->call_remote() == config));
    CHECK(bool(config->controller_only() == config));
    CHECK(bool(config->quantize(one(make_quantizer())) == config));
    CHECK(bool(config->interpolate(one(make_interpolator(0.1))) == config));
    CHECK(bool(config->quantize(Array()) == config));
}

TEST_CASE(
    "[Networked][Session][Hosted] MC8 controller_only and defer_until write "
    "the two facts a receiver reads before it runs a call, and the gate is "
    "the signal's name rather than the Signal itself"
) {
    Object *emitter = memnew(Object);
    const Ref<NetwMemberConfig> config = make_config();

    config->controller_only();
    config->defer_until(Signal(emitter, StringName("ready")));

    CHECK(config->get_is_controller_only());
    CHECK(bool(config->get_defer_signal_name() == StringName("ready")));
    memdelete(emitter);
}

TEST_CASE(
    "[Networked][Session][Hosted] MC9 the installed types reader is asked "
    "with the member's discriminator and its opaque node reference, because "
    "resolving a declared type is GDScript reflection this class cannot do"
) {
    const Ref<NetwMemberConfig> config = make_config();
    config->set_context_name(StringName("position"));
    config->set_context_type(1);
    Ref<RefCounted> node_ref;
    node_ref.instantiate();
    config->set_context_node_ref(node_ref);
    const CallLog reader;
    NetwMemberConfig::set_member_types_reader(
        reader.answering("types", one(int(Variant::STRING)))
    );

    config->quantize(one(make_quantizer()));

    NETW_CHECK_EQ(reader.count("types"), 1);
    const Array asked = reader.args("types");
    NETW_CHECK_EQ(asked.size(), 4);
    CHECK(bool(StringName(at(asked, 1)) == StringName("position")));
    NETW_CHECK_EQ(int64_t(at(asked, 2)), 1);
    CHECK(bool(Ref<RefCounted>(at(asked, 3)) == node_ref));
    NETW_CHECK_EQ(config->get_quantizers().size(), 1);

    NetwMemberConfig::set_member_types_reader(Callable());
}

TEST_CASE(
    "[Networked][Session][Hosted] MC10 the shared packing rule refuses a "
    "quantizer that cannot pack the declared type and accepts an empty slot "
    "or an unknown type, which is the one rule NetwJoinConfig also enforces"
) {
    const Ref<NetwQuantize> packer = make_quantizer();

    CHECK_FALSE(
        netw::quantizers_fit_types(
            one(packer),
            one(int(Variant::STRING)),
            StringName("position"),
            Ref<Script>()
        )
    );
    CHECK(
        netw::quantizers_fit_types(
            one(packer),
            one(int(Variant::VECTOR3)),
            StringName("position"),
            Ref<Script>()
        )
    );
    CHECK(
        netw::quantizers_fit_types(
            one(packer),
            one(int(Variant::NIL)),
            StringName("position"),
            Ref<Script>()
        )
    );
    CHECK(
        netw::quantizers_fit_types(
            one(Variant()),
            one(int(Variant::STRING)),
            StringName("position"),
            Ref<Script>()
        )
    );
    CHECK(
        netw::quantizers_fit_types(
            one(packer),
            Array(),
            StringName("position"),
            Ref<Script>()
        )
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] MC11 the shared smoothing rule refuses an "
    "interpolator over a type it cannot lerp, read off the same parallel type "
    "list the quantizer rule reads"
) {
    const Ref<NetwInterpolate> smoother = make_interpolator(0.1);

    CHECK_FALSE(
        netw::interpolators_fit_types(
            one(smoother),
            one(int(Variant::STRING)),
            StringName("position"),
            Ref<Script>()
        )
    );
    CHECK(
        netw::interpolators_fit_types(
            one(smoother),
            one(int(Variant::VECTOR3)),
            StringName("position"),
            Ref<Script>()
        )
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] MC12 a member taking two arguments wants "
    "two declarations, one per argument, and refuses a short list rather "
    "than packing argument one and leaving argument two to guess"
) {
    const Ref<NetwMemberConfig> config = make_config();
    config->set_context_name(StringName("aim"));
    const CallLog reader;
    NetwMemberConfig::set_member_types_reader(reader.answering(
        "types",
        pair(int(Variant::VECTOR3), int(Variant::VECTOR3))
    ));

    config->quantize(pair(make_quantizer(), Variant()));
    NETW_CHECK_EQ(config->get_quantizers().size(), 2);

    config->quantize(one(make_quantizer()));

    NETW_CHECK_EQ(config->get_quantizers().size(), 2);
    CHECK(bool(at(config->get_quantizers(), 1).get_type() == Variant::NIL));
    NetwMemberConfig::set_member_types_reader(Callable());
}

TEST_CASE(
    "[Networked][Session][Hosted] MC13 a single declaration is the whole list "
    "for a member taking one argument, which is why the common case never "
    "writes an arity out"
) {
    const Ref<NetwMemberConfig> config = make_config();
    config->set_context_name(StringName("position"));
    const CallLog reader;
    NetwMemberConfig::set_member_types_reader(
        reader.answering("types", one(int(Variant::VECTOR3)))
    );

    config->interpolate(one(make_interpolator(0.1)));

    NETW_CHECK_EQ(config->get_interpolators().size(), 1);
    NetwMemberConfig::set_member_types_reader(Callable());
}

TEST_CASE(
    "[Networked][Session][Hosted] MC14 a member whose declaration cannot be "
    "read takes the list on trust, because refusing what it cannot check "
    "would ground every config no script backs, but a list of nothing is "
    "refused whether the arity is readable or not"
) {
    const Ref<NetwMemberConfig> config = make_config();

    config->quantize(pair(make_quantizer(), make_quantizer()));
    config->interpolate(one(make_interpolator(0.1)));

    NETW_CHECK_EQ(config->get_quantizers().size(), 2);

    config->quantize(Array());
    config->interpolate(Array());

    NETW_CHECK_EQ(config->get_quantizers().size(), 2);
    NETW_CHECK_EQ(config->get_interpolators().size(), 1);
}

#if defined(NETW_TIER_HOSTED)

const char *MEMBER_SCRIPT = netw_test::gdsrc::A_PLAIN_SCRIPT;

Ref<Script> a_script() {
    return netw_test::script_from(MEMBER_SCRIPT);
}

TEST_CASE(
    "[Networked][Session][Declared] MC15 declaring a policy on a property or a "
    "signal compiles it into that script's own book, so the receive gate "
    "reads the rule off the script rather than walking the registry per frame"
) {
    const Ref<Script> script = a_script();
    const Ref<NetwMemberConfig> property = make_config();
    property->set_context_script(script);
    property->set_context_name(StringName("netw_mc_field"));
    property->set_context_type(1);
    const Ref<NetwMemberConfig> emitted = make_config();
    emitted->set_context_script(script);
    emitted->set_context_name(StringName("netw_mc_signal"));
    emitted->set_context_type(2);

    property->controller();
    emitted->any_peer();

    NETW_CHECK_EQ(
        book_policy(script, StringName("netw_mc_field"), false),
        int64_t(NetwMemberConfig::POLICY_CONTROLLER)
    );
    NETW_CHECK_EQ(
        book_policy(script, StringName("netw_mc_signal"), true),
        int64_t(NetwMemberConfig::POLICY_ANY_PEER)
    );
    NETW_CHECK_EQ(book_policy(script, StringName("netw_mc_field"), true), -1);
}

TEST_CASE(
    "[Networked][Session][Declared] MC16 an RPC config and a config naming no "
    "script publish nothing, because an RPC is policed by its own caller gate "
    "and a scriptless declaration has no book to be compiled into"
) {
    const Ref<Script> script = a_script();
    const Ref<NetwMemberConfig> called = make_config();
    called->set_context_script(script);
    called->set_context_name(StringName("netw_mc_method"));
    called->set_context_type(0);
    const Ref<NetwMemberConfig> scriptless = make_config();
    scriptless->set_context_name(StringName("netw_mc_loose"));
    scriptless->set_context_type(1);

    called->any_peer();
    scriptless->any_peer();

    NETW_CHECK_EQ(book_policy(script, StringName("netw_mc_method"), false), -1);
    NETW_CHECK_EQ(book_policy(script, StringName("netw_mc_loose"), false), -1);
}

#endif

} // namespace TestNetwMemberConfig
