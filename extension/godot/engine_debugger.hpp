#pragma once

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/debugger/engine_debugger.h"

namespace godot {
using ::EngineDebugger;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/engine_debugger.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

bool debugger_active();

#if defined(DEBUG_ENABLED)
bool debugger_profiling(const godot::StringName &p_name);
void debugger_add_frame_data(
    const godot::StringName &p_name,
    const godot::Array &p_data
);
#endif

void debugger_send(const godot::String &p_message, const godot::Array &p_data);

} // namespace netw::gd
