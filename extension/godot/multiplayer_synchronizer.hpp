#pragma once

#if defined(NETW_MODULE)
#include "modules/multiplayer/multiplayer_synchronizer.h"
#include "modules/multiplayer/scene_replication_config.h"

namespace godot {
using ::MultiplayerSynchronizer;
using ::SceneReplicationConfig;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/multiplayer_synchronizer.hpp>
#include <godot_cpp/classes/scene_replication_config.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif
