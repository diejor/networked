#pragma once

// Named .h because the engine's generated registration includes
// "modules/<name>/register_types.h" verbatim, and compiles it with neither
// NETW_GDEXTENSION nor NETW_MODULE defined. So the engine spelling is default.

#if defined(NETW_GDEXTENSION)
#include "godot/class_db.hpp"

void initialize_networked_module(godot::ModuleInitializationLevel level);
void uninitialize_networked_module(godot::ModuleInitializationLevel level);
#else
#include "modules/register_module_types.h"

void initialize_networked_module(ModuleInitializationLevel level);
void uninitialize_networked_module(ModuleInitializationLevel level);
#endif
