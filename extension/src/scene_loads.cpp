#include "netw/scene_loads.hpp"

using namespace godot;

namespace netw {

SceneLoader SceneLoads::engine_loader() {
    return SceneLoader{&gd::load_begin, &gd::load_status, &gd::load_collect};
}

SceneLoads &SceneLoads::shared() {
    static SceneLoads instance(engine_loader());
    return instance;
}

SceneLoads::SceneLoads(const SceneLoader &p_loader) : loader(p_loader) {
}

void SceneLoads::set_loader(const SceneLoader &p_loader) {
    loader = p_loader;
}

SceneLoads::Status SceneLoads::begin(const String &p_path) {
    const int found = index_of(p_path);
    if (found >= 0) {
        return rows[found].status;
    }
    Row row;
    row.path = p_path;
    if (loader.request(p_path) != OK) {
        row.status = STATUS_FAILED;
    } else {
        settle(row, loader.status(p_path));
    }
    rows.push_back(row);
    return row.status;
}

void SceneLoads::poll(uint64_t p_frame) {
    if (polled && p_frame == polled_frame) {
        return;
    }
    polled = true;
    polled_frame = p_frame;
    for (uint32_t index = 0; index < rows.size(); ++index) {
        if (rows[index].status == STATUS_LOADING) {
            settle(rows[index], loader.status(rows[index].path));
        }
    }
    forget_unheld();
}

SceneLoads::Status SceneLoads::status(const String &p_path) const {
    const int found = index_of(p_path);
    return found >= 0 ? rows[found].status : STATUS_NONE;
}

Ref<PackedScene> SceneLoads::scene(const String &p_path) const {
    const int found = index_of(p_path);
    return found >= 0 ? rows[found].scene : Ref<PackedScene>();
}

void SceneLoads::hold(const String &p_path) {
    const int found = index_of(p_path);
    if (found >= 0) {
        rows[found].holders += 1;
    }
}

void SceneLoads::release(const String &p_path) {
    const int found = index_of(p_path);
    if (found < 0 || rows[found].holders == 0) {
        return;
    }
    rows[found].holders -= 1;
    if (rows[found].holders == 0 && rows[found].status != STATUS_LOADING) {
        rows.remove_at(found);
    }
}

void SceneLoads::clear() {
    rows.clear();
    polled = false;
}

int SceneLoads::index_of(const String &p_path) const {
    for (uint32_t index = 0; index < rows.size(); ++index) {
        if (rows[index].path == p_path) {
            return int(index);
        }
    }
    return -1;
}

void SceneLoads::settle(Row &p_row, gd::LoadStatus p_status) {
    if (p_status == gd::LOAD_IN_PROGRESS) {
        p_row.status = STATUS_LOADING;
        return;
    }
    p_row.scene = loader.collect(p_row.path);
    p_row.status = p_row.scene.is_valid() ? STATUS_READY : STATUS_FAILED;
}

void SceneLoads::forget_unheld() {
    uint32_t index = 0;
    while (index < rows.size()) {
        if (rows[index].holders == 0 && rows[index].status != STATUS_LOADING) {
            rows.remove_at(index);
        } else {
            index += 1;
        }
    }
}

} // namespace netw
