#pragma once

#if defined(NETW_MODULE)
// A module shares the engine's binary, so it shares the engine's Tracy client.
// This header defines TRACY_ENABLE when the engine is built profiler=tracy.
#include "core/profiling/profiling.h"
#elif defined(NETW_GDEXTENSION)
// A shared library carries its own client.
#include <tracy/Tracy.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif
