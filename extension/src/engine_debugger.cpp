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

} // namespace netw::gd
