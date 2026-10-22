#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/packed_scene.hpp"

namespace netw {

struct SceneLoader {
    godot::Error (*request)(const godot::String &p_path) = nullptr;
    gd::LoadStatus (*status)(const godot::String &p_path) = nullptr;
    godot::Ref<godot::PackedScene> (*collect)(const godot::String &p_path)
        = nullptr;
};

class SceneLoads {
public:
    enum Status {
        STATUS_NONE,
        STATUS_LOADING,
        STATUS_READY,
        STATUS_FAILED,
    };

    static SceneLoader engine_loader();
    static SceneLoads &shared();

    explicit SceneLoads(const SceneLoader &p_loader);

    void set_loader(const SceneLoader &p_loader);

    Status begin(const godot::String &p_path);
    void poll(uint64_t p_frame);
    Status status(const godot::String &p_path) const;
    godot::Ref<godot::PackedScene> scene(const godot::String &p_path) const;
    void hold(const godot::String &p_path);
    void release(const godot::String &p_path);
    void clear();

private:
    struct Row {
        godot::String path;
        Status status = STATUS_LOADING;
        godot::Ref<godot::PackedScene> scene;
        int holders = 0;
    };

    SceneLoader loader;
    godot::LocalVector<Row> rows;
    uint64_t polled_frame = 0;
    bool polled = false;

    int index_of(const godot::String &p_path) const;
    void settle(Row &p_row, gd::LoadStatus p_status);
    void forget_unheld();
};

} // namespace netw
