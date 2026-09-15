#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/carrier_row.hpp"

namespace netw {

class CarrierBatch {
    godot::PackedByteArray held;
    godot::LocalVector<CarrierRow> descriptors;
    int64_t frames = 0;
    int64_t bits = 0;

public:
    void take_frame(const godot::PackedByteArray &p_frame);
    void attach(const CarrierRow &p_row);
    void close_route(int64_t p_route);
    void release();

    const godot::PackedByteArray &bytes() const {
        return held;
    }

    const godot::LocalVector<CarrierRow> &rows() const {
        return descriptors;
    }

    int64_t frame_count() const {
        return frames;
    }

    int64_t bit_count() const {
        return bits;
    }

    bool is_empty() const {
        return held.is_empty();
    }

    int64_t size() const {
        return int64_t(held.size());
    }
};

class NetwCarrierBuffers {
    godot::HashMap<int64_t, CarrierBatch> unreliable;
    godot::HashMap<int64_t, CarrierBatch> reliable;

    godot::HashMap<int64_t, CarrierBatch> &lane(bool p_reliable);
    const godot::HashMap<int64_t, CarrierBatch> &lane(bool p_reliable) const;

public:
    CarrierBatch append(
        int64_t p_peer,
        const godot::PackedByteArray &p_frame,
        bool p_reliable,
        int64_t p_budget,
        const CarrierRow *p_row = nullptr
    );

    CarrierBatch take(int64_t p_peer, bool p_reliable);

    void close_route(int64_t p_route);

    godot::PackedInt32Array peers(bool p_reliable) const;

    int64_t pending(int64_t p_peer, bool p_reliable) const;
    void clear();
};

} // namespace netw
