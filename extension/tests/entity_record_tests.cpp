#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)
#include "godot/script.hpp"
#include <godot_cpp/classes/class_db_singleton.hpp>
#endif

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/entity/ids.hpp"
#include "netw/entity/stage.hpp"
#include "netw/scene_decl.hpp"
#include "netw/script/model.hpp"
#include <memory>

#include "support/entity_facets.h"
#include "support/netw_call_log.h"

namespace TestNetwEntityRecord {

using namespace godot;
using netw::NetwEntityRecord;
using netw::entity::Stage;
using netw_test::CallLog;
using netw_test::EntityFactories;

struct Record {
    NetwEntityRecord *row = memnew(NetwEntityRecord);

    Record() = default;
    Record(const Record &) = delete;
    Record &operator=(const Record &) = delete;

    ~Record() {
        unref();
    }

    void unref() {
        if (row != nullptr) {
            godot::memdelete(row);
            row = nullptr;
        }
    }

    NetwEntityRecord *operator->() const {
        return row;
    }

    operator NetwEntityRecord *() const {
        return row;
    }
};

class WindowProbe final : public godot::CallableCustom {
    NetwEntityRecord *record = nullptr;
    std::shared_ptr<bool> held;
    godot::ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    WindowProbe(
        NetwEntityRecord *p_record,
        const std::shared_ptr<bool> &p_held,
        godot::Object *p_anchor
    )
        : record(p_record), held(p_held),
          anchor(netw::gd::instance_id(p_anchor)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("WindowProbe");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &WindowProbe::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &WindowProbe::before;
    }

    godot::ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **,
        int,
        Variant &,
        netw::gd::CallError &r_call_error
    ) const override {
        *held = record->get_active_despawn_opts().is_valid();
        netw::gd::call_ok(r_call_error);
    }
};

class DenyingSink final : public godot::CallableCustom {
    godot::ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    explicit DenyingSink(const godot::Object *p_anchor)
        : anchor(netw::gd::instance_id(p_anchor)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("DenyingSink");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &DenyingSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &DenyingSink::before;
    }

    godot::ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **p_arguments,
        int p_count,
        Variant &,
        netw::gd::CallError &r_call_error
    ) const override {
        if (p_count >= 2) {
            Object *carried = *p_arguments[1];
            netw::NetwControlRequest *request
                = Object::cast_to<netw::NetwControlRequest>(carried);
            if (request != nullptr) {
                request->deny();
            }
        }
        netw::gd::call_ok(r_call_error);
    }
};

Ref<RefCounted> make_wrapper() {
    Ref<RefCounted> wrapper;
    wrapper.instantiate();
    return wrapper;
}

