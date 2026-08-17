#include "netw/channel_book.hpp"

#include "godot/class_db.hpp"
#include "godot/rid.hpp"
#include "netw/log.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace netw {

const NetwChannelBook::Row *NetwChannelBook::row_of(int64_t channel) const {
    const HashMap<int64_t, Row>::ConstIterator found = rows.find(channel);
    return found ? &found->value : nullptr;
}

bool NetwChannelBook::register_channel(
    int64_t channel,
    const Callable &handler,
    bool defers_when_unknown
) {
    const HashMap<int64_t, Row>::Iterator found = rows.find(channel);
    if (found) {
        found->value.handler = handler;
        found->value.defers = defers_when_unknown;
        found->value.protocol = false;
        return true;
    }
    Row row;
    row.handler = handler;
    row.defers = defers_when_unknown;
    rows.insert(channel, row);
    return false;
}

bool NetwChannelBook::register_protocol(
    int64_t channel,
    const Callable &handler
) {
    const HashMap<int64_t, Row>::Iterator found = rows.find(channel);
    if (found) {
        found->value.handler = handler;
        found->value.defers = false;
        found->value.protocol = true;
        return true;
    }
    Row row;
    row.handler = handler;
    row.protocol = true;
    rows.insert(channel, row);
    return false;
}

bool NetwChannelBook::unregister_channel(int64_t channel) {
    return rows.erase(channel);
}

Callable NetwChannelBook::handler_of(int64_t channel) const {
    const Row *row = row_of(channel);
    return row && !row->protocol ? row->handler : Callable();
}

Callable NetwChannelBook::protocol_handler_of(int64_t channel) const {
    const Row *row = row_of(channel);
    return row && row->protocol ? row->handler : Callable();
}

Error NetwChannelBook::settle_protocol() const {
    static const wire::WireRegistry registry
        = wire::WireRegistry::create_default();
    Error verdict = OK;
    for (int id = 0; id < wire::WireRegistry::MAX_CHANNELS; ++id) {
        const wire::ChannelDecl *decl = registry.find_channel(uint8_t(id));
        if (decl == nullptr || decl->is_reserved) {
            continue;
        }
        if (decl->kind != wire::ChannelKind::SESSION) {
            continue;
        }
        if (!protocol_handler_of(id).is_valid()) {
            NETW_WARN(
                sys::SESSION,
                "channel %d (%s) is peer-scoped and has no handler",
                id,
                String(decl->name)
            );
            verdict = ERR_UNCONFIGURED;
        }
    }
    return verdict;
}

bool NetwChannelBook::defers(int64_t channel) const {
    const Row *row = row_of(channel);
    return row && row->defers;
}

int NetwChannelBook::size() const {
    return int(rows.size());
}

void NetwChannelBook::clear() {
    rows.clear();
}

void NetwChannelBook::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("register_channel", "channel", "handler", "defers"),
        &NetwChannelBook::register_channel
    );
    ClassDB::bind_method(
        D_METHOD("register_protocol", "channel", "handler"),
        &NetwChannelBook::register_protocol
    );
    ClassDB::bind_method(
        D_METHOD("unregister_channel", "channel"),
        &NetwChannelBook::unregister_channel
    );
    ClassDB::bind_method(
        D_METHOD("handler_of", "channel"),
        &NetwChannelBook::handler_of
    );
    ClassDB::bind_method(
        D_METHOD("protocol_handler_of", "channel"),
        &NetwChannelBook::protocol_handler_of
    );
    ClassDB::bind_method(
        D_METHOD("settle_protocol"),
        &NetwChannelBook::settle_protocol
    );
    ClassDB::bind_method(
        D_METHOD("defers", "channel"),
        &NetwChannelBook::defers
    );
    ClassDB::bind_method(D_METHOD("size"), &NetwChannelBook::size);
    ClassDB::bind_method(D_METHOD("clear"), &NetwChannelBook::clear);
}

} // namespace netw
