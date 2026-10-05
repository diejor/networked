#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED) && defined(DEBUG_ENABLED)

#include <memory>

#include "godot/world.hpp"
#include "netw/diag/sketch.hpp"

using namespace godot;

namespace TestDiagSketchLaws {

using netw::diag::MARK_OURS;
using netw::diag::MARK_SERVER;
using netw::diag::RingRow;
using netw::diag::Sketch;

LocalVector<RingRow> erring_row_in(const Ref<World3D> &p_world) {
    RingRow row;
    row.world = uint64_t(p_world->get_instance_id());
    row.marks = MARK_SERVER | MARK_OURS;
    row.server = Vector3(1.0f, 0.0f, 0.0f);
    row.ours = Vector3(2.0f, 0.0f, 0.0f);
    row.gap = 1.0;
    LocalVector<RingRow> rows;
    rows.push_back(row);
    return rows;
}

TEST_CASE(
    "[Networked][Diag] S1 a sketch frees what it drew into a world once the "
    "world is gone, and everything else when it ends"
) {
    const int64_t before = Sketch::live_rids();
    auto sketch = std::make_unique<Sketch>();
    Ref<World3D> world;
    world.instantiate();
    Ref<World2D> plane;
    plane.instantiate();
    LocalVector<RingRow> rows = erring_row_in(world);
    RingRow flat = rows[0];
    flat.world = uint64_t(plane->get_instance_id());
    flat.flat = true;
    rows.push_back(flat);

    sketch->draw(rows);
    NETW_CHECK_EQ(int64_t(sketch->space_count()), int64_t(2));
    const int64_t drawing = Sketch::live_rids();
    NETW_CHECK_GT(drawing, before);

    world.unref();
    plane.unref();
    sketch->draw(LocalVector<RingRow>());
    NETW_CHECK_EQ(int64_t(sketch->space_count()), int64_t(0));
    NETW_CHECK_EQ(Sketch::live_rids(), drawing - 9);

    sketch.reset();
    NETW_CHECK_EQ(Sketch::live_rids(), before);
}

} // namespace TestDiagSketchLaws

#endif