TEST_CASE(
    "[Networked][Entity][Hosted] R1 a record is born whole and shares no part"
) {
    Record a;
    Record b;

    CHECK(netw::entity::minted(a->get_handle()));
    CHECK(netw::entity::minted(b->get_handle()));
    CHECK(a->get_handle() != b->get_handle());
    NETW_CHECK_EQ(netw::entity::holders(a->get_handle()), 1);

    CHECK(a->get_control() != nullptr);
    CHECK(a->get_control() != b->get_control());
    a->get_control()->set_controller(7);
    NETW_CHECK_EQ(b->get_control()->get_controller(), 0);

    SUBCASE("a fresh record is unbound, unrouted and unnamed") {
        NETW_CHECK_EQ(a->get_stage(), int(Stage::UNBOUND));
        NETW_CHECK_EQ(a->get_route(), 0);
        NETW_CHECK_EQ(a->get_peer_id(), 0);
        CHECK(a->get_entity_id() == StringName());
    }

    SUBCASE("the stage moves without disturbing its neighbour") {
        CHECK(a->advance(int(Stage::ARMED)));
        NETW_CHECK_EQ(b->get_stage(), int(Stage::UNBOUND));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R2 a record's handle lives exactly as long as "
    "the record"
) {
    const int before = netw::entity::outstanding();
    RID handle;
    {
        Record record;
        handle = record->get_handle();
        NETW_CHECK_EQ(netw::entity::outstanding(), before + 1);
        CHECK(netw::entity::minted(handle));
    }
    CHECK_FALSE(netw::entity::minted(handle));
    NETW_CHECK_EQ(netw::entity::outstanding(), before);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R3 adopting a handle lets go of the one it "
    "replaces"
) {
    const int before = netw::entity::outstanding();
    Record standing;
    Record arriving;
    const RID birth = arriving->get_handle();
    const RID held = standing->get_handle();

    CHECK(arriving->adopt_handle(held));

    CHECK(arriving->get_handle() == held);
    CHECK_FALSE(netw::entity::minted(birth));
    NETW_CHECK_EQ(netw::entity::holders(held), 2);
    NETW_CHECK_EQ(netw::entity::outstanding(), before + 1);

    SUBCASE("adopting what it already holds costs nothing") {
        CHECK(arriving->adopt_handle(held));
        NETW_CHECK_EQ(netw::entity::holders(held), 2);
    }

    SUBCASE("a handle the mint never issued is refused, and nothing is lost") {
        CHECK_FALSE(arriving->adopt_handle(RID()));
        CHECK(arriving->get_handle() == held);
        NETW_CHECK_EQ(netw::entity::holders(held), 2);
    }

    SUBCASE("the standing record letting go leaves the adopter holding") {
        standing.unref();
        CHECK(netw::entity::minted(held));
        NETW_CHECK_EQ(netw::entity::holders(held), 1);
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R4 a record moves by the stage table and a "
    "refused move changes nothing"
) {
    Record record;

    CHECK(record->advance(int(Stage::ARMED)));
    CHECK(record->advance(int(Stage::LIVE)));
    NETW_CHECK_EQ(record->get_stage(), int(Stage::LIVE));

    SUBCASE("a stage is never re-entered") {
        CHECK_FALSE(record->advance(int(Stage::LIVE)));
        NETW_CHECK_EQ(record->get_stage(), int(Stage::LIVE));
    }

    SUBCASE("a backward move is refused and costs the record nothing") {
        CHECK_FALSE(record->advance(int(Stage::ARMED)));
        NETW_CHECK_EQ(record->get_stage(), int(Stage::LIVE));
    }

    SUBCASE("the terminal stage is terminal") {
        CHECK(record->advance(int(Stage::DESPAWNING)));
        CHECK(record->advance(int(Stage::FREED)));
        CHECK_FALSE(record->advance(int(Stage::LIVE)));
        CHECK_FALSE(record->advance(int(Stage::ARMED)));
        NETW_CHECK_EQ(record->get_stage(), int(Stage::FREED));
    }

    SUBCASE("the record has no second opinion about the table") {
        for (int from = 0; from <= int(Stage::FREED); ++from) {
            for (int to = 0; to <= int(Stage::FREED); ++to) {
                Record probe;
                if (!netw::entity::stage_edge_is_legal(
                        int(Stage::UNBOUND),
                        from
                    )
                    && from != int(Stage::UNBOUND)) {
                    continue;
                }
                if (from != int(Stage::UNBOUND)) {
                    probe->advance(from);
                }
                NETW_CHECK_EQ(
                    probe->advance(to),
                    netw::entity::stage_edge_is_legal(from, to)
                );
            }
        }
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R5 a facet is minted once and answered "
    "forever"
) {
    EntityFactories factories;
    CallLog log;
    NetwEntityRecord::set_part_factory(
        NetwEntityRecord::PART_SCENE,
        log.minting("scene")
    );
    Record record;
    Ref<RefCounted> wrapper = make_wrapper();

    const Ref<RefCounted> first
        = record->part(NetwEntityRecord::PART_SCENE, wrapper.ptr());

    CHECK(first.is_valid());
    NETW_CHECK_EQ(log.count("scene"), 1);
    REQUIRE(log.args("scene").size() == 1);
    CHECK(
        Object::cast_to<Object>(log.args("scene")[0])
        == static_cast<Object *>(wrapper.ptr())
    );

    const Ref<RefCounted> again
        = record->part(NetwEntityRecord::PART_SCENE, wrapper.ptr());
    CHECK(again == first);
    NETW_CHECK_EQ(log.count("scene"), 1);

    Record other;
    const Ref<RefCounted> theirs
        = other->part(NetwEntityRecord::PART_SCENE, make_wrapper().ptr());
    CHECK(theirs.is_valid());
    CHECK(theirs != first);
    NETW_CHECK_EQ(log.count("scene"), 2);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R6 a facet a record cannot build is nothing, "
    "not a guess"
) {
    EntityFactories factories;
    Record record;
    Ref<RefCounted> wrapper = make_wrapper();

    CHECK_FALSE(
        NetwEntityRecord::has_part_factory(NetwEntityRecord::PART_INTEREST)
    );
    CHECK(
        Ref<RefCounted>(
            record->part(NetwEntityRecord::PART_INTEREST, wrapper.ptr())
        )
            .is_null()
    );

    CHECK(
        Ref<RefCounted>(record->part(NetwEntityRecord::PART_MAX, wrapper.ptr()))
            .is_null()
    );
    CHECK(Ref<RefCounted>(record->part(-1, wrapper.ptr())).is_null());
    CHECK_FALSE(NetwEntityRecord::has_part_factory(-1));

    CallLog log;
    NetwEntityRecord::set_part_factory(
        NetwEntityRecord::PART_INTEREST,
        log.minting("interest")
    );
    CHECK(
        Ref<RefCounted>(
            record->part(NetwEntityRecord::PART_INTEREST, wrapper.ptr())
        )
            .is_valid()
    );
    NETW_CHECK_EQ(log.count("interest"), 1);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R7 a record's facets end with the record"
) {
    EntityFactories factories;
    CallLog log;
    NetwEntityRecord::set_part_factory(
        NetwEntityRecord::PART_DISPLAY,
        log.minting("display")
    );
    Ref<RefCounted> wrapper = make_wrapper();
    Ref<RefCounted> facet;
    {
        Record record;
        facet = record->part(NetwEntityRecord::PART_DISPLAY, wrapper.ptr());
        REQUIRE(facet.is_valid());
        NETW_CHECK_EQ(facet->get_reference_count(), 2);
    }
    NETW_CHECK_EQ(facet->get_reference_count(), 1);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R8 a deactivated owner stops processing, "
    "showing and replicating"
) {
    Record record;
    Node *owner = memnew(Node);
    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    owner->add_child(sync);
    sync->set_owner(owner);
    sync->set_root_path(NodePath(".."));

    record->deactivate(owner);

    NETW_CHECK_EQ(owner->get_process_mode(), Node::PROCESS_MODE_DISABLED);
    CHECK_FALSE(sync->get_visibility_for(0));
    CHECK(sync->get_visibility_for(1));

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R9 a template is declared once and stays "
    "terminal"
) {
    Record record;
    Node *owner = memnew(Node);

    CHECK(record->mark_template(owner));
    NETW_CHECK_EQ(record->get_stage(), int(Stage::TEMPLATE));

    CHECK(record->mark_template(owner));
    NETW_CHECK_EQ(record->get_stage(), int(Stage::TEMPLATE));

    SUBCASE("a marked record is not carried down the go-live path") {
        CHECK_FALSE(record->classify_activation(owner));
    }

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R10 what an unbound record on a node in the "
    "tree IS, decided once"
) {
    Node *scene = memnew(Node);

    SUBCASE("a bound identity goes live") {
        Record record;
        Node *owner = memnew(Node);
        record->set_entity_id("crate");
        CHECK(record->classify_activation(owner));
        NETW_CHECK_EQ(record->get_stage(), int(Stage::UNBOUND));
        memdelete(owner);
    }

    SUBCASE("an editor-placed factory declares itself a template") {
        Record record;
        Node *owner = memnew(Node);
        scene->add_child(owner);
        owner->set_owner(scene);
        CHECK(NetwEntityRecord::declares_template(owner));
        CHECK_FALSE(record->classify_activation(owner));
        NETW_CHECK_EQ(record->get_stage(), int(Stage::TEMPLATE));
    }

    SUBCASE("a scene assembled without ownership declares it by mark") {
        Record record;
        Node *owner = memnew(Node);
        owner->set_meta(NetwEntityRecord::template_meta(), true);
        CHECK(NetwEntityRecord::declares_template(owner));
        CHECK_FALSE(record->classify_activation(owner));
        NETW_CHECK_EQ(record->get_stage(), int(Stage::TEMPLATE));
        memdelete(owner);
    }

    SUBCASE("a bare programmatic node stays inert rather than becoming one") {
        Record record;
        Node *owner = memnew(Node);
        CHECK_FALSE(NetwEntityRecord::declares_template(owner));
        CHECK_FALSE(record->classify_activation(owner));
        NETW_CHECK_EQ(record->get_stage(), int(Stage::UNBOUND));
        memdelete(owner);
    }

    SUBCASE("a record past UNBOUND is already classified") {
        Record record;
        Node *owner = memnew(Node);
        REQUIRE(record->advance(int(Stage::ARMED)));
        CHECK(record->classify_activation(owner));
        memdelete(owner);
    }

    memdelete(scene);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R11 a teardown window opens once, and the "
    "options are in flight only inside it"
) {
    Record record;
    Ref<RefCounted> wrapper = make_wrapper();
    netw::gd::add_signal(wrapper.ptr(), "despawning", 1);
    auto seen = std::make_shared<bool>(false);
    wrapper->connect(
        "despawning",
        Callable(memnew(WindowProbe(record, seen, wrapper.ptr())))
    );
    REQUIRE(record->advance(int(Stage::ARMED)));
    Ref<netw::NetwDespawnOpts> opts;
    opts.instantiate();
    opts->set_reason("killed");

    CHECK(record->begin_despawn(wrapper.ptr(), nullptr, opts));

    NETW_CHECK_EQ(record->get_stage(), int(Stage::DESPAWNING));
    CHECK(*seen);
    CHECK(record->get_active_despawn_opts().is_null());

    SUBCASE("a second teardown is refused rather than announced twice") {
        *seen = false;
        CHECK_FALSE(record->begin_despawn(wrapper.ptr(), nullptr, opts));
        CHECK_FALSE(*seen);
        NETW_CHECK_EQ(record->get_stage(), int(Stage::DESPAWNING));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R12 a record that cannot tear down changes "
    "nothing"
) {
    Record record;
    Ref<RefCounted> wrapper = make_wrapper();
    REQUIRE(record->mark_template(nullptr));

    Ref<netw::NetwDespawnOpts> none;
    CHECK_FALSE(record->begin_despawn(wrapper.ptr(), nullptr, none));
    NETW_CHECK_EQ(record->get_stage(), int(Stage::TEMPLATE));
    CHECK(record->get_active_despawn_opts().is_null());

    SUBCASE("an unbound record with no options tears down by default") {
        Record fresh;
        CHECK(fresh->begin_despawn(wrapper.ptr(), nullptr, none));
        NETW_CHECK_EQ(fresh->get_stage(), int(Stage::DESPAWNING));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R13 authority follows the controller, and the "
    "server is the one peer the two sides spell differently"
) {
    Record record;
    Ref<RefCounted> wrapper = make_wrapper();
    netw::gd::add_signal(wrapper.ptr(), "control_changed", 2);
    netw_test::CallLog log;
    wrapper->connect("control_changed", log.callable("moved"));
    Node *owner = memnew(Node);

    record->get_control()->set_controller(7);
    record->apply_control(wrapper.ptr(), owner, true);

    NETW_CHECK_EQ(owner->get_multiplayer_authority(), 7);
    NETW_CHECK_EQ(log.count("moved"), 1);
    REQUIRE(log.args("moved").size() == 2);
    NETW_CHECK_EQ(int(log.args("moved")[0]), 0);
    NETW_CHECK_EQ(int(log.args("moved")[1]), 7);

    SUBCASE("the server takes the engine's own name for itself") {
        record->get_control()->set_controller(0);
        record->apply_control(wrapper.ptr(), owner, true);
        NETW_CHECK_EQ(owner->get_multiplayer_authority(), 1);
        NETW_CHECK_EQ(log.count("moved"), 2);
    }

    SUBCASE("a re-apply that moves nothing announces nothing") {
        record->apply_control(wrapper.ptr(), owner, true);
        NETW_CHECK_EQ(log.count("moved"), 1);
    }

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R14 an authority write recurses everywhere "
    "but mid-tree-entry"
) {
    CHECK_FALSE(NetwEntityRecord::control_recurses(false, true, false));

    CHECK(NetwEntityRecord::control_recurses(true, true, false));
    CHECK(NetwEntityRecord::control_recurses(false, false, false));
    CHECK(NetwEntityRecord::control_recurses(false, true, true));
    CHECK(NetwEntityRecord::control_recurses(true, false, false));
    CHECK(NetwEntityRecord::control_recurses(true, true, true));
    CHECK(NetwEntityRecord::control_recurses(false, false, true));
    CHECK(NetwEntityRecord::control_recurses(true, false, true));
}

TEST_CASE(
    "[Networked][Entity][Hosted] R15 a recursive write reaches the children "
    "the entity replicates through"
) {
    Record record;
    Node *owner = memnew(Node);
    Node *child = memnew(Node);
    owner->add_child(child);

    record->get_control()->set_controller(4);
    record->apply_control(nullptr, owner, true);

    NETW_CHECK_EQ(owner->get_multiplayer_authority(), 4);
    NETW_CHECK_EQ(child->get_multiplayer_authority(), 4);

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R16 only a teardown already in flight ends at "
    "freed"
) {
    Record record;
    Ref<RefCounted> wrapper = make_wrapper();
    netw::gd::add_signal(wrapper.ptr(), "despawned");
    netw_test::CallLog log;
    wrapper->connect("despawned", log.callable("gone"));
    REQUIRE(record->advance(int(Stage::ARMED)));
    REQUIRE(record->advance(int(Stage::LIVE)));

    CHECK_FALSE(record->finish_teardown(wrapper.ptr()));
    NETW_CHECK_EQ(record->get_stage(), int(Stage::LIVE));
    NETW_CHECK_EQ(log.count("gone"), 0);

    SUBCASE("a despawning record ends and says so once") {
        REQUIRE(record->advance(int(Stage::DESPAWNING)));
        CHECK(record->finish_teardown(wrapper.ptr()));
        NETW_CHECK_EQ(record->get_stage(), int(Stage::FREED));
        NETW_CHECK_EQ(log.count("gone"), 1);

        CHECK_FALSE(record->finish_teardown(wrapper.ptr()));
        NETW_CHECK_EQ(log.count("gone"), 1);
    }

    SUBCASE("a lingering record ends the same way") {
        REQUIRE(record->advance(int(Stage::DESPAWNING)));
        REQUIRE(record->advance(int(Stage::LINGERING)));
        CHECK(record->finish_teardown(wrapper.ptr()));
        NETW_CHECK_EQ(record->get_stage(), int(Stage::FREED));
        NETW_CHECK_EQ(log.count("gone"), 1);
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R17 a control request is arbitrated once, and "
    "the strictest listener wins"
) {
    Record record;
    Ref<RefCounted> wrapper = make_wrapper();
    netw::gd::add_signal(wrapper.ptr(), "control_requested", 2);
    netw_test::CallLog log;
    wrapper->connect("control_requested", log.callable("asked"));

    NETW_CHECK_EQ(record->admit_control_request(wrapper.ptr(), 9), 0);
    NETW_CHECK_EQ(log.count("asked"), 0);

    SUBCASE("a requestable entity asks, and grants when nobody refuses") {
        record->get_control()->set_transfer(
            int(netw::entity::Control::Transfer::REQUESTABLE)
        );
        NETW_CHECK_EQ(record->admit_control_request(wrapper.ptr(), 9), 9);
        NETW_CHECK_EQ(log.count("asked"), 1);
        REQUIRE(log.args("asked").size() == 2);
        NETW_CHECK_EQ(int(log.args("asked")[0]), 9);
    }

    SUBCASE("a listener that denies stops the grant") {
        record->get_control()->set_transfer(
            int(netw::entity::Control::Transfer::REQUESTABLE)
        );
        wrapper->connect(
            "control_requested",
            Callable(memnew(DenyingSink(wrapper.ptr())))
        );
        NETW_CHECK_EQ(record->admit_control_request(wrapper.ptr(), 9), 0);
        NETW_CHECK_EQ(log.count("asked"), 1);
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R18 an unset identity is filled from the node "
    "name, and a bound one is not"
) {
    Record record;
    Node *owner = memnew(Node);
    owner->set_name("valeria|7");

    record->hydrate_identity(owner);

    CHECK(record->get_entity_id() == StringName("valeria"));
    NETW_CHECK_EQ(record->get_peer_id(), 7);

    SUBCASE("a caller that already bound the identity keeps it") {
        Record bound;
        bound->set_entity_id("crate");
        bound->set_peer_id(3);
        bound->hydrate_identity(owner);
        CHECK(bound->get_entity_id() == StringName("crate"));
        NETW_CHECK_EQ(bound->get_peer_id(), 3);
    }

    SUBCASE("a name that spells no identity fills nothing") {
        Record plain;
        owner->set_name("JustANode");
        plain->hydrate_identity(owner);
        CHECK(plain->get_entity_id() == StringName());
        NETW_CHECK_EQ(plain->get_peer_id(), 0);
    }

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R19 the controller is written once and the "
    "move is announced once"
) {
    Record record;
    Ref<RefCounted> wrapper = make_wrapper();
    netw::gd::add_signal(wrapper.ptr(), "control_changed", 2);
    netw_test::CallLog log;
    wrapper->connect("control_changed", log.callable("moved"));

    CHECK(record->set_controller(wrapper.ptr(), 5));

    NETW_CHECK_EQ(record->get_control()->get_controller(), 5);
    NETW_CHECK_EQ(log.count("moved"), 1);
    REQUIRE(log.args("moved").size() == 2);
    NETW_CHECK_EQ(int(log.args("moved")[0]), 0);
    NETW_CHECK_EQ(int(log.args("moved")[1]), 5);

    SUBCASE("writing the value it already holds announces nothing") {
        CHECK_FALSE(record->set_controller(wrapper.ptr(), 5));
        NETW_CHECK_EQ(log.count("moved"), 1);
    }
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Entity] R20 an instance's own scene declaration "
    "outranks the one its script made"
) {
    EntityFactories factories;
    Record record;
    Node *owner = memnew(Node);
    const Ref<Script> made
        = ClassDBSingleton::get_singleton()->instantiate("GDScript");
    made->set_source_code(String("extends Node\n"));
    made->reload();
    netw::SceneDecl decl;
    decl.declared = true;
    decl.label = StringName("Arena");
    decl.isolation = 1;
    netw::script::model::declare_scene(made, decl);
    owner->set_script(made);

    CHECK(record->scene_label_of(owner) == StringName("Arena"));
    NETW_CHECK_EQ(record->scene_isolation_of(owner), 1);

    SUBCASE("a write on the instance is what the record answers after") {
        record->set_scene_label("Lobby");
        record->set_scene_isolation(0);
        CHECK(record->scene_label_of(owner) == StringName("Lobby"));
        NETW_CHECK_EQ(record->scene_isolation_of(owner), 0);
    }

    SUBCASE(
        "a build whose owner carries no declaration answers the "
        "unisolated default"
    ) {
        Node *plain_owner = memnew(Node);
        Record plain;
        CHECK(plain->scene_label_of(plain_owner) == StringName());
        NETW_CHECK_EQ(plain->scene_isolation_of(plain_owner), 0);
        memdelete(plain_owner);
    }

    memdelete(owner);
}

#endif

} // namespace TestNetwEntityRecord
