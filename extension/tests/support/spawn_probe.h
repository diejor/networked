#pragma once

#include "netw_test.h"

#include "godot/class_db.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

#if defined(NETW_TIER_HOSTED)
#include "netw/api/entity.hpp"
#include "netw/api/property_config.hpp"
#include "netw/entity/control.hpp"
#include "netw/script/model.hpp"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#endif

namespace netw_test {

#if defined(NETW_TIER_HOSTED)

class SpawnIdentityProbe : public godot::Node {
    GDCLASS(SpawnIdentityProbe, godot::Node)

    godot::Dictionary identity_packet;
    godot::Vector<godot::StringName> sample_stages;
    godot::Vector<godot::Dictionary> sample_packets;

protected:
    static void _bind_methods() {
        godot::ClassDB::bind_method(
            D_METHOD("set_identity_packet", "packet"),
            &SpawnIdentityProbe::set_identity_packet
        );
        godot::ClassDB::bind_method(
            D_METHOD("get_identity_packet"),
            &SpawnIdentityProbe::get_identity_packet
        );
        ADD_PROPERTY(
            godot::PropertyInfo(godot::Variant::DICTIONARY, "identity_packet"),
            "set_identity_packet",
            "get_identity_packet"
        );
        godot::ClassDB::bind_method(
            D_METHOD("record_spawning"),
            &SpawnIdentityProbe::record_spawning
        );
    }

    void _notification(int p_what) {
        if (p_what != NOTIFICATION_PARENTED) {
            return;
        }
        record(godot::StringName("parented"));
        const godot::Ref<netw::NetwEntity> entity
            = netw::NetwEntity::resolve(this);
        if (entity.is_null()) {
            return;
        }
        entity->set_initial_controller(
            int64_t(netw::entity::Control::InitialController::REPRESENTED_PEER)
        );
        netw::script::model::configure_node_property(
            this,
            godot::StringName("identity_packet")
        )
            ->on_spawn();
        const godot::Callable sink
            = godot::Callable(this, godot::StringName("record_spawning"));
        if (!entity->is_connected(godot::StringName("spawning"), sink)) {
            entity->connect(godot::StringName("spawning"), sink);
        }
    }

public:
    void _enter_tree() override {
        record(godot::StringName("enter_tree"));
    }

    void _ready() override {
        record(godot::StringName("ready"));
    }

    void record_spawning() {
        record(godot::StringName("spawning"));
    }

    void set_identity_packet(const godot::Dictionary &p_packet) {
        identity_packet = p_packet;
    }

    godot::Dictionary get_identity_packet() const {
        return identity_packet;
    }

    bool saw(const godot::StringName &p_stage) const {
        for (int at = 0; at < sample_stages.size(); ++at) {
            if (sample_stages[at] == p_stage) {
                return true;
            }
        }
        return false;
    }

    godot::String marker_at(const godot::StringName &p_stage) const {
        for (int at = 0; at < sample_stages.size(); ++at) {
            if (sample_stages[at] != p_stage) {
                continue;
            }
            return godot::String(sample_packets[at].get("marker", ""));
        }
        return godot::String();
    }

    godot::String marker() const {
        return godot::String(identity_packet.get("marker", ""));
    }

    int64_t route_at(const godot::StringName &p_stage) const {
        return reading_at(p_stage, godot::StringName("route"));
    }

    int64_t peer_at(const godot::StringName &p_stage) const {
        return reading_at(p_stage, godot::StringName("peer"));
    }

    int64_t authority_at(const godot::StringName &p_stage) const {
        return reading_at(p_stage, godot::StringName("authority"));
    }

    godot::StringName entity_id_at(const godot::StringName &p_stage) const {
        for (int at = 0; at < sample_stages.size(); ++at) {
            if (sample_stages[at] == p_stage) {
                return godot::StringName(
                    sample_readings[at].get("entity_id", godot::StringName())
                );
            }
        }
        return godot::StringName();
    }

private:
    godot::Vector<godot::Dictionary> sample_readings;

    int64_t reading_at(
        const godot::StringName &p_stage,
        const godot::StringName &p_field
    ) const {
        for (int at = 0; at < sample_stages.size(); ++at) {
            if (sample_stages[at] == p_stage) {
                return int64_t(sample_readings[at].get(p_field, -1));
            }
        }
        return -1;
    }

    void record(const godot::StringName &p_stage) {
        sample_stages.push_back(p_stage);
        sample_packets.push_back(identity_packet.duplicate(true));

        godot::Dictionary reading;
        const godot::Ref<netw::NetwEntity> entity
            = netw::NetwEntity::resolve(this);
        if (entity.is_valid()) {
            reading["route"] = entity->get_route();
            reading["peer"] = entity->get_peer_id();
            reading["entity_id"] = entity->get_entity_id();
            godot::Node *root = entity->get_owner();
            reading["authority"] = root != nullptr
                ? int64_t(root->get_multiplayer_authority())
                : int64_t(-1);
        }
        sample_readings.push_back(reading);
    }
};

#endif

} // namespace netw_test
