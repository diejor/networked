#pragma once

#if defined(NETW_MODULE)
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"

namespace godot {
using ::ResourceFormatLoader;
using ::ResourceFormatSaver;
using ::ResourceLoader;
using ::ResourceSaver;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif
