#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/server_info.hpp"

namespace netw::connect {

struct TargetRow {
    godot::RID transport;
    godot::String address;
    godot::String display_name;
    godot::Dictionary metadata;
    godot::Ref<NetwServerInfo> advertised;
    godot::StringName directory;
    int64_t status = 0;
    godot::Ref<NetwServerInfo> info;
    bool observed = false;
};

class BrowseList {
public:
    static const int MAX_IN_FLIGHT_PROBES = 6;

    ~BrowseList();

    void set_local_app_id(const godot::StringName &p_app_id);

    godot::RID add(
        const godot::RID &p_transport,
        const godot::String &p_address,
        const godot::String &p_display_name
    );
    bool remove(const godot::RID &p_target);
    godot::RID find(
        const godot::RID &p_transport,
        const godot::String &p_address
    ) const;
    bool is_caller_row(const godot::RID &p_target) const;

    TargetRow *row(const godot::RID &p_target);
    const TargetRow *row(const godot::RID &p_target) const;

    godot::Array list() const;
    const godot::LocalVector<godot::RID> &caller_targets() const {
        return caller_rows;
    }
    int size() const;

    void publish_directory(
        const godot::StringName &p_directory,
        const godot::RID &p_transport,
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::Array &p_infos,
        godot::LocalVector<godot::RID> &r_dropped,
        godot::LocalVector<godot::RID> &r_added,
        godot::LocalVector<godot::RID> &r_updated
    );
    void forget_directory(
        const godot::StringName &p_directory,
        godot::LocalVector<godot::RID> &r_dropped
    );

    void enqueue(const godot::RID &p_target);
    void enqueue_caller_rows();
    bool next_to_probe(godot::RID &r_target);
    void probe_settled(
        const godot::RID &p_target,
        int64_t p_error,
        const godot::Ref<NetwServerInfo> &p_info
    );
    int in_flight() const {
        return probes_in_flight;
    }

    int64_t classify(
        const godot::Ref<NetwServerInfo> &p_info,
        int64_t p_error
    ) const;

private:
    godot::StringName local_app_id;
    godot::LocalVector<godot::RID> caller_rows;
    godot::LocalVector<godot::RID> directory_rows;
    godot::LocalVector<godot::RID> queue;
    int probes_in_flight = 0;

    godot::RID mint(const TargetRow &p_row);
    void release(const godot::RID &p_target);
};

} // namespace netw::connect
