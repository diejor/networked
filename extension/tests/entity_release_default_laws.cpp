#include "support/netw_test.h"

#include "netw/api/entity.hpp"
#include "netw/api/promise.hpp"

namespace TestEntityReleaseDefaultLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwPromise;

TEST_CASE(
    "[Networked][Entity][Hosted] R1 release_authority called with no "
    "arguments from a script reaches the server default, because the bound "
    "default is a peer id the guard accepts"
) {
    Ref<NetwEntity> entity;
    entity.instantiate();

    const Variant answer = entity->callv("release_authority", Array());
    const Ref<NetwPromise> promise = answer;

    REQUIRE(promise.is_valid());
    CHECK(promise->get_code() != ERR_INVALID_PARAMETER);
}

} // namespace TestEntityReleaseDefaultLaws
