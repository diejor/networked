#include "support/fault_log.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestNetwLagCompRewindVerbLaws {

constexpr const char *SHOOTER = R"(extends Node

var offset := 0.0

func _init() -> void:
	Netw.configure_property(self, &"offset").state()

func shoot(entities: Array[NetwEntity], tick: int) -> Variant:
	var me := self as Node
	return Netw.rewind(entities, tick, func() -> float: return me.offset)

func write_inside(entities: Array[NetwEntity], tick: int) -> void:
	var me := self as Node
	Netw.rewind(entities, tick, func() -> void: me.offset = 999.0)

func await_inside(entities: Array[NetwEntity], tick: int) -> void:
	Netw.rewind(entities, tick, func() -> void:
		await Engine.get_main_loop().process_frame)
)";

constexpr const char *TURRET = R"(extends Node3D

var offset := 0.0:
	set(value):
		offset = value
		rotation_degrees = Vector3(value * 13.0, value * 37.0, value * 7.0)

func _init() -> void:
	Netw.configure_property(self, &"position").state()
	Netw.configure_property(self, &"quaternion").state()

func look(entities: Array[NetwEntity], tick: int) -> void:
	Netw.rewind(entities, tick, func() -> void: pass)
)";

constexpr int TICKS = 10;
constexpr int64_t AGO = 5;

Script *shooter_script = nullptr;
int bodies_called = 0;

Node *build_shooter(const Variant &p_name) {
    Node *made = Object::cast_to<Node>(shooter_script->call("new"));
    made->set_name(String(p_name));
    return made;
}

void count_body() {
    bodies_called += 1;
}

struct Range {
    Ref<Script> script;
    netw_test::SimStand stand{0};
    Node *shooter = nullptr;
    TypedArray<netw::NetwEntity> entities;
    int64_t past = -1;

    explicit Range(const char *p_source = SHOOTER) {
        script = netw_test::minted_script(p_source);
        REQUIRE(script.is_valid());
        shooter_script = script.ptr();
        REQUIRE(stand.ready());
        stand.teach(StringName("shooter"), callable_mp_static(&build_shooter));
        stand.mount();
        Node *arena = stand.arena();
        stand.arm(30);
        netw::NetwMultiplayer *server = stand.session(-1);
        REQUIRE(server->lagcomp_initialize(8, 12) == OK);
        Array args;
        args.push_back(String("shooter"));
        const RID made
            = server->spawn_registered(StringName("shooter"), args, nullptr);
        shooter = server->entity_get_node(made);
        REQUIRE(shooter != nullptr);
        arena->add_child(shooter);
        stand.pump(4);
        REQUIRE(server->lagcomp_timeline_declare(made) == OK);
        for (int tick = 1; tick <= TICKS; ++tick) {
            shooter->set(StringName("offset"), double(tick));
            stand.step_ticks(1);
        }
        past = server->clock_get_tick() - AGO;
        entities.push_back(server->entity_get_view(made));
    }

    ~Range() {
        shooter_script = nullptr;
    }

    double offset() const {
        return double(shooter->get(StringName("offset")));
    }
};

TEST_CASE(
    "[Networked][LagComp] K-RW4 Netw.rewind with no entities is an error "
    "and never calls its body"
) {
    const netw_test::FaultWatch faults;
    bodies_called = 0;
    const Variant answered = netw::Netw::rewind(
        TypedArray<netw::NetwEntity>(),
        1,
        callable_mp_static(&count_body)
    );
    NETW_CHECK_EQ(bodies_called, 0);
    NETW_CHECK_EQ(int(answered.get_type()), int(Variant::NIL));
    NETW_CHECK_EQ(faults.errors_containing("got no entities"), int64_t(1));
}

TEST_CASE(
    "[Networked][LagComp] K-RW5 a lambda that does not use self runs inside "
    "Netw.rewind and its return value comes back"
) {
    Range range;
    const Variant answered
        = range.shooter->call(StringName("shoot"), range.entities, range.past);
    NETW_CHECK_EQ(int(answered.get_type()), int(Variant::FLOAT));
    NETW_CHECK_LT(double(answered), double(TICKS));
    NETW_CHECK_GT(double(answered), 0.0);
    NETW_CHECK_CLOSE(range.offset(), double(TICKS), 0.0001);
}

TEST_CASE(
    "[Networked][LagComp] K-RW7 a declared value written inside a rewind "
    "body warns once that the rewind put it back"
) {
    Range range;
    const netw_test::FaultWatch faults;
    for (int shot = 0; shot < 2; ++shot) {
        range.shooter
            ->call(StringName("write_inside"), range.entities, range.past);
    }
    NETW_CHECK_CLOSE(range.offset(), double(TICKS), 0.0001);
    NETW_CHECK_EQ(
        faults.warnings_containing("changed inside a rewind"),
        int64_t(1)
    );
}

TEST_CASE(
    "[Networked][LagComp] K-RW8 a rewind body that writes nothing does not "
    "warn about a quaternion the node reads back from its basis"
) {
    Range range(TURRET);
    const netw_test::FaultWatch faults;
    range.shooter->call(StringName("look"), range.entities, range.past);
    NETW_CHECK_EQ(
        faults.warnings_containing("changed inside a rewind"),
        int64_t(0)
    );
}

TEST_CASE(
    "[Networked][LagComp] a rewind body that awaits warns that the rest of it "
    "ran after the rewind"
) {
    Range range;
    const netw_test::FaultWatch faults;
    range.shooter->call(StringName("await_inside"), range.entities, range.past);
    NETW_CHECK_EQ(
        faults.warnings_containing("rewind body awaited"),
        int64_t(1)
    );
    NETW_CHECK_CLOSE(range.offset(), double(TICKS), 0.0001);
}

} // namespace TestNetwLagCompRewindVerbLaws

#endif
