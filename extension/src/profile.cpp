#include "netw/profile.hpp"

#include <memory>
#include <string>
#include <vector>

#if defined(NETW_PROFILING) && defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>
#include <godot_cpp/core/class_db.hpp>
#endif

namespace netw::profile {

#if defined(NETW_PROFILING) && defined(NETW_GDEXTENSION)

class NetwProfileLogger : public godot::Logger {
    GDCLASS(NetwProfileLogger, godot::Logger);

    static const char *origin(int32_t p_type) {
        switch (p_type) {
            case ERROR_TYPE_SCRIPT:
                return "script";
            case ERROR_TYPE_SHADER:
                return "shader";
            default:
                return "godot";
        }
    }

    static void send(const godot::String &p_line, uint32_t p_color) {
        const godot::CharString utf8 = p_line.utf8();
        TracyMessageC(utf8.get_data(), utf8.length(), p_color);
    }

protected:
    static void _bind_methods() {
    }

public:
    void _log_error(
        const godot::String &p_function,
        const godot::String &p_file,
        int32_t p_line,
        const godot::String &p_code,
        const godot::String &p_rationale,
        bool p_editor_notify,
        int32_t p_error_type,
        const godot::TypedArray<godot::Ref<godot::ScriptBacktrace>>
            &p_script_backtraces
    ) override {
        const godot::String text
            = p_rationale.is_empty() ? p_code : p_rationale;
        const bool warning = p_error_type == ERROR_TYPE_WARNING;
        godot::String where = p_file;
        int64_t at = p_line;
        const char *from = origin(p_error_type);
        if (!p_script_backtraces.is_empty()) {
            const godot::Ref<godot::ScriptBacktrace> trace
                = p_script_backtraces[0];
            if (trace.is_valid() && trace->get_frame_count() > 0) {
                where = trace->get_frame_file(0);
                at = trace->get_frame_line(0);
                from = "game";
            }
        }
        godot::String line = text;
        if (!text.begins_with("[")) {
            line = godot::String(warning ? "[warn][" : "[error][") + from + "] "
                + text;
        }
        line += " (" + where + ":" + godot::String::num_int64(at) + ")";
        send(line, warning ? colors::WARN : colors::ERROR);
    }

