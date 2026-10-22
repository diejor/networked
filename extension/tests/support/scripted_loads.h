#pragma once

#include "netw_test.h"

#include <cstdint>

#include "godot/packed_scene.hpp"
#include "netw/scene_loads.hpp"

namespace netw_test {

class ScriptedLoads {
    static ScriptedLoads *&installed() {
        static ScriptedLoads *held = nullptr;
        return held;
    }

    static godot::Error request(const godot::String &) {
        installed()->requests += 1;
        return godot::OK;
    }

    static netw::gd::LoadStatus status(const godot::String &) {
        const ScriptedLoads *loads = installed();
        if (loads->loading) {
            return netw::gd::LOAD_IN_PROGRESS;
        }
        return loads->fails ? netw::gd::LOAD_FAILED : netw::gd::LOAD_LOADED;
    }

    static godot::Ref<godot::PackedScene> collect(
        const godot::String &p_path
    ) {
        return installed()->fails ? godot::Ref<godot::PackedScene>()
                                  : netw::gd::load_scene(p_path);
    }

public:
    bool loading = true;
    bool fails = false;
    int requests = 0;

    ScriptedLoads() {
        installed() = this;
        netw::SceneLoads::shared().clear();
        netw::SceneLoads::shared().set_loader(
            netw::SceneLoader{&request, &status, &collect}
        );
    }

    ~ScriptedLoads() {
        netw::SceneLoads::shared().clear();
        netw::SceneLoads::shared().set_loader(
            netw::SceneLoads::engine_loader()
        );
        installed() = nullptr;
    }

    ScriptedLoads(const ScriptedLoads &) = delete;
    ScriptedLoads &operator=(const ScriptedLoads &) = delete;

    void finish() {
        static uint64_t frame = uint64_t(1) << 40;
        loading = false;
        netw::SceneLoads::shared().poll(++frame);
    }
};

} // namespace netw_test
