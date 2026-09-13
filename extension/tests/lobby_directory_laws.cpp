#include "support/netw_test.h"

#include "godot/object.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/connect/directory_transport.hpp"

namespace TestLobbyDirectoryLaws {

using namespace godot;
using netw::LobbyDirectory;
using netw::connect::DirectoryTransport;

class FakeDirectory : public LobbyDirectory {
public:
    StringName announced = StringName("FakeMultiplayerPeer");
    String joined;
    Dictionary hosted;
    int browses = 0;
    int leaves = 0;

    StringName peer_class() override {
        return announced;
    }
    int64_t capabilities() override {
        return CAPABILITY_BROWSE;
    }
    void join_lobby(const String &p_address) override {
        joined = p_address;
    }
    void host_lobby(const Dictionary &p_settings) override {
        hosted = p_settings;
    }
    void list_lobbies() override {
        browses += 1;
    }
    void leave_lobby() override {
        leaves += 1;
    }
};

struct Held {
    FakeDirectory *directory = memnew(FakeDirectory);
    ~Held() {
        memdelete(directory);
    }
};

TEST_CASE(
    "[Networked][Connect][Hosted] LD1 a directory that overrides nothing "
    "answers the stock defaults, so a provider states only what it differs on"
) {
    LobbyDirectory *bare = memnew(LobbyDirectory);

    NETW_CHECK_EQ(int(bare->peer_class() == StringName()), 1);
    NETW_CHECK_EQ(int(bare->capabilities()), 0);
    NETW_CHECK_EQ(int(bare->is_available()), 1);
    NETW_CHECK_EQ(int(bare->can_host_here()), 1);
    NETW_CHECK_EQ(int(bare->can_probe()), 0);
    NETW_CHECK_EQ(int(bare->accepts_empty_address()), 0);
    NETW_CHECK_EQ(int(bare->address_label() == String("Lobby")), 1);
    NETW_CHECK_EQ(int(bare->timeout_hint() == 20.0), 1);
    NETW_CHECK_EQ(int(bare->member_name(7) == String("Player 7")), 1);
    NETW_CHECK_EQ(int(bare->local_member_name() == String("Player")), 1);

    memdelete(bare);
}

TEST_CASE(
    "[Networked][Connect][Hosted] LD2 display_name falls through to the peer "
    "class a directory announces, so a provider naming one names both"
) {
    Held held;
    NETW_CHECK_EQ(
        int(held.directory->display_name() == String("FakeMultiplayerPeer")),
        1
    );
}

TEST_CASE(
    "[Networked][Connect][Hosted] LD3 supports reads the capability flags a "
    "directory advertises rather than a seam it happens to define"
) {
    Held held;
    NETW_CHECK_EQ(
        int(held.directory->supports(LobbyDirectory::CAPABILITY_BROWSE)),
        1
    );
    NETW_CHECK_EQ(
        int(held.directory->supports(LobbyDirectory::CAPABILITY_INVITES)),
        0
    );
}

TEST_CASE(
    "[Networked][Connect][Hosted] LD4 the plane reaches a directory as a typed "
    "provider, so a host request lands on host_lobby and a join on join_lobby "
    "with the address the caller named"
) {
    Held held;
    DirectoryTransport transport(held.directory);

    NETW_CHECK_EQ(int(DirectoryTransport::is_directory(held.directory)), 1);
    NETW_CHECK_EQ(
        int(DirectoryTransport::peer_class_of_directory(held.directory)
            == StringName("FakeMultiplayerPeer")),
        1
    );
    NETW_CHECK_EQ(
        int(transport.peer_class() == StringName("FakeMultiplayerPeer")),
        1
    );

    transport.make_peer(
        netw::connect::PEER_MODE_CLIENT,
        String("lobby-42"),
        Dictionary()
    );
    NETW_CHECK_EQ(int(held.directory->joined == String("lobby-42")), 1);
    NETW_CHECK_EQ(int(held.directory->hosted.is_empty()), 1);

    Dictionary settings;
    settings["max_players"] = 8;
    transport.make_peer(netw::connect::PEER_MODE_HOST, String(), settings);
    NETW_CHECK_EQ(int(held.directory->hosted["max_players"]), 8);

    transport.browse();
    transport.close();
    NETW_CHECK_EQ(held.directory->browses, 1);
    NETW_CHECK_EQ(held.directory->leaves, 1);
}

TEST_CASE(
    "[Networked][Connect][Hosted] LD5 a transport whose directory has been "
    "freed answers the stock defaults rather than reaching a dead object"
) {
    FakeDirectory *directory = memnew(FakeDirectory);
    DirectoryTransport transport(directory);
    memdelete(directory);

    NETW_CHECK_EQ(int(transport.peer_class() == StringName()), 1);
    NETW_CHECK_EQ(int(transport.is_available()), 0);
    NETW_CHECK_EQ(int(transport.can_browse()), 0);
    NETW_CHECK_EQ(int(transport.address_label() == String("Lobby")), 1);
    NETW_CHECK_EQ(int(transport.timeout_hint() == 20.0), 1);
    transport.browse();
    transport.close();
}

TEST_CASE(
    "[Networked][Connect][Hosted] LD6 an object that is not a directory is not "
    "adopted as one, which is what keeps a stray node out of the peer sources"
) {
    Node *stray = memnew(Node);
    NETW_CHECK_EQ(int(DirectoryTransport::is_directory(stray)), 0);
    NETW_CHECK_EQ(int(DirectoryTransport::is_directory(nullptr)), 0);
    NETW_CHECK_EQ(
        int(DirectoryTransport::peer_class_of_directory(stray) == StringName()),
        1
    );
    memdelete(stray);
}

} // namespace TestLobbyDirectoryLaws