    void _log_message(const godot::String &p_message, bool p_error) override {
        if (p_error) {
            send(p_message.strip_edges(), colors::ERROR);
        }
    }
};

namespace {

godot::Ref<NetwProfileLogger> &installed_logger() {
    static godot::Ref<NetwProfileLogger> held;
    return held;
}

} // namespace

#endif

namespace names {

const char *const TICK_FRAME = "Networked tick";
const char *const PHYSICS_FRAME = "Networked physics frame";
const char *const POLL_FRAME = "Networked poll";
const char *const INTEREST_EDGES = "Networked interest edges";
const char *const INTEREST_TRANSITIONS = "Networked interest transitions";
const char *const PREDICT_CORRECTIONS = "Networked predict corrections";
const char *const PREDICT_TAP_DRAINED = "Networked predict tap drained";
const char *const RPC_CALLS_DEFERRED = "Networked rpc calls deferred";
const char *const ROWS_PULLED = "Networked rows pulled";
const char *const AWAKE_ROWS = "Networked awake rows";
const char *const SWEEP_POSITION = "Networked sweep position table";
const char *const ROWS_OWED = "Networked rows owed peer";
const char *const ROWS_SENT = "Networked rows sent peer";
const char *const BYTES_SENT = "Networked bytes sent peer";
const char *const BYTE_BUDGET = "Networked byte budget peer";
const char *const MEMORY_STORE_ROWS = "Memory store rows";
const char *const MEMORY_PEER_ROWS = "Memory peer rows";
const char *const MEMORY_PEER_FLIGHTS = "Memory peer flights";
const char *const MEMORY_PREDICT_TRANSITIONS = "Memory prediction transitions";
const char *const MEMORY_PREDICT_INPUTS = "Memory prediction inputs";
const char *const MEMORY_PREDICT_JOURNAL = "Memory prediction journal";
const char *const MEMORY_PREDICT_SLOTS = "Memory prediction slots";
const char *const MEMORY_DISPLAY = "Memory display runtimes";
const char *const MEMORY_BINDINGS = "Memory binding book";
const char *const MEMORY_SETS = "Memory set book";
const char *const MEMORY_ENTITIES = "Memory entity book";
const char *const MEMORY_COMPAT = "Memory compat rows";

} // namespace names

namespace {

struct PlotSpec {
    const char *const *name;
    PlotFormat format;
    uint32_t color;
};

const PlotSpec FIXED_PLOTS[] = {
    {&names::INTEREST_EDGES, PlotFormat::NUMBER, colors::INTEREST},
    {&names::INTEREST_TRANSITIONS, PlotFormat::NUMBER, colors::INTEREST},
    {&names::PREDICT_CORRECTIONS, PlotFormat::NUMBER, colors::PREDICTION},
    {&names::PREDICT_TAP_DRAINED, PlotFormat::NUMBER, colors::PREDICTION},
    {&names::RPC_CALLS_DEFERRED, PlotFormat::NUMBER, colors::SESSION},
    {&names::ROWS_PULLED, PlotFormat::NUMBER, colors::TABLE},
    {&names::AWAKE_ROWS, PlotFormat::NUMBER, colors::TABLE},
    {&names::MEMORY_STORE_ROWS, PlotFormat::MEMORY, colors::TABLE},
    {&names::MEMORY_PEER_ROWS, PlotFormat::MEMORY, colors::WIRE},
    {&names::MEMORY_PEER_FLIGHTS, PlotFormat::MEMORY, colors::TRANSPORT},
    {&names::MEMORY_PREDICT_TRANSITIONS,
     PlotFormat::MEMORY,
     colors::PREDICTION},
    {&names::MEMORY_PREDICT_INPUTS, PlotFormat::MEMORY, colors::PREDICTION},
    {&names::MEMORY_PREDICT_JOURNAL, PlotFormat::MEMORY, colors::PREDICTION},
    {&names::MEMORY_PREDICT_SLOTS, PlotFormat::MEMORY, colors::PREDICTION},
    {&names::MEMORY_DISPLAY, PlotFormat::MEMORY, colors::INTERP},
    {&names::MEMORY_BINDINGS, PlotFormat::MEMORY, colors::CODEC},
    {&names::MEMORY_SETS, PlotFormat::MEMORY, colors::CODEC},
    {&names::MEMORY_ENTITIES, PlotFormat::MEMORY, colors::LIVENESS},
    {&names::MEMORY_COMPAT, PlotFormat::MEMORY, colors::SESSION},
};

#if defined(NETW_PROFILING)

struct NamedPlot {
    const char *prefix;
    int64_t id;
    std::unique_ptr<std::string> text;
};

std::vector<NamedPlot> &named_plots() {
    static std::vector<NamedPlot> held;
    return held;
}

struct Tally {
    const char *name;
    int64_t count;
};

constexpr int TALLY_LIMIT = 8;
Tally tallies[TALLY_LIMIT] = {};
int tally_count = 0;

#endif

} // namespace

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
    for (const PlotSpec &spec : FIXED_PLOTS) {
        NETW_PLOT_CONFIG(*spec.name, spec.format, true, false, spec.color);
    }
}

const char *plot_name(
    const char *p_prefix,
    int64_t p_id,
    PlotFormat p_format,
    uint32_t p_color
) {
#if defined(NETW_PROFILING)
    std::vector<NamedPlot> &held = named_plots();
    for (const NamedPlot &plot : held) {
        if (plot.prefix == p_prefix && plot.id == p_id) {
            return plot.text->c_str();
        }
    }
    NamedPlot made;
    made.prefix = p_prefix;
    made.id = p_id;
    made.text = std::make_unique<std::string>(
        std::string(p_prefix) + " " + std::to_string(p_id)
    );
    const char *text = made.text->c_str();
    held.push_back(std::move(made));
    configure_plot(text, p_format, true, false, p_color);
    return text;
#else
    (void)p_id;
    (void)p_format;
    (void)p_color;
    return p_prefix;
#endif
}

void tally(const char *p_name, int64_t p_count) {
#if defined(NETW_PROFILING)
    for (int at = 0; at < tally_count; ++at) {
        if (tallies[at].name == p_name) {
            tallies[at].count += p_count;
            return;
        }
    }
    if (tally_count < TALLY_LIMIT) {
        tallies[tally_count++] = Tally{p_name, p_count};
    }
#else
    (void)p_name;
    (void)p_count;
#endif
}

void flush_tallies() {
#if defined(NETW_PROFILING)
    for (int at = 0; at < tally_count; ++at) {
        TracyPlot(tallies[at].name, tallies[at].count);
        tallies[at].count = 0;
    }
#endif
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

void install_logger() {
#if defined(NETW_PROFILING) && defined(NETW_GDEXTENSION)
    GDREGISTER_INTERNAL_CLASS(NetwProfileLogger);
    godot::Ref<NetwProfileLogger> &held = installed_logger();
    held.instantiate();
    godot::OS::get_singleton()->add_logger(held);
#endif
}

void remove_logger() {
#if defined(NETW_PROFILING) && defined(NETW_GDEXTENSION)
    godot::Ref<NetwProfileLogger> &held = installed_logger();
    if (held.is_valid()) {
        godot::OS::get_singleton()->remove_logger(held);
        held.unref();
    }
#endif
}

} // namespace netw::profile
