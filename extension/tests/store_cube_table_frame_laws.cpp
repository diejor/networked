#include "support/frame_drive.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include <string>
#include <vector>

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/quantize.hpp"
#include "netw/store/world.hpp"

using namespace godot;

namespace TestNetwStoreCubeTable {

using netw::NetwMultiplayer;
using netw::store::Family;
using netw::store::Port;
using netw::store::Table;
using netw::store::World;

constexpr const char *CUBE_ID = "store_cube";
constexpr const char *CUBE_SCENE = "res://examples/playground/cube.tscn";
constexpr int TICKRATE = 30;
constexpr int FRAMES = 4;

struct ColumnSeen {
    std::string key;
    std::string quantizer;
    int bits = 0;
    int port = -1;
};

struct CubeEvidence {
    bool driven = false;
    int tables = 0;
    int rows = 0;
    int family = -1;
    bool placed = false;
    std::vector<ColumnSeen> columns;
};

CubeEvidence &cube_evidence() {
    static CubeEvidence evidence;
    return evidence;
}

PackedScene *cube_scene = nullptr;

Node *build_cube(const Variant &p_name) {
    if (cube_scene == nullptr) {
        return nullptr;
    }
    Node *made = cube_scene->instantiate();
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

std::string text_of(const String &p_text) {
    return std::string(p_text.utf8().get_data());
}

class CubeTableScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<PackedScene> scene;
    LocalVector<RID> cubes;

    NetwMultiplayer *server() const {
        return stand->session(-1);
    }

    bool open() {
        scene = ResourceLoader::get_singleton()->load(CUBE_SCENE);
        if (scene.is_null()) {
            return false;
        }
        cube_scene = scene.ptr();
        stand = new netw_test::SimStand(0);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(CUBE_ID), callable_mp_static(&build_cube));
        stand->mount();
        stand->arm(TICKRATE);
        Node *arena = stand->arena();
        Array args;
        args.push_back(String("cube0"));
        const RID cube
            = server()->spawn_registered(StringName(CUBE_ID), args, nullptr);
        cubes.push_back(cube);
        Node *built = server()->entity_get_node(cube);
        if (built == nullptr) {
            return false;
        }
        arena->add_child(built);
        stand->pump(4);
        return true;
    }

    void read() {
        CubeEvidence &seen = cube_evidence();
        const World &world = server()->store();
        seen.tables = int(world.table_count());
        if (world.table_count() != 1) {
            return;
        }
        const Table &table = world.table(0);
        seen.rows = int(table.row_count());
        seen.family = int(table.family);
        if (table.row_count() == 1) {
            const int64_t route
                = server()->get_liveness_core()->route_of(cubes[0]);
            seen.placed = route > 0 && world.route_of(table.slots[0]) == route;
        }
        for (int at = 0; at < table.schema.column_count(); ++at) {
            const netw::table::SchemaColumn *column = table.schema.at(at);
            ColumnSeen held;
            held.key = text_of(String(column->key));
            held.port = int(table.columns[uint32_t(at)].port);
            if (column->quantizer.is_valid()) {
                held.quantizer = text_of(column->quantizer->get_class());
                held.bits = table.plan.column(uint32_t(at)).width;
            }
            seen.columns.push_back(held);
        }
    }

    void close() {
        cube_evidence().driven = true;
        delete stand;
        stand = nullptr;
        cube_scene = nullptr;
        scene = Ref<PackedScene>();
        cubes.clear();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            close();
            done = true;
            return false;
        }
        if (frame == FRAMES) {
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

NETW_FRAME_SCENARIO(CubeTableScenario, store_cube_table_scenario);

TEST_CASE(
    "[Networked][Store][Frame] W4 the playground cube's table holds its five "
    "declared columns in declared order with their declared quantizers, read "
    "through the body port, and its row names the cube's route"
) {
    const CubeEvidence &seen = cube_evidence();
    REQUIRE(seen.driven);
    NETW_CHECK_EQ(seen.tables, 1);
    NETW_CHECK_EQ(seen.rows, 1);
    NETW_CHECK_EQ(seen.family, int(Family::VOLATILE));
    CHECK(seen.placed);
    REQUIRE(seen.columns.size() == 5);
    const char *keys[5] = {
        "position",
        "quaternion",
        "linear_velocity",
        "angular_velocity",
        "sleeping",
    };
    const char *grids[5] = {
        "NetwQuantizeScalar",
        "NetwQuantizeQuaternion",
        "NetwQuantizeScalar",
        "NetwQuantizeScalar",
        "",
    };
    const int bits[5] = {20, 50, 16, 16, 0};
    for (int at = 0; at < 5; ++at) {
        CHECK(bool(seen.columns[at].key == keys[at]));
        CHECK(bool(seen.columns[at].quantizer == grids[at]));
        NETW_CHECK_EQ(seen.columns[at].bits, bits[at]);
        NETW_CHECK_EQ(seen.columns[at].port, int(Port::BODY));
    }
}

} // namespace TestNetwStoreCubeTable

#endif
