// Laws for NetwEntityRecord, the assembled record plane.
//
// What the assembly is for is that one handle covers identity, control and
// stage at once. So the laws are about the record being ONE thing: it is born
// whole, it never shares a part with another record, it lets go of what it
// gave up, and it moves through its life by the one table that says which
// moves exist.

#include "support/netw_test.h"

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "netw/entity_ids.hpp"
#include "netw/entity_options.hpp"
#include "netw/entity_record.hpp"
#include "netw/entity_stage.hpp"
#include <memory>

#include "support/entity_facets.h"
#include "support/netw_call_log.h"

namespace TestNetwEntityRecord {

using namespace godot;
using netw::EntityStage;
using netw::NetwEntityRecord;
using netw_test::CallLog;
using netw_test::EntityFactories;

/* A sink that reads the record AT THE MOMENT it is called.
 *
 * What makes the in-flight options a field rather than an argument is that a
 * listener reads them during the announcement, so a law that reads them
 * afterwards is reading the wrong instant.
 */
class WindowProbe final : public godot::CallableCustom {
    Ref<NetwEntityRecord> record;
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
        const Ref<NetwEntityRecord> &p_record,
        const std::shared_ptr<bool> &p_held
    )
        : record(p_record), held(p_held),
          anchor(netw::gd::instance_id(p_record.ptr())) {
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

// A listener that refuses every control request it is shown, which is how the
// deny latch is observable: the record's answer must follow the refusal rather
// than the order listeners were connected in.
class DenyingSink final : public godot::CallableCustom {
    // A custom callable with no object reads as INVALID, and emit_signal skips
    // every one of those in silence.
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

Ref<NetwEntityRecord> make_record() {
    Ref<NetwEntityRecord> record;
    record.instantiate();
    return record;
}

TEST_CASE(
    "[Networked][Entity][Hosted] R1 a record is born whole and shares no part"
) {
    Ref<NetwEntityRecord> a = make_record();
    Ref<NetwEntityRecord> b = make_record();

    CHECK(netw::entity_ids::minted(a->get_handle()));
    CHECK(netw::entity_ids::minted(b->get_handle()));
    CHECK(a->get_handle() != b->get_handle());
    NETW_CHECK_EQ(netw::entity_ids::holders(a->get_handle()), 1);

    // The part that a shared default would break: two records that answer the
    // same control answer for one entity while claiming to be two.
    CHECK(a->get_control().is_valid());
    CHECK(a->get_control() != b->get_control());
    a->get_control()->set_controller(7);
    NETW_CHECK_EQ(b->get_control()->get_controller(), 0);

    SUBCASE("a fresh record is unbound, unrouted and unnamed") {
        NETW_CHECK_EQ(a->get_stage(), int(EntityStage::UNBOUND));
        NETW_CHECK_EQ(a->get_route(), 0);
        NETW_CHECK_EQ(a->get_peer_id(), 0);
        CHECK(a->get_entity_id() == StringName());
    }

    SUBCASE("the stage moves without disturbing its neighbour") {
        CHECK(a->advance(int(EntityStage::ARMED)));
        NETW_CHECK_EQ(b->get_stage(), int(EntityStage::UNBOUND));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R2 a record's handle lives exactly as long as "
    "the record"
) {
    const int before = netw::entity_ids::outstanding();
    RID handle;
    {
        Ref<NetwEntityRecord> record = make_record();
        handle = record->get_handle();
        NETW_CHECK_EQ(netw::entity_ids::outstanding(), before + 1);
        CHECK(netw::entity_ids::minted(handle));
    }
    CHECK_FALSE(netw::entity_ids::minted(handle));
    NETW_CHECK_EQ(netw::entity_ids::outstanding(), before);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R3 adopting a handle lets go of the one it "
    "replaces"
) {
    const int before = netw::entity_ids::outstanding();
    Ref<NetwEntityRecord> standing = make_record();
    Ref<NetwEntityRecord> arriving = make_record();
    const RID birth = arriving->get_handle();
    const RID held = standing->get_handle();

    CHECK(arriving->adopt_handle(held));

    CHECK(arriving->get_handle() == held);
    CHECK_FALSE(netw::entity_ids::minted(birth));
    NETW_CHECK_EQ(netw::entity_ids::holders(held), 2);
    NETW_CHECK_EQ(netw::entity_ids::outstanding(), before + 1);

    SUBCASE("adopting what it already holds costs nothing") {
        CHECK(arriving->adopt_handle(held));
        NETW_CHECK_EQ(netw::entity_ids::holders(held), 2);
    }

    SUBCASE("a handle the mint never issued is refused, and nothing is lost") {
        CHECK_FALSE(arriving->adopt_handle(RID()));
        CHECK(arriving->get_handle() == held);
        NETW_CHECK_EQ(netw::entity_ids::holders(held), 2);
    }

    SUBCASE("the standing record letting go leaves the adopter holding") {
        standing.unref();
        CHECK(netw::entity_ids::minted(held));
        NETW_CHECK_EQ(netw::entity_ids::holders(held), 1);
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R4 a record moves by the stage table and a "
    "refused move changes nothing"
) {
    Ref<NetwEntityRecord> record = make_record();

    CHECK(record->advance(int(EntityStage::ARMED)));
    CHECK(record->advance(int(EntityStage::LIVE)));
    NETW_CHECK_EQ(record->get_stage(), int(EntityStage::LIVE));

    SUBCASE("a stage is never re-entered") {
        CHECK_FALSE(record->advance(int(EntityStage::LIVE)));
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::LIVE));
    }

    SUBCASE("a backward move is refused and costs the record nothing") {
        CHECK_FALSE(record->advance(int(EntityStage::ARMED)));
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::LIVE));
    }

    SUBCASE("the terminal stage is terminal") {
        CHECK(record->advance(int(EntityStage::DESPAWNING)));
        CHECK(record->advance(int(EntityStage::FREED)));
        CHECK_FALSE(record->advance(int(EntityStage::LIVE)));
        CHECK_FALSE(record->advance(int(EntityStage::ARMED)));
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::FREED));
    }

    SUBCASE("the record has no second opinion about the table") {
        for (int from = 0; from <= int(EntityStage::FREED); ++from) {
            for (int to = 0; to <= int(EntityStage::FREED); ++to) {
                Ref<NetwEntityRecord> probe = make_record();
                // Only edges the table admits out of UNBOUND are reachable to
                // set up with, so the walk starts where it can.
                if (!netw::NetwEntityStage::edge_is_legal(
                        int(EntityStage::UNBOUND),
                        from
                    )
                    && from != int(EntityStage::UNBOUND)) {
                    continue;
                }
                if (from != int(EntityStage::UNBOUND)) {
                    probe->advance(from);
                }
                NETW_CHECK_EQ(
                    probe->advance(to),
                    netw::NetwEntityStage::edge_is_legal(from, to)
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
    Ref<NetwEntityRecord> record = make_record();
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

    Ref<NetwEntityRecord> other = make_record();
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
    Ref<NetwEntityRecord> record = make_record();
    Ref<RefCounted> wrapper = make_wrapper();

    CHECK_FALSE(
        NetwEntityRecord::has_part_factory(NetwEntityRecord::PART_INTEREST)
    );
    CHECK(
        record->part(NetwEntityRecord::PART_INTEREST, wrapper.ptr()).is_null()
    );

    CHECK(record->part(NetwEntityRecord::PART_MAX, wrapper.ptr()).is_null());
    CHECK(record->part(-1, wrapper.ptr()).is_null());
    CHECK_FALSE(NetwEntityRecord::has_part_factory(-1));

    CallLog log;
    NetwEntityRecord::set_part_factory(
        NetwEntityRecord::PART_INTEREST,
        log.minting("interest")
    );
    CHECK(
        record->part(NetwEntityRecord::PART_INTEREST, wrapper.ptr()).is_valid()
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
        Ref<NetwEntityRecord> record = make_record();
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
    Ref<NetwEntityRecord> record = make_record();
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
    Ref<NetwEntityRecord> record = make_record();
    Node *owner = memnew(Node);

    CHECK(record->mark_template(owner));
    NETW_CHECK_EQ(record->get_stage(), int(EntityStage::TEMPLATE));

    // Idempotent, because every peer marks its own copy of the same editor
    // scene and a second mark is a duplicate rather than a bug.
    CHECK(record->mark_template(owner));
    NETW_CHECK_EQ(record->get_stage(), int(EntityStage::TEMPLATE));

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
        Ref<NetwEntityRecord> record = make_record();
        Node *owner = memnew(Node);
        record->set_entity_id("crate");
        CHECK(record->classify_activation(owner));
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::UNBOUND));
        memdelete(owner);
    }

    SUBCASE("an editor-placed factory declares itself a template") {
        Ref<NetwEntityRecord> record = make_record();
        Node *owner = memnew(Node);
        scene->add_child(owner);
        owner->set_owner(scene);
        CHECK(NetwEntityRecord::declares_template(owner));
        CHECK_FALSE(record->classify_activation(owner));
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::TEMPLATE));
    }

    SUBCASE("a scene assembled without ownership declares it by mark") {
        Ref<NetwEntityRecord> record = make_record();
        Node *owner = memnew(Node);
        owner->set_meta(NetwEntityRecord::template_meta(), true);
        CHECK(NetwEntityRecord::declares_template(owner));
        CHECK_FALSE(record->classify_activation(owner));
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::TEMPLATE));
        memdelete(owner);
    }

    SUBCASE("a bare programmatic node stays inert rather than becoming one") {
        Ref<NetwEntityRecord> record = make_record();
        Node *owner = memnew(Node);
        CHECK_FALSE(NetwEntityRecord::declares_template(owner));
        CHECK_FALSE(record->classify_activation(owner));
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::UNBOUND));
        memdelete(owner);
    }

    SUBCASE("a record past UNBOUND is already classified") {
        Ref<NetwEntityRecord> record = make_record();
        Node *owner = memnew(Node);
        REQUIRE(record->advance(int(EntityStage::ARMED)));
        CHECK(record->classify_activation(owner));
        memdelete(owner);
    }

    memdelete(scene);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R11 a teardown window opens once, and the "
    "options are in flight only inside it"
) {
    Ref<NetwEntityRecord> record = make_record();
    Ref<RefCounted> wrapper = make_wrapper();
    netw::gd::add_signal(wrapper.ptr(), "despawning", 1);
    auto seen = std::make_shared<bool>(false);
    wrapper->connect(
        "despawning",
        Callable(memnew(WindowProbe(record, seen)))
    );
    REQUIRE(record->advance(int(EntityStage::ARMED)));
    Ref<netw::NetwDespawnOpts> opts;
    opts.instantiate();
    opts->set_reason("killed");

    CHECK(record->begin_despawn(wrapper.ptr(), nullptr, opts));

    NETW_CHECK_EQ(record->get_stage(), int(EntityStage::DESPAWNING));
    // A listener reads the mode during the announcement, which is the whole
    // reason it is a field and not an argument.
    CHECK(*seen);
    CHECK(record->get_active_despawn_opts().is_null());

    SUBCASE("a second teardown is refused rather than announced twice") {
        *seen = false;
        CHECK_FALSE(record->begin_despawn(wrapper.ptr(), nullptr, opts));
        CHECK_FALSE(*seen);
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::DESPAWNING));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R12 a record that cannot tear down changes "
    "nothing"
) {
    Ref<NetwEntityRecord> record = make_record();
    Ref<RefCounted> wrapper = make_wrapper();
    REQUIRE(record->mark_template(nullptr));

    // A template is terminal: it never spawned, so it has nothing to tear
    // down and the window would be an illegal edge rather than a no-op.
    Ref<netw::NetwDespawnOpts> none;
    CHECK_FALSE(record->begin_despawn(wrapper.ptr(), nullptr, none));
    NETW_CHECK_EQ(record->get_stage(), int(EntityStage::TEMPLATE));
    CHECK(record->get_active_despawn_opts().is_null());

    SUBCASE("an unbound record with no options tears down by default") {
        Ref<NetwEntityRecord> fresh = make_record();
        CHECK(fresh->begin_despawn(wrapper.ptr(), nullptr, none));
        NETW_CHECK_EQ(fresh->get_stage(), int(EntityStage::DESPAWNING));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R13 authority follows the controller, and the "
    "server is the one peer the two sides spell differently"
) {
    Ref<NetwEntityRecord> record = make_record();
    Ref<RefCounted> wrapper = make_wrapper();
    netw::gd::add_signal(wrapper.ptr(), "control_changed", 2);
    netw_test::CallLog log;
    wrapper->connect("control_changed", log.callable("moved"));
    Node *owner = memnew(Node);

    record->get_control()->set_controller(7);
    record->apply_control(wrapper.ptr(), owner, true);

    NETW_CHECK_EQ(owner->get_multiplayer_authority(), 7);
    NETW_CHECK_EQ(log.count("moved"), 1);
    // The record calls the server 0 and the engine calls it 1, so a node that
    // has never moved reads as "was 0" rather than "was 1".
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
    // The one row that must be false is the one that trips an engine
    // assertion: in the tree, not ready yet, and not the authoring peer.
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
    Ref<NetwEntityRecord> record = make_record();
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
    Ref<NetwEntityRecord> record = make_record();
    Ref<RefCounted> wrapper = make_wrapper();
    netw::gd::add_signal(wrapper.ptr(), "despawned");
    netw_test::CallLog log;
    wrapper->connect("despawned", log.callable("gone"));
    REQUIRE(record->advance(int(EntityStage::ARMED)));
    REQUIRE(record->advance(int(EntityStage::LIVE)));

    // A reparent leaves the tree and re-enters with the record still live, so
    // a live owner leaving is never assumed to be a teardown.
    CHECK_FALSE(record->finish_teardown(wrapper.ptr()));
    NETW_CHECK_EQ(record->get_stage(), int(EntityStage::LIVE));
    NETW_CHECK_EQ(log.count("gone"), 0);

    SUBCASE("a despawning record ends and says so once") {
        REQUIRE(record->advance(int(EntityStage::DESPAWNING)));
        CHECK(record->finish_teardown(wrapper.ptr()));
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::FREED));
        NETW_CHECK_EQ(log.count("gone"), 1);

        CHECK_FALSE(record->finish_teardown(wrapper.ptr()));
        NETW_CHECK_EQ(log.count("gone"), 1);
    }

    SUBCASE("a lingering record ends the same way") {
        REQUIRE(record->advance(int(EntityStage::DESPAWNING)));
        REQUIRE(record->advance(int(EntityStage::LINGERING)));
        CHECK(record->finish_teardown(wrapper.ptr()));
        NETW_CHECK_EQ(record->get_stage(), int(EntityStage::FREED));
        NETW_CHECK_EQ(log.count("gone"), 1);
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] R17 a control request is arbitrated once, and "
    "the strictest listener wins"
) {
    Ref<NetwEntityRecord> record = make_record();
    Ref<RefCounted> wrapper = make_wrapper();
    netw::gd::add_signal(wrapper.ptr(), "control_requested", 2);
    netw_test::CallLog log;
    wrapper->connect("control_requested", log.callable("asked"));

    // A fixed entity refuses without ever asking, so gameplay code is not
    // consulted about a transfer that could not happen.
    NETW_CHECK_EQ(record->admit_control_request(wrapper.ptr(), 9), 0);
    NETW_CHECK_EQ(log.count("asked"), 0);

    SUBCASE("a requestable entity asks, and grants when nobody refuses") {
        record->get_control()->set_transfer(
            int(netw::Transfer::REQUESTABLE)
        );
        NETW_CHECK_EQ(record->admit_control_request(wrapper.ptr(), 9), 9);
        NETW_CHECK_EQ(log.count("asked"), 1);
        REQUIRE(log.args("asked").size() == 2);
        NETW_CHECK_EQ(int(log.args("asked")[0]), 9);
    }

    SUBCASE("a listener that denies stops the grant") {
        record->get_control()->set_transfer(
            int(netw::Transfer::REQUESTABLE)
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
    Ref<NetwEntityRecord> record = make_record();
    Node *owner = memnew(Node);
    owner->set_name("valeria|7");

    record->hydrate_identity(owner);

    CHECK(record->get_entity_id() == StringName("valeria"));
    NETW_CHECK_EQ(record->get_peer_id(), 7);

    SUBCASE("a caller that already bound the identity keeps it") {
        Ref<NetwEntityRecord> bound = make_record();
        bound->set_entity_id("crate");
        bound->set_peer_id(3);
        bound->hydrate_identity(owner);
        CHECK(bound->get_entity_id() == StringName("crate"));
        NETW_CHECK_EQ(bound->get_peer_id(), 3);
    }

    SUBCASE("a name that spells no identity fills nothing") {
        Ref<NetwEntityRecord> plain = make_record();
        owner->set_name("JustANode");
        plain->hydrate_identity(owner);
        // The codec answers nothing for a name carrying no separator, so a
        // node the pipeline never named keeps an unbound record rather than
        // taking its own node name for an entity id.
        CHECK(plain->get_entity_id() == StringName());
        NETW_CHECK_EQ(plain->get_peer_id(), 0);
    }

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Entity][Hosted] R19 the controller is written once and the "
    "move is announced once"
) {
    Ref<NetwEntityRecord> record = make_record();
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

TEST_CASE(
    "[Networked][Entity][Hosted] R20 an instance's own scene declaration "
    "outranks the one its script made"
) {
    EntityFactories factories;
    Ref<NetwEntityRecord> record = make_record();
    Node *owner = memnew(Node);
    netw_test::CallLog log;
    Dictionary declared;
    declared["label"] = StringName("Arena");
    declared["isolation"] = 1;
    NetwEntityRecord::set_scene_declaration_reader(
        log.answering("declared", declared)
    );

    CHECK(record->scene_label_of(owner) == StringName("Arena"));
    NETW_CHECK_EQ(record->scene_isolation_of(owner), 1);

    SUBCASE("a write on the instance is what the record answers after") {
        record->set_scene_label("Lobby");
        record->set_scene_isolation(0);
        CHECK(record->scene_label_of(owner) == StringName("Lobby"));
        NETW_CHECK_EQ(record->scene_isolation_of(owner), 0);
    }

    SUBCASE("a build that declares nothing answers the unisolated default") {
        NetwEntityRecord::set_scene_declaration_reader(Callable());
        Ref<NetwEntityRecord> plain = make_record();
        CHECK(plain->scene_label_of(owner) == StringName());
        NETW_CHECK_EQ(plain->scene_isolation_of(owner), 0);
    }

    memdelete(owner);
}

} // namespace TestNetwEntityRecord
