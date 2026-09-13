#include "netw/wire/spec.hpp"

#include "godot/local_vector.hpp"
#include "netw/carrier_frame.hpp"
#include "netw/interest/relay.hpp"
#include "netw/predict/frames.hpp"
#include "netw/schema_core.hpp"
#include "netw/session/frames.hpp"
#include "netw/spawn/record.hpp"
#include "netw/sync_kernel.hpp"
#include "netw/table/core.hpp"
#include "netw/wire/capture.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace netw::wire {

namespace {

const char *kind_name(ChannelKind p_value) {
    switch (p_value) {
        case ChannelKind::KEYED:
            return "KEYED";
        case ChannelKind::SESSION:
            return "SESSION";
        case ChannelKind::ROUTED:
            return "ROUTED";
    }
    return "?";
}

const char *reliability_name(Reliability p_value) {
    switch (p_value) {
        case Reliability::UNRELIABLE:
            return "UNRELIABLE";
        case Reliability::UNRELIABLE_ACKED:
            return "UNRELIABLE_ACKED";
        case Reliability::RELIABLE:
            return "RELIABLE";
    }
    return "?";
}

const char *freshness_name(Freshness p_value) {
    switch (p_value) {
        case Freshness::NONE:
            return "NONE";
        case Freshness::FRESHEST_WINS:
            return "FRESHEST_WINS";
    }
    return "?";
}

const char *delivery_name(Delivery p_value) {
    switch (p_value) {
        case Delivery::IMMEDIATE:
            return "IMMEDIATE";
        case Delivery::FITTED:
            return "FITTED";
    }
    return "?";
}

const char *direction_name(Direction p_value) {
    switch (p_value) {
        case Direction::EITHER:
            return "EITHER";
        case Direction::SERVER_TO_CLIENT:
            return "SERVER_TO_CLIENT";
        case Direction::CLIENT_TO_SERVER:
            return "CLIENT_TO_SERVER";
        case Direction::OWNER_TO_SERVER:
            return "OWNER_TO_SERVER";
        case Direction::SERVER_TO_OWNER:
            return "SERVER_TO_OWNER";
    }
    return "?";
}

const char *payload_name(PayloadContract p_value) {
    switch (p_value) {
        case PayloadContract::RAW:
            return "RAW";
        case PayloadContract::PLANNED:
            return "PLANNED";
        case PayloadContract::DELTA:
            return "DELTA";
    }
    return "?";
}

const char *delta_name(DeltaMode p_value) {
    switch (p_value) {
        case DeltaMode::FULL:
            return "FULL";
        case DeltaMode::LADDER:
            return "LADDER";
    }
    return "?";
}

} // namespace

Dictionary spec_records() {
    Dictionary out = predict::spec_records();
    out.merge(frame_spec_records());
    out.merge(spawn::verb_spec_records());
    out.merge(session::frame_spec_records());
    out.merge(interest::awareness_spec_records());
    out.merge(table::Core::frame_spec_records());
    out.merge(sync_kernel::frame_spec_records());
    out.merge(NetwCarrierFrame::spec_records());
    out.merge(capture_spec_records());
    return out;
}

Array spec_channels() {
    const WireRegistry reg = WireRegistry::create_default();
    Array out;
    for (int id = 0; id < WireRegistry::MAX_CHANNELS; ++id) {
        const ChannelDecl *decl = reg.find_channel(uint8_t(id));
        if (decl == nullptr || decl->is_reserved) {
            continue;
        }
        Dictionary row;
        row[StringName("id")] = int64_t(id);
        row[StringName("name")] = String(decl->name);
        row[StringName("kind")] = String(kind_name(decl->kind));
        row[StringName("reliability")]
            = String(reliability_name(decl->reliability));
        row[StringName("freshness")] = String(freshness_name(decl->freshness));
        row[StringName("delivery")] = String(delivery_name(decl->delivery));
        row[StringName("direction")] = String(direction_name(decl->direction));
        row[StringName("payload")] = String(payload_name(decl->payload));
        out.push_back(row);
    }
    return out;
}

Array spec_reserved() {
    const WireRegistry reg = WireRegistry::create_default();
    Array out;
    for (int id = 0; id < WireRegistry::MAX_CHANNELS; ++id) {
        const ChannelDecl *decl = reg.find_channel(uint8_t(id));
        if (decl != nullptr && decl->is_reserved) {
            out.push_back(int64_t(id));
        }
    }
    return out;
}

Array spec_schemas(const netw::SchemaCore &p_schemas) {
    LocalVector<const table::SchemaRecord *> sealed;
    p_schemas.sealed_records(sealed);
    Array out;
    for (uint32_t at = 0; at < sealed.size(); ++at) {
        const table::SchemaRecord &record = *sealed[at];
        const WirePlan plan = WirePlan::compile(record);
        if (!plan.valid()) {
            continue;
        }
        Array columns;
        for (uint32_t index = 0; index < plan.column_count(); ++index) {
            const ColumnPlan &slot = plan.column(index);
            Dictionary cell;
            cell[StringName("key")] = String(record.columns[index].key);
            cell[StringName("width")] = int64_t(slot.width);
            cell[StringName("stride")] = int64_t(slot.stride);
            cell[StringName("delta")] = String(delta_name(slot.delta));
            columns.push_back(cell);
        }
        Dictionary row;
        row[StringName("name")] = String(record.name);
        row[StringName("shape_hash")] = int64_t(record.shape_hash);
        row[StringName("row_bits")] = plan.row_bits();
        row[StringName("columns")] = columns;
        out.push_back(row);
    }
    return out;
}

Dictionary spec_document() {
    Dictionary out;
    out[StringName("format")] = int64_t(FORMAT_VERSION);
    out[StringName("records")] = spec_records();
    out[StringName("channels")] = spec_channels();
    out[StringName("reserved")] = spec_reserved();
    out[StringName("identity")] = int64_t(
        WireRegistry::create_default().identity_hash() & 0x7FFFFFFFFFFFFFFFULL
    );
    return out;
}

} // namespace netw::wire
