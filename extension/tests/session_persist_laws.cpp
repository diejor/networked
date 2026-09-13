#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"

namespace TestNetwSessionPersist {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPromise;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a persistence verb answers a resolved "
    "refusal rather than nothing, so a caller may always await the promise"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    const Ref<NetwPromise> hydrated = session->persist_hydrate(entity);
    CHECK(hydrated.is_valid());

    const Ref<NetwPromise> flushed = session->persist_flush(entity, Array());
    CHECK(flushed.is_valid());

    SUBCASE("an entity nobody minted is still answered with a promise") {
        CHECK(session->persist_hydrate(RID()).is_valid());
        CHECK(session->persist_flush(RID(), Array()).is_valid());
    }
}

} // namespace TestNetwSessionPersist
