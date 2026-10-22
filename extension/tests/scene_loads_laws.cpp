#include "support/netw_test.h"

#include "godot/packed_scene.hpp"
#include "netw/scene_loads.hpp"

namespace TestNetwSceneLoadsLaws {

using namespace godot;
using netw::SceneLoader;
using netw::SceneLoads;
using netw::gd::LoadStatus;

const char *const LEVEL = "res://levels/arena.tscn";

struct LoaderScript {
    int loading_answers = 0;
    LoadStatus settles = netw::gd::LOAD_LOADED;
    int requests = 0;
    int asked = 0;
    int collects = 0;
};

LoaderScript script;

Error scripted_request(const String &) {
    script.requests += 1;
    return OK;
}

LoadStatus scripted_status(const String &) {
    script.asked += 1;
    if (script.loading_answers > 0) {
        script.loading_answers -= 1;
        return netw::gd::LOAD_IN_PROGRESS;
    }
    return script.settles;
}

Ref<PackedScene> scripted_collect(const String &) {
    script.collects += 1;
    Ref<PackedScene> scene;
    if (script.settles == netw::gd::LOAD_LOADED) {
        scene.instantiate();
    }
    return scene;
}

SceneLoader scripted(int p_loading_answers, LoadStatus p_settles) {
    script = LoaderScript();
    script.loading_answers = p_loading_answers;
    script.settles = p_settles;
    return SceneLoader{&scripted_request, &scripted_status, &scripted_collect};
}

TEST_CASE(
    "[Networked][Scene][Hosted] LD1 a cached scene is ready at the call, and "
    "a scene the book holds asks the loader nothing"
) {
    SceneLoads loads(scripted(0, netw::gd::LOAD_LOADED));

    NETW_CHECK_EQ(int(loads.begin(LEVEL)), int(SceneLoads::STATUS_READY));
    CHECK(loads.scene(LEVEL).is_valid());
    NETW_CHECK_EQ(script.requests, 1);
    NETW_CHECK_EQ(script.collects, 1);

    loads.hold(LEVEL);
    NETW_CHECK_EQ(int(loads.begin(LEVEL)), int(SceneLoads::STATUS_READY));
    NETW_CHECK_EQ(script.requests, 1);
    NETW_CHECK_EQ(script.asked, 1);
}

struct SettleRow {
    const char *name;
    int loading_polls;
    LoadStatus settles;
    SceneLoads::Status reads;
    bool has_scene;
};

TEST_CASE(
    "[Networked][Scene][Hosted] LD2 LD3 a load settles on the poll its loader "
    "finishes, and is collected exactly once"
) {
    const SettleRow rows[] = {
        {"loaded after 3 polls",
         3,
         netw::gd::LOAD_LOADED,
         SceneLoads::STATUS_READY,
         true},
        {"failed", 0, netw::gd::LOAD_FAILED, SceneLoads::STATUS_FAILED, false},
    };
    for (const SettleRow &row : rows) {
        INFO(row.name);
        SceneLoads loads(scripted(1 + row.loading_polls, row.settles));

        NETW_CHECK_EQ(int(loads.begin(LEVEL)), int(SceneLoads::STATUS_LOADING));
        loads.hold(LEVEL);
        uint64_t frame = 1;
        for (int poll = 0; poll < row.loading_polls; ++poll) {
            loads.poll(frame++);
            NETW_CHECK_EQ(
                int(loads.status(LEVEL)),
                int(SceneLoads::STATUS_LOADING)
            );
        }
        NETW_CHECK_EQ(script.collects, 0);

        loads.poll(frame++);
        NETW_CHECK_EQ(int(loads.status(LEVEL)), int(row.reads));
        NETW_CHECK_EQ(loads.scene(LEVEL).is_valid(), row.has_scene);

        loads.poll(frame++);
        loads.poll(frame++);
        NETW_CHECK_EQ(int(loads.status(LEVEL)), int(row.reads));
        NETW_CHECK_EQ(script.requests, 1);
        NETW_CHECK_EQ(script.collects, 1);
    }
}

TEST_CASE(
    "[Networked][Scene][Hosted] LD4 two waits on one path share one request "
    "and one collect"
) {
    SceneLoads loads(scripted(2, netw::gd::LOAD_LOADED));

    NETW_CHECK_EQ(int(loads.begin(LEVEL)), int(SceneLoads::STATUS_LOADING));
    loads.hold(LEVEL);
    NETW_CHECK_EQ(int(loads.begin(LEVEL)), int(SceneLoads::STATUS_LOADING));
    loads.hold(LEVEL);
    loads.poll(1);
    loads.poll(2);
    loads.poll(3);

    NETW_CHECK_EQ(int(loads.status(LEVEL)), int(SceneLoads::STATUS_READY));
    NETW_CHECK_EQ(script.requests, 1);
    NETW_CHECK_EQ(script.collects, 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] LD5 a scene stays loaded while anything holds "
    "it, and a later begin after the last release loads it again"
) {
    SceneLoads loads(scripted(0, netw::gd::LOAD_LOADED));
    loads.begin(LEVEL);
    loads.hold(LEVEL);
    loads.hold(LEVEL);
    const Ref<PackedScene> scene = loads.scene(LEVEL);

    loads.release(LEVEL);
    loads.poll(1);
    CHECK(loads.scene(LEVEL) == scene);

    loads.release(LEVEL);
    NETW_CHECK_EQ(scene->get_reference_count(), 1);
    NETW_CHECK_EQ(int(loads.status(LEVEL)), int(SceneLoads::STATUS_NONE));

    NETW_CHECK_EQ(int(loads.begin(LEVEL)), int(SceneLoads::STATUS_READY));
    NETW_CHECK_EQ(script.requests, 2);
}

TEST_CASE(
    "[Networked][Scene][Hosted] LD6 the book is polled once a frame, however "
    "many sessions poll it"
) {
    SceneLoads loads(scripted(10, netw::gd::LOAD_LOADED));
    loads.begin(LEVEL);
    loads.hold(LEVEL);
    NETW_CHECK_EQ(script.asked, 1);

    loads.poll(7);
    loads.poll(7);
    NETW_CHECK_EQ(script.asked, 2);

    loads.poll(8);
    NETW_CHECK_EQ(script.asked, 3);
}

TEST_CASE(
    "[Networked][Scene][Hosted] LD7 a load nobody holds any more is still "
    "collected once, and then forgotten"
) {
    SceneLoads loads(scripted(2, netw::gd::LOAD_LOADED));
    loads.begin(LEVEL);
    loads.hold(LEVEL);
    loads.release(LEVEL);

    loads.poll(1);
    NETW_CHECK_EQ(int(loads.status(LEVEL)), int(SceneLoads::STATUS_LOADING));
    loads.poll(2);
    loads.poll(3);

    NETW_CHECK_EQ(script.collects, 1);
    NETW_CHECK_EQ(int(loads.status(LEVEL)), int(SceneLoads::STATUS_NONE));
}

} // namespace TestNetwSceneLoadsLaws
