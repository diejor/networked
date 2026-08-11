#pragma once

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "scene/main/multiplayer_peer.h"

// Engine types are global, so the aliases keep `godot::` spellings compiling.
namespace godot {
using ::MultiplayerPeer;
using ::MultiplayerPeerExtension;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/multiplayer_peer_extension.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

/* A peer implemented in C++ derives from a different class in each tier, and
 * spells every override differently, for one reason: `MultiplayerPeerExtension`
 * reaches its implementation through GDVIRTUAL, which dispatches to a script or
 * an extension and never to a C++ override. So the module tier, which is
 * compiled INTO the engine, must derive from `MultiplayerPeer` and override the
 * plain virtuals, while the library tier derives from
 * `MultiplayerPeerExtension` and overrides the `_`-prefixed ones.
 *
 * That is the whole of the difference, and it is mechanical: one base, and one
 * leading underscore. `NETW_PEER_VIRTUAL` supplies the underscore where the
 * tier wants it, so an override is declared once.
 *
 *     void NETW_PEER_VIRTUAL(poll)() override;
 *
 * It covers the overrides whose signatures match. The two packet verbs differ
 * in signature as well as in name, so they have no one spelling.
 */
namespace netw {

#if defined(NETW_MODULE)
using MultiplayerPeerBase = godot::MultiplayerPeer;
#else
using MultiplayerPeerBase = godot::MultiplayerPeerExtension;
#endif

} // namespace netw

#if defined(NETW_MODULE)
#define NETW_PEER_VIRTUAL(m_name) m_name
#else
#define NETW_PEER_VIRTUAL(m_name) _##m_name
#endif
