#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/alloc_probe.h"
#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace TestNetwStoreReceive {

using netw::NetwMultiplayer;

constexpr const char *CUBE_ID = "store_receive_cube";

constexpr const char *CUBE_SOURCE = R"(extends RigidBody3D

func _init() -> void:
	gravity_scale = 0.0
	collision_layer = 0
	collision_mask = 0
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	add_child(shape)
	Netw.configure_entity(self)
	Netw.configure_property(self, &"position").broadcast().heartbeat(2)
	Netw.configure_property(self, &"quaternion").broadcast().heartbeat(2)
	Netw.configure_property(self, &"linear_velocity").broadcast() \
			.heartbeat(2)
)";

constexpr const char *DECODING_SOURCE = R"(extends NetwMultiplayer

var decoded := 0

func _sync_decode(_entity: RID, _comp: int, _flags: int, _tick: int,
		_payload: PackedByteArray) -> Error:
	decoded += 1
	return ERR_INVALID_DATA
)";

constexpr int PILE = 8;
constexpr int TICKRATE = 30;
constexpr int SETTLE_FRAMES = 60;
constexpr int WINDOW = 60;

Script *cube_script = nullptr;

Node *build_cube(const Variant &p_name) {
    if (cube_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(cube_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct ReceiveEvidence {
    bool driven = false;
    int64_t received = 0;
    int64_t staged = 0;
    int64_t decoded = 0;
    int64_t allocations = 0;
};

ReceiveEvidence &receive_evidence(bool p_decoding) {
    static ReceiveEvidence evidence[2];
    return evidence[p_decoding ? 1 : 0];
}

class ReceiveScenario final : public netw_test::FrameScenario {
    const bool decoding;
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    Ref<Script> session;
    netw_test::AllocProbe probe;
    NetwMultiplayer::IdleWork opened;
    int64_t decoded = 0;

    NetwMultiplayer *server() const {
        return stand->session(-1);
    }

    NetwMultiplayer *client() const {
        return stand->session(0);
    }

    int64_t decoded_now() const {
        return decoding ? int64_t(client()->get("decoded")) : 0;
    }

    bool open() {
        script = netw_test::minted_script(CUBE_SOURCE);
        if (script.is_null()) {
            return false;
        }
        cube_script = script.ptr();
        stand = new netw_test::SimStand(1);
        if (!stand->ready()) {
            return false;
        }
        if (decoding) {
            session = netw_test::minted_script(DECODING_SOURCE);
            if (session.is_null()) {
                return false;
            }
            client()->set_script(session);
        }
        stand->teach(StringName(CUBE_ID), callable_mp_static(&build_cube));
        stand->mount();
        stand->arm(TICKRATE);
        Node *arena = stand->arena();
        for (int index = 0; index < PILE; ++index) {
            Array args;
            args.push_back(String("received") + String::num_int64(index));
            const RID made = server()->spawn_registered(
                StringName(CUBE_ID),
                args,
                nullptr
            );
            Node *built = server()->entity_get_node(made);
            if (built == nullptr) {
                return false;
            }
            arena->add_child(built);
        }
        stand->pump(8);
        return true;
    }

    void read() {
        ReceiveEvidence &seen = receive_evidence(decoding);
        const NetwMultiplayer::IdleWork &now = client()->idle_work;
        seen.received = now.received_rows - opened.received_rows;
        seen.staged = now.decode_stages - opened.decode_stages;
        seen.allocations = now.receive_allocations - opened.receive_allocations;
        seen.decoded = decoded_now() - decoded;
    }

    void close() {
        receive_evidence(decoding).driven = true;
        delete stand;
        stand = nullptr;
        cube_script = nullptr;
        script = Ref<Script>();
        session = Ref<Script>();
    }

public:
    explicit ReceiveScenario(bool p_decoding) : decoding(p_decoding) {
    }

    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            close();
            done = true;
            return false;
        }
        if (frame == SETTLE_FRAMES) {
            client()->store().validate_every = 0;
            decoded = decoded_now();
            probe.arm();
            opened = client()->idle_work;
        }
        if (frame == SETTLE_FRAMES + WINDOW) {
            probe.disarm();
            read();
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(ReceiveScenario, store_receive_scenario, false);
NETW_FRAME_SCENARIO(ReceiveScenario, store_receive_decoding_scenario, true);

TEST_CASE(
    "[Networked][Store][Frame] R1 a received row runs the decode stage only "
    "when the session's script overrides it, and then for every row that "
    "arrives"
) {
    const ReceiveEvidence &plain = receive_evidence(false);
    const ReceiveEvidence &decoding = receive_evidence(true);
    REQUIRE(plain.driven);
    REQUIRE(decoding.driven);
    NETW_CHECK_GT(plain.received, int64_t(PILE));
    NETW_CHECK_EQ(plain.staged, int64_t(0));
    NETW_CHECK_GT(decoding.staged, int64_t(PILE));
    NETW_CHECK_EQ(decoding.decoded, decoding.staged);
    NETW_CHECK_EQ(decoding.received, int64_t(0));
}

TEST_CASE(
    "[Networked][Store][Frame] R2 a steady stream of received rows, "
    "interpolated and landed on bodies, allocates through the extension's "
    "allocator at most once in every four rows"
) {
    const ReceiveEvidence &seen = receive_evidence(false);
    REQUIRE(seen.driven);
    NETW_CHECK_GT(seen.received, int64_t(PILE));
    NETW_CHECK_LE(seen.allocations * 4, seen.received);
}

} // namespace TestNetwStoreReceive

#endif
