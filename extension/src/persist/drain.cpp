#include "netw/persist/drain.hpp"

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/utility.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/api/promise.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw::persist {

namespace {

const char *SHUTDOWN_NOTICE = "Server is shutting down.";

} // namespace

NetwMultiplayer *Drain::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void Drain::open(NetwMultiplayer *p_session) {
    session_id = gd::instance_id(p_session);
    if (p_session != nullptr) {
        engines = p_session->persistence_live_engines();
    }
}

void Drain::note_database(const Variant &p_database) {
    Object *db = gd::live_object(p_database);
    if (db == nullptr) {
        return;
    }
    const ObjectID id = gd::instance_id(db);
    for (uint32_t at = 0; at < databases.size(); at++) {
        if (databases[at] == id) {
            return;
        }
    }
    databases.push_back(id);
}

bool Drain::flush_next_engine(const Callable &p_resume) {
    while (engine_at < engines.size()) {
        const Ref<NetwPersistenceEngine> engine
            = Object::cast_to<NetwPersistenceEngine>(engines[engine_at]);
        engine_at++;
        if (engine.is_null()) {
            continue;
        }
        note_database(engine->database());
        const Ref<NetwPromise> flushed = engine->flush(Array());
        if (flushed.is_null()) {
            continue;
        }
        flushed->when_settled(p_resume);
        return false;
    }
    return drain_next_database(p_resume);
}

bool Drain::drain_next_database(const Callable &p_resume) {
    while (database_at < databases.size()) {
        Object *db = gd::object_of(databases[database_at]);
        database_at++;
        if (db == nullptr) {
            continue;
        }
        const Variant backend = db->get(StringName("backend"));
        Object *backend_object = gd::live_object(backend);
        if (backend_object == nullptr
            || !backend_object->has_method(StringName("drain"))) {
            continue;
        }
        const Ref<NetwPromise> drained
            = Ref<NetwPromise>(backend_object->call(StringName("drain")));
        if (drained.is_null()) {
            continue;
        }
        drained->when_settled(p_resume);
        return false;
    }
    return true;
}

bool Drain::advance(const Callable &p_resume) {
    NETW_ZONE_NC("persistence drain advance", colors::TABLE);
    if (engine_at < engines.size()) {
        return flush_next_engine(p_resume);
    }
    return drain_next_database(p_resume);
}

} // namespace netw::persist

namespace netw {

void NetwMultiplayer::persistence_drain_start(double p_notify_delay) {
    if (persistence_drain != nullptr) {
        return;
    }
    persistence_drain = memnew(persist::Drain);
    persistence_drain->open(this);

    SceneTree *tree = gd::scene_tree();
    if (!is_host() || tree == nullptr) {
        persistence_drain_advance();
        return;
    }
    session_notify_shutdown(persist::SHUTDOWN_NOTICE);
    Ref<SceneTreeTimer> grace = tree->create_timer(p_notify_delay);
    grace->connect(
        StringName("timeout"),
        callable_mp(this, &NetwMultiplayer::persistence_drain_advance),
        Object::CONNECT_ONE_SHOT
    );
}

void NetwMultiplayer::persistence_drain_advance() {
    if (persistence_drain == nullptr) {
        return;
    }
    const bool finished = persistence_drain->advance(
        callable_mp(this, &NetwMultiplayer::persistence_drain_advance)
    );
    if (!finished) {
        return;
    }
    NETW_TRACE(sys::TABLE, "persistence shutdown drain finished");
    persistence_drain_forget();
    if (SceneTree *tree = gd::scene_tree()) {
        tree->quit();
    }
}

void NetwMultiplayer::persistence_drain_forget() {
    if (persistence_drain != nullptr) {
        godot::memdelete(persistence_drain);
        persistence_drain = nullptr;
    }
}

} // namespace netw
