#include "godot/engine_debugger.hpp"

#include "godot/variant.hpp"

using namespace godot;

namespace netw::gd {

bool debugger_active() {
#if defined(NETW_MODULE)
    return EngineDebugger::is_active();
#else
    EngineDebugger *debugger = EngineDebugger::get_singleton();
    return debugger != nullptr && debugger->is_active();
#endif
}

void debugger_send(const String &p_message, const Array &p_data) {
    if (!debugger_active()) {
        return;
    }
    EngineDebugger::get_singleton()->send_message(p_message, p_data);
}

#if defined(DEBUG_ENABLED)
bool debugger_profiling(const StringName &p_name) {
    return debugger_active()
        && EngineDebugger::get_singleton()->is_profiling(p_name);
}
#endif

} // namespace netw::gd
