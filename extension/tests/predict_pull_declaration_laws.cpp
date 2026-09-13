#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"

namespace TestNetwPredictPullDeclaration {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionHandle;

namespace {

struct Branch {
    Node *node = nullptr;
    Ref<NetwMultiplayer> api;

    explicit Branch(const char *p_name) {
        node = memnew(Node);
        node->set_name(StringName(p_name));
        netw::gd::scene_root()->add_child(node);
        Ref<SceneMultiplayer> inner;
        inner.instantiate();
        inner->set_root_path(node->get_path());
        api.instantiate();
        api->session_set_inner(inner);
        netw::gd::scene_tree()->set_multiplayer(api, node->get_path());
    }

    ~Branch() {
        netw::gd::scene_tree()->set_multiplayer(
            Ref<MultiplayerAPI>(),
            node->get_path()
        );
        node->get_parent()->remove_child(node);
        memdelete(node);
    }
};

Ref<NetwEntity> mount(
    Branch &p_branch,
    const char *p_id,
    NetwPredict::Archetype p_archetype
) {
    Node *owner = memnew(Node);
    owner->set_name(StringName(p_id));
    NetwEntity::bind(owner, p_id, 1);
    const Ref<NetwEntity> entity = NetwEntity::of(owner);
    REQUIRE(entity.is_valid());
    entity->get_prediction()->set_archetype(p_archetype);
    p_branch.node->add_child(owner);
    return entity;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][SceneTree] a declared archetype seats its "
    "own slot when the owner is ready, with no verb from the game"
) {
    CHECK(netw::gd::scene_root() != nullptr);
    if (netw::gd::scene_root() == nullptr) {
        return;
    }
    Branch branch("pull_declared");
    const Ref<NetwEntity> entity
        = mount(branch, "declared", NetwPredict::ARCHETYPE_KINEMATIC);

    CHECK(branch.api->predict_engine_seated(entity->get_rid_handle()));
}

TEST_CASE(
    "[Networked][Predict][Hosted][SceneTree] an entity declaring no archetype "
    "seats no slot, so the default costs a game nothing"
) {
    CHECK(netw::gd::scene_root() != nullptr);
    if (netw::gd::scene_root() == nullptr) {
        return;
    }
    Branch branch("pull_undeclared");
    const Ref<NetwEntity> entity
        = mount(branch, "undeclared", NetwPredict::ARCHETYPE_NONE);

    CHECK_FALSE(branch.api->predict_engine_seated(entity->get_rid_handle()));
}

} // namespace TestNetwPredictPullDeclaration
