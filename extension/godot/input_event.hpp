#pragma once

#include "godot/ref_counted.hpp"

#if defined(NETW_MODULE)
#include "core/input/input_event.h"

namespace godot {
using ::InputEvent;
using ::InputEventJoypadButton;
using ::InputEventJoypadMotion;
using ::InputEventMouse;
} // namespace godot

#define NETW_GUI_INPUT gui_input
#define NETW_UNHANDLED_INPUT unhandled_input
#define NETW_NODE_INPUT input
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/input_event.hpp>
#include <godot_cpp/classes/input_event_joypad_button.hpp>
#include <godot_cpp/classes/input_event_joypad_motion.hpp>
#include <godot_cpp/classes/input_event_mouse.hpp>

#define NETW_GUI_INPUT _gui_input
#define NETW_UNHANDLED_INPUT _unhandled_input
#define NETW_NODE_INPUT _input
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif
