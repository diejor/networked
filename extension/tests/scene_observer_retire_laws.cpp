#include "support/netw_test.h"

#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

#include "godot/rid.hpp"

namespace TestSceneObserverRetireLaws {

using namespace godot;
using netw::NetwSceneCore;
using netw_test::CallLog;

TEST_CASE(
    "[Networked][Scene][Hosted] SO1 an observer answering true is dropped and "
    "hears nothing after, so a live observer can finish without holding an "
    "identity the registry would have to match it by"
) {
    Ref<NetwSceneCore> scenes;
    scenes.instantiate();
    RID_Owner<int> owner;
    const RID arena = owner.make_rid(0);
    const CallLog heard;
    scenes->observe(
        arena,
        NetwSceneCore::EVENT_PLAYER,
        heard.answering("done", true)
    );

    NETW_CHECK_EQ(
        scenes->dispatch(arena, NetwSceneCore::EVENT_PLAYER, true, Variant()),
        0
    );
    NETW_CHECK_EQ(heard.count("done"), 1);
    NETW_CHECK_EQ(
        scenes->observer_count(arena, NetwSceneCore::EVENT_PLAYER),
        0
    );

    scenes->dispatch(arena, NetwSceneCore::EVENT_PLAYER, true, Variant());

    NETW_CHECK_EQ(heard.count("done"), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SO2 an observer answering anything else is "
    "kept, so a callback that returns nothing is an ordinary subscriber "
    "rather than one that unsubscribes on its first edge"
) {
    Ref<NetwSceneCore> scenes;
    scenes.instantiate();
    RID_Owner<int> owner;
    const RID arena = owner.make_rid(0);
    const CallLog heard;
    scenes->observe(arena, NetwSceneCore::EVENT_PLAYER, heard.callable("edge"));
    scenes->observe(
        arena,
        NetwSceneCore::EVENT_ENTITY,
        heard.answering("falsey", false)
    );

    scenes->dispatch(arena, NetwSceneCore::EVENT_PLAYER, true, Variant());
    scenes->dispatch(arena, NetwSceneCore::EVENT_PLAYER, false, Variant());
    scenes->dispatch(arena, NetwSceneCore::EVENT_ENTITY, true, Variant());

    NETW_CHECK_EQ(heard.count("edge"), 2);
    NETW_CHECK_EQ(heard.count("falsey"), 1);
    NETW_CHECK_EQ(
        scenes->observer_count(arena, NetwSceneCore::EVENT_PLAYER),
        1
    );
    NETW_CHECK_EQ(
        scenes->observer_count(arena, NetwSceneCore::EVENT_ENTITY),
        1
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] SO3 one of two observers sharing a scene and "
    "an event retires alone, which is the case a Callable-matching removal "
    "cannot express, because a bound Callable compares on its base and its "
    "bind COUNT rather than on the values bound"
) {
    Ref<NetwSceneCore> scenes;
    scenes.instantiate();
    RID_Owner<int> owner;
    const RID arena = owner.make_rid(0);
    const CallLog heard;
    scenes->observe(
        arena,
        NetwSceneCore::EVENT_PLAYER,
        heard.answering("leaving", true)
    );
    scenes->observe(
        arena,
        NetwSceneCore::EVENT_PLAYER,
        heard.callable("staying")
    );
    NETW_CHECK_EQ(
        scenes->observer_count(arena, NetwSceneCore::EVENT_PLAYER),
        2
    );

    scenes->dispatch(arena, NetwSceneCore::EVENT_PLAYER, true, Variant());

    NETW_CHECK_EQ(
        scenes->observer_count(arena, NetwSceneCore::EVENT_PLAYER),
        1
    );
    NETW_CHECK_EQ(heard.count("leaving"), 1);
    NETW_CHECK_EQ(heard.count("staying"), 1);

    scenes->dispatch(arena, NetwSceneCore::EVENT_PLAYER, true, Variant());

    NETW_CHECK_EQ(heard.count("leaving"), 1);
    NETW_CHECK_EQ(heard.count("staying"), 2);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SO4 an observer whose object is gone is "
    "dropped without being called, so retiring is a decision a live observer "
    "takes and never the registry's way of noticing a dead one"
) {
    Ref<NetwSceneCore> scenes;
    scenes.instantiate();
    RID_Owner<int> owner;
    const RID arena = owner.make_rid(0);
    const CallLog surviving;
    {
        const CallLog dying;
        scenes->observe(
            arena,
            NetwSceneCore::EVENT_PLAYER,
            dying.callable("gone")
        );
        scenes->observe(
            arena,
            NetwSceneCore::EVENT_PLAYER,
            surviving.callable("here")
        );
        NETW_CHECK_EQ(
            scenes->observer_count(arena, NetwSceneCore::EVENT_PLAYER),
            2
        );
    }

    NETW_CHECK_EQ(
        scenes->dispatch(arena, NetwSceneCore::EVENT_PLAYER, true, Variant()),
        1
    );
    NETW_CHECK_EQ(
        scenes->observer_count(arena, NetwSceneCore::EVENT_PLAYER),
        1
    );
    NETW_CHECK_EQ(surviving.count("here"), 1);
}

} // namespace TestSceneObserverRetireLaws
