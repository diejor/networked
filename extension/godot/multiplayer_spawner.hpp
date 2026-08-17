#pragma once

#if defined(NETW_MODULE)
#include "modules/multiplayer/multiplayer_spawner.h"

namespace godot {
using ::MultiplayerSpawner;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/multiplayer_spawner.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif
