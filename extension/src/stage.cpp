#include "netw/stage.hpp"

#if defined(DEBUG_ENABLED)

#include <chrono>

#include "godot/engine.hpp"
#include "godot/engine_debugger.hpp"

using namespace godot;

namespace netw::stage {

const char *const NAMES[COUNT] = {
    "transport",
    "receive",
    "spawn",
    "apply",
    "interest",
    "lag_compensation",
    "rollback",
    "simulation",
    "pull",
    "send",
    "flush",
    "display",
    "other",
};

const char *const CATEGORY = "networked";

Book book;

namespace {

const StringName &servers() {
    static const StringName name("servers");
    return name;
}

} // namespace

uint64_t now_nanos() {
    using namespace std::chrono;
    return uint64_t(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch())
            .count()
    );
}

Stage enter(Stage p_stage, uint64_t p_now) {
    const Stage outer = book.open;
    if (outer != COUNT) {
        book.nanos[outer] += p_now - book.mark;
    }
    book.open = p_stage;
    book.mark = p_now;
    return outer;
}

void leave(Stage p_stage, Stage p_outer, uint64_t p_now) {
    book.nanos[p_stage] += p_now - book.mark;
    book.open = p_outer;
    book.mark = p_now;
}

bool turn(uint64_t p_frame) {
    if (p_frame == book.frame) {
        return false;
    }
    book.frame = p_frame;
    return true;
}

void set_timing(bool p_timing) {
    book.timing = p_timing;
}

Array take() {
    Array rows;
    if (book.timing) {
        rows.resize(1 + 2 * COUNT);
        rows[0] = String(CATEGORY);
        for (int at = 0; at < COUNT; ++at) {
            rows[1 + 2 * at] = String(NAMES[at]);
            rows[2 + 2 * at] = double(book.nanos[at]) / 1.0e9;
        }
    }
    for (int at = 0; at < COUNT; ++at) {
        book.nanos[at] = 0;
    }
    return rows;
}

void flush_frame() {
    if (!turn(Engine::get_singleton()->get_process_frames())) {
        return;
    }
    const Array rows = take();
    if (!rows.is_empty()) {
        gd::debugger_add_frame_data(servers(), rows);
    }
    set_timing(gd::debugger_profiling(servers()));
}

} // namespace netw::stage

#endif
