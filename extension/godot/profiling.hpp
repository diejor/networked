#pragma once

#if defined(NETW_MODULE)
#include "core/profiling/profiling.h"
#elif defined(NETW_GDEXTENSION)
#include <tracy/Tracy.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif
