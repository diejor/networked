#include "support/netw_test.h"

#include "netw/scene_core.hpp"

#include "godot/rid.hpp"
#include "godot/utility.hpp"
#include "godot/variant.hpp"

namespace TestNetwSceneDeclarationLaws {

using namespace godot;
using netw::NetwSceneCore;

const int ISOLATION_NONE = 0;
const int ISOLATION_OWN_WORLD = 1;

Ref<NetwSceneCore> fresh() {
    Ref<NetwSceneCore> core;
    core.instantiate();
    return core;
}

StringName stem(const char *p_name) {
    return StringName(p_name);
}

void row(
    const Ref<NetwSceneCore> &p_core,
    const char *p_stem,
    const char *p_path,
    const Variant &p_spawn_data,
    bool p_initial
) {
    p_core->declaration_row(
        StringName(p_stem),
        String(p_path),
        p_spawn_data,
        p_initial
    );
}

Dictionary round_of(int p_round) {
    Dictionary out;
    out[stem("round")] = p_round;
    return out;
}

int round_in(const Variant &p_value) {
    return int(Dictionary(p_value)[stem("round")]);
}

Variant spawn_data(const Ref<NetwSceneCore> &p_core, const char *p_stem) {
    return p_core->declared_spawn_data(StringName(p_stem), Variant());
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD1 a published container is detached from "
    "the caller's, so an authoring edit after the fact reaches nothing"
) {
    Ref<NetwSceneCore> core = fresh();
    Dictionary authored = round_of(3);

    core->declaration_open(ISOLATION_NONE, Callable());
    row(core, "Arena", "res://arena.tscn", authored, true);
    core->declaration_publish();

    authored[stem("round")] = 9;
    authored[stem("added")] = true;

    const Variant held = spawn_data(core, "Arena");
    NETW_CHECK_EQ(round_in(held), 3);
    CHECK_FALSE(bool(Dictionary(held).has(stem("added"))));

    Dictionary answered = Dictionary(held);
    answered[stem("round")] = 11;

    NETW_CHECK_EQ(round_in(spawn_data(core, "Arena")), 3);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD2 a draft is unreadable until it is "
    "published, so a re-publish is never half seen"
) {
    Ref<NetwSceneCore> core = fresh();

    core->declaration_open(ISOLATION_NONE, Callable());
    row(core, "Lobby", "res://lobby.tscn", Variant(), true);

    CHECK_FALSE(core->declaration_is_published());
    NETW_CHECK_EQ(core->declared_scene_count(), 0);
    CHECK_FALSE(core->declares_scene(stem("Lobby")));

    core->declaration_publish();

    CHECK(core->declaration_is_published());
    NETW_CHECK_EQ(core->declared_scene_count(), 1);

    core->declaration_open(ISOLATION_OWN_WORLD, Callable());
    row(core, "Arena", "res://arena.tscn", Variant(), false);

    NETW_CHECK_EQ(core->declared_isolation(), ISOLATION_NONE);
    CHECK(core->declares_scene(stem("Lobby")));
    CHECK_FALSE(core->declares_scene(stem("Arena")));

    core->declaration_publish();

    NETW_CHECK_EQ(core->declared_isolation(), ISOLATION_OWN_WORLD);
    CHECK_FALSE(core->declares_scene(stem("Lobby")));
    CHECK(core->declares_scene(stem("Arena")));
    NETW_CHECK_EQ(core->declared_scene_count(), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD3 a row offered with no draft open declares "
    "nothing, so a stray write cannot amend a live declaration"
) {
    Ref<NetwSceneCore> core = fresh();

    core->declaration_open(ISOLATION_NONE, Callable());
    row(core, "Arena", "res://arena.tscn", Variant(), true);
    core->declaration_publish();

    row(core, "Annex", "res://annex.tscn", Variant(), true);
    core->declaration_publish();

    CHECK_FALSE(core->declares_scene(stem("Annex")));
    NETW_CHECK_EQ(core->declared_scene_count(), 1);

    core->declaration_open(ISOLATION_NONE, Callable());
    row(core, "", "res://nameless.tscn", Variant(), true);
    core->declaration_publish();

    NETW_CHECK_EQ(core->declared_scene_count(), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD4 one stem is one row, and being named "
    "again merges rather than doubling"
) {
    Ref<NetwSceneCore> core = fresh();

    core->declaration_open(ISOLATION_NONE, Callable());
    row(core, "Arena", "res://arena.tscn", Variant(), false);
    row(core, "Arena", "", round_of(7), true);
    row(core, "Arena", "", Variant(), false);
    core->declaration_publish();

    NETW_CHECK_EQ(core->declared_scene_count(), 1);
    CHECK(bool(
        core->declared_scene_path(stem("Arena")) == String("res://arena.tscn")
    ));
    NETW_CHECK_EQ(round_in(spawn_data(core, "Arena")), 7);
    NETW_CHECK_EQ(core->declared_initial_stems().size(), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD5 the initial stems answer in declaration "
    "order, which is the order a starting session brings them online"
) {
    Ref<NetwSceneCore> core = fresh();

    core->declaration_open(ISOLATION_NONE, Callable());
    row(core, "Lobby", "res://lobby.tscn", Variant(), true);
    row(core, "Arena", "res://arena.tscn", Variant(), false);
    row(core, "Annex", "res://annex.tscn", Variant(), true);
    core->declaration_publish();

    const Array initial = core->declared_initial_stems();
    NETW_CHECK_EQ(initial.size(), 2);
    CHECK(bool(StringName(initial[0]) == stem("Lobby")));
    CHECK(bool(StringName(initial[1]) == stem("Annex")));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD6 a stem with no declared spawn data "
    "answers the fallback, so a caller still names the scene it wants"
) {
    Ref<NetwSceneCore> core = fresh();

    NETW_CHECK_EQ(int(core->declared_spawn_data(stem("Arena"), 42)), 42);
    CHECK(core->declared_scene_path(stem("Arena")).is_empty());

    core->declaration_open(ISOLATION_NONE, Callable());
    row(core, "Arena", "res://arena.tscn", Variant(), true);
    core->declaration_publish();

    NETW_CHECK_EQ(int(core->declared_spawn_data(stem("Arena"), 42)), 42);
    NETW_CHECK_EQ(int(core->declared_spawn_data(stem("Missing"), 42)), 42);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SD7 clearing the live book keeps the "
    "declaration, and only dropping it forgets it"
) {
    Ref<NetwSceneCore> core = fresh();

    core->declaration_open(ISOLATION_OWN_WORLD, Callable());
    row(core, "Arena", "res://arena.tscn", Variant(), true);
    core->declaration_publish();

    core->clear();

    CHECK(core->declaration_is_published());
    CHECK(core->declares_scene(stem("Arena")));
    NETW_CHECK_EQ(core->declared_isolation(), ISOLATION_OWN_WORLD);

    core->declaration_drop();

    CHECK_FALSE(core->declaration_is_published());
    CHECK_FALSE(core->declares_scene(stem("Arena")));
    NETW_CHECK_EQ(core->declared_isolation(), ISOLATION_NONE);
    NETW_CHECK_EQ(core->declared_initial_stems().size(), 0);
}

} // namespace TestNetwSceneDeclarationLaws
