#include "support/netw_test.h"

#include "netw/scene_core.hpp"

#include "godot/rid.hpp"
#include "godot/utility.hpp"

namespace TestNetwScenePresentationLaws {

using namespace godot;
using netw::NetwSceneCore;

class Scenes {
    RID_Owner<int> owner;
    LocalVector<RID> minted;

public:
    Scenes() {
        for (int index = 0; index < 4; ++index) {
            minted.push_back(owner.make_rid(index));
        }
    }

    RID operator[](int p_index) {
        REQUIRE(p_index < int(minted.size()));
        return minted[p_index];
    }
};

Ref<NetwSceneCore> fresh() {
    Ref<NetwSceneCore> core;
    core.instantiate();
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP1 a peer that presents nothing answers no "
    "scene however many are live and whatever seat it holds, which is what "
    "lets a dedicated server run every scene and stand in none of them"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;

    core->scene_enter(scenes[0], StringName("Arena"), false);
    core->scene_enter(scenes[1], StringName("Annex"), false);

    CHECK(core->resolve_current(false, RID()) == RID());
    CHECK(core->resolve_current(false, scenes[1]) == RID());

    CHECK(core->resolve_current(true, RID()) == scenes[0]);
    CHECK(core->resolve_current(true, scenes[1]) == scenes[1]);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP2 a held seat is the presentation, so a "
    "host seated in the second of two live scenes presents the one it sits "
    "in rather than the one the live book answers first"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;

    core->scene_enter(scenes[0], StringName("Arena"), false);
    core->scene_enter(scenes[1], StringName("Annex"), false);

    CHECK(core->resolve_current(true, RID()) == scenes[0]);

    CHECK(core->resolve_current(true, scenes[1]) == scenes[1]);
    CHECK(core->resolve_current(true, scenes[0]) == scenes[0]);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP3 an unseated peer presents the first live "
    "scene its own stem still answers with, so the shadowed sibling of a "
    "re-entered stem is passed over rather than presented"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;
    const StringName arena("Arena");

    core->scene_enter(scenes[0], arena, false);

    CHECK(core->scene_named(arena) == scenes[0]);
    CHECK(core->resolve_current(true, RID()) == scenes[0]);

    core->scene_enter(scenes[1], arena, false);

    CHECK(core->scene_named(arena) == scenes[1]);
    CHECK(core->resolve_current(true, RID()) == scenes[1]);

    core->scene_enter(scenes[2], StringName("Annex"), false);

    CHECK(core->resolve_current(true, RID()) == scenes[1]);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP4 a session holding nothing live presents "
    "nothing, so the scene that left the book stops being the answer instead "
    "of lingering as one"
) {
    Ref<NetwSceneCore> core = fresh();
    Scenes scenes;

    CHECK(core->resolve_current(true, RID()) == RID());

    core->scene_enter(scenes[0], StringName("Arena"), false);

    CHECK(core->resolve_current(true, RID()) == scenes[0]);

    core->scene_exit(scenes[0]);

    NETW_CHECK_EQ(core->live_count(), 0);
    CHECK(core->resolve_current(true, RID()) == RID());
}

} // namespace TestNetwScenePresentationLaws
