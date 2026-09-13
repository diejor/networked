#include "support/netw_test.h"

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/sync_compat.hpp"
#include "netw/sync_kernel.hpp"

namespace TestSyncCompat {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::SyncCompat;
using netw::spawn::Book;
namespace sync_kernel = netw::sync_kernel;
using netw::NetwSyncModel;

const int64_t ROUTE = 7;

struct Stand {
    Ref<NetwMultiplayer> core;
    NetwSyncModel model;
    SyncCompat *compat = nullptr;
    Node *root = nullptr;
    MultiplayerSynchronizer *sync = nullptr;
    Ref<SceneReplicationConfig> config;
    Ref<NetwEntity> entity;

    Stand() {
        core.instantiate();
        compat = core->get_replication_plane()->get_sync_compat();
        compat->set_sync_model(&model);

        root = memnew(Node);
        root->set_name("SyncProbe");
        sync = memnew(MultiplayerSynchronizer);
        sync->set_name("Sync");
        sync->set_root_path(NodePath(".."));
        config.instantiate();
        root->add_child(sync);
        sync->set_owner(root);
        Node *scene = netw::gd::scene_root();
        if (scene != nullptr) {
            scene->add_child(root);
        }
    }

    ~Stand() {
        Node *parent = root->get_parent();
        if (parent != nullptr) {
            parent->remove_child(root);
        }
        memdelete(root);
    }

    void declare(const String &p_name, int p_mode) {
        const NodePath path = NodePath(String(".:") + p_name);
        config->add_property(path);
        config->property_set_replication_mode(
            path,
            SceneReplicationConfig::ReplicationMode(p_mode)
        );
    }

    void arm() {
        sync->set_replication_config(config);
        entity = NetwEntity::ensure(root);
        core->liveness_bind_route(ROUTE, entity.ptr());
        compat->consume(root, sync);
    }

    int64_t counter(const char *p_key) const {
        return int64_t(compat->counters()[StringName(p_key)]);
    }

