#include "support/netw_test.h"

#include "netw/channel_book.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwChannelBook {

using namespace godot;
using netw::NetwChannelBook;

TEST_CASE(
    "[Networked][Repl][Hosted] a channel's handler and its deferral are one "
    "registration"
) {
    NetwChannelBook book;
    Ref<RefCounted> holder;
    holder.instantiate();
    const Callable first(holder.ptr(), StringName("get_class"));
    const Callable second(holder.ptr(), StringName("get_instance_id"));

    CHECK_FALSE(book.register_channel(120, first, true));
    CHECK(book.handler_of(120) == first);
    CHECK(book.defers(120));
    NETW_CHECK_EQ(book.size(), 1);

    CHECK(book.register_channel(120, second, false));
    CHECK(book.handler_of(120) == second);
    CHECK_FALSE(book.defers(120));
    NETW_CHECK_EQ(book.size(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] an unregistered channel has no handler and "
    "defers nothing"
) {
    NetwChannelBook book;
    Ref<RefCounted> holder;
    holder.instantiate();

    CHECK_FALSE(book.handler_of(120).is_valid());
    CHECK_FALSE(book.defers(120));
    CHECK_FALSE(book.unregister_channel(120));

    const Callable handler(holder.ptr(), StringName("get_class"));
    book.register_channel(120, handler, true);
    CHECK(book.unregister_channel(120));
    CHECK_FALSE(book.handler_of(120).is_valid());
    CHECK_FALSE(book.defers(120));
    NETW_CHECK_EQ(book.size(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] channels are registered one at a time and "
    "cleared all at once"
) {
    NetwChannelBook book;
    Ref<RefCounted> holder;
    holder.instantiate();
    const Callable handler(holder.ptr(), StringName("get_class"));

    book.register_channel(120, handler, true);
    book.register_channel(121, handler, false);
    NETW_CHECK_EQ(book.size(), 2);
    CHECK(book.defers(120));
    CHECK_FALSE(book.defers(121));

    book.clear();

    NETW_CHECK_EQ(book.size(), 0);
    CHECK_FALSE(book.handler_of(120).is_valid());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer-scoped row and an entity row answer "
    "different lanes"
) {
    NetwChannelBook book;
    Ref<RefCounted> holder;
    holder.instantiate();
    const Callable handler(holder.ptr(), StringName("get_class"));

    CHECK_FALSE(book.register_protocol(23, handler));
    CHECK(book.protocol_handler_of(23) == handler);
    CHECK_FALSE(book.handler_of(23).is_valid());
    CHECK_FALSE(book.defers(23));

    CHECK_FALSE(book.register_channel(120, handler, true));
    CHECK(book.handler_of(120) == handler);
    CHECK_FALSE(book.protocol_handler_of(120).is_valid());

    CHECK(book.register_channel(23, handler, false));
    CHECK(book.handler_of(23) == handler);
    CHECK_FALSE(book.protocol_handler_of(23).is_valid());
    NETW_CHECK_EQ(book.size(), 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] settle refuses a peer-scoped channel with no "
    "handler"
) {
    NetwChannelBook book;
    Ref<RefCounted> holder;
    holder.instantiate();
    const Callable handler(holder.ptr(), StringName("get_class"));

    NETW_CHECK_EQ(int(book.settle_protocol()), int(ERR_UNCONFIGURED));

    const netw::wire::WireRegistry registry
        = netw::wire::WireRegistry::create_default();
    int declared = 0;
    for (int id = 0; id < netw::wire::WireRegistry::MAX_CHANNELS; ++id) {
        const netw::wire::ChannelDecl *decl
            = registry.find_channel(uint8_t(id));
        if (decl == nullptr || decl->is_reserved) {
            continue;
        }
        if (decl->kind != netw::wire::ChannelKind::SESSION) {
            continue;
        }
        declared++;
        book.register_protocol(id, handler);
    }
    NETW_CHECK_GT(declared, 0);
    NETW_CHECK_EQ(int(book.settle_protocol()), int(OK));

    CHECK(book.unregister_channel(23));
    NETW_CHECK_EQ(int(book.settle_protocol()), int(ERR_UNCONFIGURED));

    book.register_channel(23, handler, false);
    NETW_CHECK_EQ(int(book.settle_protocol()), int(ERR_UNCONFIGURED));
}

} // namespace TestNetwChannelBook
