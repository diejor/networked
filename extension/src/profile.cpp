#include "netw/profile.hpp"

namespace netw::profile {

namespace names {

const char *const TICK_FRAME = "Networked tick";
const char *const CODEC_BYTES = "Networked codec bytes";
const char *const INTEREST_EDGES = "Networked interest edges";
const char *const INTEREST_TRANSITIONS = "Networked interest transitions";
const char *const PREDICT_CORRECTIONS = "Networked predict corrections";
const char *const PREDICT_REPLAY_DEPTH = "Networked predict replay_depth";
const char *const PREDICT_ACK_AGE = "Networked predict ack_age";
const char *const PREDICT_FALLBACKS = "Networked predict fallbacks";
const char *const PREDICT_PROBATION = "Networked predict probation";
const char *const PREDICT_DIVERGENCE = "Networked predict divergence";
const char *const PREDICT_TAP_DRAINED = "Networked predict tap_drained";
const char *const RPC_CALLS_DEFERRED = "Networked rpc calls_deferred";

} // namespace names

bool connected() {
#if defined(NETW_PROFILING)
    return TracyIsConnected;
#else
    return false;
#endif
}

void configure_plot(
    const char *name,
    PlotFormat format,
    bool step,
    bool fill,
    uint32_t color
) {
#if defined(NETW_PROFILING)
    tracy::PlotFormatType tracy_format = tracy::PlotFormatType::Number;
    if (format == PlotFormat::MEMORY) {
        tracy_format = tracy::PlotFormatType::Memory;
    } else if (format == PlotFormat::PERCENTAGE) {
        tracy_format = tracy::PlotFormatType::Percentage;
    }
    TracyPlotConfig(name, tracy_format, step, fill, color);
#else
    (void)name;
    (void)format;
    (void)step;
    (void)fill;
    (void)color;
#endif
}

void configure_plots() {
    NETW_PLOT_CONFIG(
        names::CODEC_BYTES,
        PlotFormat::MEMORY,
        true,
        false,
        colors::CODEC
    );
    // The two numbers that decide whether the interest fold is worth splitting
    // per forest. Edges is the size of the matrix being maintained, and
    // transitions is how much of it moved, so a session where the second stays
    // near zero against a large first is one where the fold is not the cost.
    NETW_PLOT_CONFIG(
        names::INTEREST_EDGES,
        PlotFormat::NUMBER,
        true,
        false,
        colors::INTEREST
    );
    NETW_PLOT_CONFIG(
        names::INTEREST_TRANSITIONS,
        PlotFormat::NUMBER,
        true,
        false,
        colors::INTEREST
    );
    NETW_PLOT_CONFIG(
        names::PREDICT_CORRECTIONS,
        PlotFormat::NUMBER,
        true,
        false,
        colors::PREDICTION
    );
    NETW_PLOT_CONFIG(
        names::PREDICT_REPLAY_DEPTH,
        PlotFormat::NUMBER,
        true,
        false,
        colors::PREDICTION
    );
    NETW_PLOT_CONFIG(
        names::PREDICT_ACK_AGE,
        PlotFormat::NUMBER,
        true,
        false,
        colors::PREDICTION
    );
    NETW_PLOT_CONFIG(
        names::PREDICT_FALLBACKS,
        PlotFormat::NUMBER,
        true,
        false,
        colors::PREDICTION
    );
    NETW_PLOT_CONFIG(
        names::PREDICT_PROBATION,
        PlotFormat::NUMBER,
        true,
        false,
        colors::PREDICTION
    );
    NETW_PLOT_CONFIG(
        names::PREDICT_DIVERGENCE,
        PlotFormat::NUMBER,
        false,
        false,
        colors::PREDICTION
    );
    NETW_PLOT_CONFIG(
        names::PREDICT_TAP_DRAINED,
        PlotFormat::NUMBER,
        true,
        false,
        colors::PREDICTION
    );
    NETW_PLOT_CONFIG(
        names::RPC_CALLS_DEFERRED,
        PlotFormat::NUMBER,
        true,
        false,
        colors::SESSION
    );
}

void startup() {
#if defined(NETW_PROFILING) && defined(NETW_GDEXTENSION)
    tracy::StartupProfiler();
#endif
}

void shutdown() {
#if defined(NETW_PROFILING) && defined(NETW_GDEXTENSION)
    tracy::ShutdownProfiler();
#endif
}

} // namespace netw::profile
