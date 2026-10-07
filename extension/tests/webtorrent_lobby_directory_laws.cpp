#include "support/netw_test.h"

#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/webtorrent_lobby_directory.hpp"

namespace netw {

struct WebTorrentLobbyDirectoryProbe {
    static godot::PackedStringArray trackers(
        const WebTorrentLobbyDirectory &p_directory
    ) {
        return p_directory.effective_trackers();
    }

    static godot::String space(const WebTorrentLobbyDirectory &p_directory) {
        return p_directory.effective_namespace();
    }
};

} // namespace netw

namespace TestNetwWebTorrentLobbyDirectory {

using namespace godot;
using netw::NetwMultiplayer;
using netw::WebTorrentLobbyDirectory;
using netw::WebTorrentLobbyDirectoryProbe;

TEST_CASE(
    "[Networked][Connect][Hosted] WT1 an empty trackers export reads the "
    "trackers and namespace from the WebRTC defaults, and a non-empty export "
    "still wins"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    WebTorrentLobbyDirectory *board = memnew(WebTorrentLobbyDirectory);
    board->service_entered(core.ptr());
    PackedStringArray preset;
    preset.push_back(String("wss://private.example"));
    Dictionary defaults;
    defaults["trackers"] = preset;
    defaults["signaling_namespace"] = String("private-game");
    const RID webrtc
        = core->transport_find(StringName("WebRTCMultiplayerPeer"));
    REQUIRE(core->transport_set_defaults(webrtc, defaults) == OK);

    CHECK(WebTorrentLobbyDirectoryProbe::trackers(*board) == preset);
    CHECK(WebTorrentLobbyDirectoryProbe::space(*board) == "private-game");

    PackedStringArray authored;
    authored.push_back(String("ws://127.0.0.1:1"));
    board->set_trackers(authored);
    CHECK(WebTorrentLobbyDirectoryProbe::trackers(*board) == authored);

    board->service_exiting(core.ptr());
    memdelete(board);
    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Hosted] a trackers entry that is not a ws:// or "
    "wss:// URL is kept in the export and skipped when browsing"
) {
    WebTorrentLobbyDirectory *board = memnew(WebTorrentLobbyDirectory);
    PackedStringArray urls;
    urls.push_back(String("ws://127.0.0.1:1"));
    urls.push_back(String("http://127.0.0.1:2"));
    board->set_trackers(urls);

    CHECK(board->get_trackers() == urls);
    PackedStringArray dialed;
    dialed.push_back(String("ws://127.0.0.1:1"));
    CHECK(WebTorrentLobbyDirectoryProbe::trackers(*board) == dialed);
    memdelete(board);
}

} // namespace TestNetwWebTorrentLobbyDirectory