    PackedByteArray sync_payload(
        int64_t p_ordinal,
        int64_t p_flags,
        const Array &p_values
    ) const {
        netw::wire::WriteStream stream;
        sync_kernel::VolatileHead head;
        head.ordinal = uint64_t(p_ordinal);
        head.flags = uint64_t(p_flags);
        Array types;
        for (int at = 0; at < p_values.size(); ++at) {
            types.push_back(int(p_values[at].get_type()));
        }
        if (!sync_kernel::VolatileHead::wire.run(stream, head)
            || !netw::call_args::values_write(stream, p_values, Array(), types)
            || !stream.align_verify()) {
            return PackedByteArray();
        }
        return stream.to_bytes();
    }
};

TEST_CASE(
    "[Networked][Sync][Hosted] a synchronizer is consumed once, and released "
    "with the registration that consumed it"
) {
    Stand stand;
    stand.declare("position", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.arm();

    NETW_CHECK_EQ(stand.counter("sync_sets_active"), int64_t(1));

    stand.compat->consume(stand.root, stand.sync);
    NETW_CHECK_EQ(stand.counter("sync_sets_active"), int64_t(1));

    NETW_CHECK_EQ(
        int(stand.compat->consume(nullptr, stand.sync)),
        int(ERR_INVALID_PARAMETER)
    );
    NETW_CHECK_EQ(
        int(stand.compat->consume(stand.root, nullptr)),
        int(ERR_INVALID_PARAMETER)
    );

    stand.compat->consume_remove(stand.root, stand.sync);
    NETW_CHECK_EQ(stand.counter("sync_sets_active"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] a consumed row applies its config's always "
    "properties, and a frame whose value count disagrees poisons it"
) {
    Stand stand;
    stand.declare("name", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.arm();

    Array one;
    one.push_back(String("applied"));
    stand.compat->handle_sync(stand.entity, stand.sync_payload(0, 0, one), 1);

    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(1));
    NETW_CHECK_EQ(stand.counter("drops_sync_poisoned"), int64_t(0));

    Array two;
    two.push_back(String("a"));
    two.push_back(String("b"));
    stand.compat->handle_sync(stand.entity, stand.sync_payload(0, 0, two), 1);

    NETW_CHECK_EQ(stand.counter("drops_sync_poisoned"), int64_t(1));

    stand.compat->handle_sync(stand.entity, stand.sync_payload(0, 0, one), 1);
    NETW_CHECK_EQ(stand.counter("drops_sync_poisoned"), int64_t(2));
    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(1));
}

TEST_CASE(
    "[Networked][Sync][Hosted] an ordinal no consumed row answers to is "
    "dropped and counted rather than misread"
) {
    Stand stand;
    stand.declare("name", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.arm();

    Array one;
    one.push_back(String("applied"));
    stand.compat->handle_sync(stand.entity, stand.sync_payload(5, 0, one), 1);

    NETW_CHECK_EQ(stand.counter("drops_sync_no_set"), int64_t(1));
    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] only the synchronizer's authority may author "
    "its stream"
) {
    Stand stand;
    stand.declare("name", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.arm();

    Array one;
    one.push_back(String("applied"));
    stand.compat->handle_sync(stand.entity, stand.sync_payload(0, 0, one), 2);

    NETW_CHECK_EQ(stand.counter("drops_sync_bad_sender"), int64_t(1));
    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] a sender that wrote the reserved byte speaks a "
    "grammar this version does not have, so the frame drops loudly"
) {
    Stand stand;
    stand.declare("name", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.arm();

    Array one;
    one.push_back(String("applied"));
    stand.compat->handle_sync(
        stand.entity,
        stand.sync_payload(0, sync_kernel::RESERVED + 1, one),
        1
    );

    NETW_CHECK_EQ(stand.counter("drops_sync_unknown_flag"), int64_t(1));
    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] the config is read live, so a config the "
    "synchronizer is given after consumption replaces the field list"
) {
    Stand stand;
    stand.declare("first", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.arm();

    Array one;
    one.push_back(String("a"));
    stand.compat->handle_sync(stand.entity, stand.sync_payload(0, 0, one), 1);
    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(1));

    Ref<SceneReplicationConfig> wider;
    wider.instantiate();
    wider->add_property(NodePath(".:first"));
    wider->property_set_replication_mode(
        NodePath(".:first"),
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS
    );
    wider->add_property(NodePath(".:second"));
    wider->property_set_replication_mode(
        NodePath(".:second"),
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS
    );
    stand.sync->set_replication_config(wider);

    Array two;
    two.push_back(String("a"));
    two.push_back(String("b"));
    stand.compat->handle_sync(stand.entity, stand.sync_payload(0, 0, two), 1);

    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(2));
    NETW_CHECK_EQ(stand.counter("drops_sync_poisoned"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] a config mutated in place is honored only once "
    "it announces the change, because the engine's own editors never do"
) {
    Stand stand;
    stand.declare("first", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.arm();

    Array two;
    two.push_back(String("a"));
    two.push_back(String("b"));

    stand.declare("second", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.compat->handle_sync(stand.entity, stand.sync_payload(0, 0, two), 1);
    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(0));
    NETW_CHECK_EQ(stand.counter("drops_sync_poisoned"), int64_t(1));

    Stand announced;
    announced.declare("first", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    announced.arm();
    announced.declare(
        "second",
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS
    );
    announced.config->emit_changed();

    announced.compat
        ->handle_sync(announced.entity, announced.sync_payload(0, 0, two), 1);
    NETW_CHECK_EQ(announced.counter("sync_frames_in"), int64_t(1));
    NETW_CHECK_EQ(announced.counter("drops_sync_poisoned"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] a watch list past the 64-bit delta mask is "
    "unrepresentable, so the row poisons at consumption"
) {
    Stand fits;
    fits.declare("name", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    for (int at = 0; at < 64; ++at) {
        fits.declare(
            String("w") + String::num_int64(at),
            SceneReplicationConfig::REPLICATION_MODE_ON_CHANGE
        );
    }
    fits.arm();

    Array one;
    one.push_back(String("applied"));
    fits.compat->handle_sync(fits.entity, fits.sync_payload(0, 0, one), 1);

    NETW_CHECK_EQ(fits.counter("sync_frames_in"), int64_t(1));
    NETW_CHECK_EQ(fits.counter("drops_sync_poisoned"), int64_t(0));

    Stand over;
    over.declare("name", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    for (int at = 0; at < 65; ++at) {
        over.declare(
            String("w") + String::num_int64(at),
            SceneReplicationConfig::REPLICATION_MODE_ON_CHANGE
        );
    }
    over.arm();

    over.compat->handle_sync(over.entity, over.sync_payload(0, 0, one), 1);

    NETW_CHECK_EQ(over.counter("drops_sync_poisoned"), int64_t(1));
    NETW_CHECK_EQ(over.counter("sync_frames_in"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] the spawn frame carries one ordinal and schema "
    "hash per consumed set, and the hash follows the field order"
) {
    Stand stand;
    stand.declare("first", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.declare("second", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.arm();

    const Dictionary declared_rows = netw::spawn::Record::descriptors_of_bytes(
        stand.compat->encode_descriptors(ROUTE)
    );

    NETW_CHECK_EQ(int64_t(declared_rows.size()), int64_t(1));
    REQUIRE(declared_rows.has(int64_t(0)));
    const int64_t declared = declared_rows[int64_t(0)];

    stand.compat->note_schema(ROUTE, Dictionary());
    Dictionary agreeing;
    agreeing[int64_t(0)] = declared;
    stand.compat->note_schema(ROUTE, agreeing);

    Array two;
    two.push_back(String("a"));
    two.push_back(String("b"));
    stand.compat->handle_sync(stand.entity, stand.sync_payload(0, 0, two), 1);

    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(1));
    NETW_CHECK_EQ(stand.counter("drops_sync_poisoned"), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] a spawn descriptor the receiver's own set "
    "disagrees with poisons the binding instead of misreading its bytes"
) {
    Stand stand;
    stand.declare("first", SceneReplicationConfig::REPLICATION_MODE_ALWAYS);
    stand.arm();

    Dictionary disagreeing;
    disagreeing[int64_t(0)] = int64_t(0x1234);
    stand.compat->note_schema(ROUTE, disagreeing);

    Array one;
    one.push_back(String("a"));
    stand.compat->handle_sync(stand.entity, stand.sync_payload(0, 0, one), 1);

    NETW_CHECK_EQ(stand.counter("drops_sync_poisoned"), int64_t(1));
    NETW_CHECK_EQ(stand.counter("sync_frames_in"), int64_t(0));
}

} // namespace TestSyncCompat
