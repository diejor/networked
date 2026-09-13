#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/object.hpp"
#include "godot/resource_loader.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/directory_transport.hpp"
#include "support/directory_stubs.h"

namespace TestNetwConnectQueryTeardown {

using namespace godot;
using netw::NetwMultiplayer;
using netw::connect::DirectoryTransport;

namespace {

netw_test::ProbeDirectory *make_directory() {
    return memnew(netw_test::ProbeDirectory);
}

int leaves_of(netw_test::ProbeDirectory *p_directory) {
    return p_directory->leaves;
}

int listings_of(netw_test::ProbeDirectory *p_directory) {
    return p_directory->listings;
}

} // namespace

TEST_CASE(
    "[Networked][Connect][QueryTeardown] tearing a discovery query down "
    "never abandons the lobby, so a player who closed the server browser "
    "after joining keeps the match the browser found for them"
) {
    netw_test::ProbeDirectory *directory = make_directory();
    REQUIRE(directory != nullptr);
    REQUIRE(DirectoryTransport::is_directory(directory));

    DirectoryTransport query(directory);
    query.browse();
    NETW_CHECK_EQ(listings_of(directory), 1);

    query.close_query();

    NETW_CHECK_EQ(leaves_of(directory), 0);

    memdelete(directory);
}

TEST_CASE(
    "[Networked][Connect][QueryTeardown] tearing the MATCH runtime down "
    "still leaves the lobby, so splitting the two teardowns removed no "
    "release the directory was owed"
) {
    netw_test::ProbeDirectory *directory = make_directory();
    REQUIRE(directory != nullptr);

    DirectoryTransport match(directory);
    match.close();

    NETW_CHECK_EQ(leaves_of(directory), 1);

    memdelete(directory);
}

TEST_CASE(
    "[Networked][Connect][QueryTeardown] a session dropping the browsers it "
    "opened over a directory reaches the query teardown and not the match "
    "one, which is the whole path a closing browser takes"
) {
    netw_test::ProbeDirectory *directory = make_directory();
    REQUIRE(directory != nullptr);

    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->service_register(directory, nullptr);
    core->connect_plane().refresh();

    REQUIRE(listings_of(directory) > 0);

    core->connect_plane().dispose();

    NETW_CHECK_EQ(leaves_of(directory), 0);

    memdelete(directory);
}

} // namespace TestNetwConnectQueryTeardown

#endif
