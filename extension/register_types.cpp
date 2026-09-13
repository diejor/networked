#include "register_types.h"

#include "godot/class_db.hpp"
#include "godot/extension.hpp"

#include "netw/api/ring_buffer.hpp"

#include "netw/api/action.hpp"
#include "netw/api/auth_flow.hpp"
#include "netw/api/auth_protocol.hpp"
#include "netw/api/auth_result.hpp"
#include "netw/api/bit_stream.hpp"
#include "netw/api/channel.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/context.hpp"
#include "netw/api/database.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/debug_join_config.hpp"
#include "netw/api/default_join.hpp"
#include "netw/api/despawn_config.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/file_system_database.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/interest_handle.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/join_config.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/api/nodes/multiplayer_tree.hpp"
#include "netw/api/nodes/service.hpp"
#include "netw/api/nodes/view/host_scene_view.hpp"
#include "netw/api/nodes/view/participant_view.hpp"
#include "netw/api/nodes/view/participant_viewport.hpp"
#include "netw/api/nodes/view/participant_window.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_field_recovery.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/predict_journal_snapshot.hpp"
#include "netw/api/predict_slot_engine.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/probe_result.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/record.hpp"
#include "netw/api/record_table.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/resolved_join.hpp"
#include "netw/api/scene_config.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/server_info.hpp"
#include "netw/api/session_config.hpp"
#include "netw/api/spawn_slot.hpp"
#include "netw/api/sync_compat.hpp"
#include "netw/api/sync_model.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/api/transaction.hpp"
#include "netw/api/warm_policy.hpp"
#include "netw/call_park.hpp"
#include "netw/carrier_buffers.hpp"
#include "netw/carrier_frame.hpp"
#include "netw/channel_book.hpp"
#include "netw/clock_engine.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/history.hpp"
#include "netw/display/playhead.hpp"
#include "netw/display/port.hpp"
#include "netw/display/role_facts.hpp"
#include "netw/display/runtime.hpp"
#include "netw/display/timing.hpp"
#include "netw/display/tracks.hpp"
#include "netw/effect_ledger.hpp"
#include "netw/entity/control.hpp"
#include "netw/entity/ids.hpp"
#include "netw/handle_ledger.hpp"
#include "netw/interest/decl.hpp"
#include "netw/interest/engine.hpp"
#include "netw/interest/leave.hpp"
#include "netw/interest/perception.hpp"
#include "netw/interest/relay.hpp"
#include "netw/join_roster.hpp"
#include "netw/log.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/frame_records.hpp"
#include "netw/predict/relay_book.hpp"
#include "netw/prediction_core.hpp"
#include "netw/replication_send.hpp"
#include "netw/scene_core.hpp"
#include "netw/script/model.hpp"
#include "netw/session_core.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/planner.hpp"
#include "netw/spawn/record.hpp"
#include "netw/sync_progress.hpp"

#include "netw/lagcomp_core.hpp"
#include "netw/profile.hpp"

#include "netw/api/clock_handle.hpp"
#include "netw/api/connect_handle.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_transport.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/ring_buffer.hpp"
#include "netw/api/session_handle.hpp"
#include "netw/api/tests.hpp"
#include "netw/api/timeline.hpp"
#include "netw/api/webrtc_signaler.hpp"
#include "netw/connect/transports.hpp"
#include "netw/connect/webrtc_link.hpp"
#include "netw/table/core.hpp"
#include "netw/txn_book.hpp"

#if defined(NETW_TESTS) && defined(NETW_GDEXTENSION)
#include "tests/support/carrier.h"
#include "tests/support/spawn_probe.h"
#endif

#if defined(NETW_TESTS)
#include "tests/support/auth_stand.h"
#include "tests/support/persistence_stand.h"
#include "tests/support/published_classes.h"
#include "tests/support/stepper_recorder.h"
#endif

#if defined(NETW_TESTS) && defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/class_db_singleton.hpp>
#endif

using namespace godot;

void initialize_networked_module(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    netw::profile::startup();
    netw::profile::configure_plots();
    netw::log::configure();
#if defined(NETW_TESTS) && defined(NETW_GDEXTENSION)
    const PackedStringArray before_registration
        = ClassDBSingleton::get_singleton()->get_class_list();
#endif
    GDREGISTER_CLASS(netw::NetwRingBuffer);
    GDREGISTER_CLASS(netw::NetwTimeline);
    GDREGISTER_CLASS(netw::NetwBitStream);
    GDREGISTER_CLASS(netw::Netw);
    GDREGISTER_CLASS(netw::NetwInterpolate);
    GDREGISTER_CLASS(netw::NetwEntity);
    GDREGISTER_CLASS(netw::NetwInterestLayer);
    GDREGISTER_CLASS(netw::NetwPromise);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwAuthProtocol);
    GDREGISTER_CLASS(netw::ResolvedJoin);
    GDREGISTER_CLASS(netw::NetwDefaultJoin);
    GDREGISTER_CLASS(netw::NetwAction);
    GDREGISTER_CLASS(netw::NetwActionContext);
    GDREGISTER_CLASS(netw::NetwParticipant);
    GDREGISTER_CLASS(netw::NetwPhysicsStepper);
    GDREGISTER_CLASS(netw::AuthResult);
    GDREGISTER_CLASS(netw::NetwAuthFlow);
    GDREGISTER_CLASS(netw::NetwServerInfo);
    GDREGISTER_CLASS(netw::NetwConnectHandle);
    GDREGISTER_CLASS(netw::NetwSessionHandle);
    GDREGISTER_CLASS(netw::NetwClockHandle);
    GDREGISTER_CLASS(netw::NetwTransport);
    GDREGISTER_CLASS(netw::NetwWebRTCSignaler);
    GDREGISTER_CLASS(netw::NetwLinkConditions);
    GDREGISTER_CLASS(netw::NetwProbeResult);
    GDREGISTER_CLASS(netw::NetwIdentity);
    GDREGISTER_CLASS(netw::DebugJoinConfig);
    GDREGISTER_CLASS(netw::NetwDisplayHandle);
    GDREGISTER_CLASS(netw::NetwInterestHandle);
    GDREGISTER_CLASS(netw::NetwSceneConfig);
    GDREGISTER_CLASS(netw::NetwSceneHandle);
    GDREGISTER_CLASS(netw::SpawnSlot);
    GDREGISTER_CLASS(netw::NetwPropertySetColumn);
    GDREGISTER_CLASS(netw::NetwPropertySet);
    GDREGISTER_CLASS(netw::NetwPropertySetBinding);
    GDREGISTER_CLASS(netw::NetwChannel);
    GDREGISTER_CLASS(netw::NetwPersistenceEngine);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwQuantize);
    GDREGISTER_CLASS(netw::NetwQuantizeScalar);
    GDREGISTER_CLASS(netw::NetwQuantizeAngle);
    GDREGISTER_CLASS(netw::NetwQuantizeQuaternion);
    GDREGISTER_CLASS(netw::NetwQuantizeTransform2D);
    GDREGISTER_CLASS(netw::NetwQuantizeTransform3D);
    GDREGISTER_CLASS(netw::NetwPredictFold);
    GDREGISTER_CLASS(netw::NetwPredictJudgement);
    GDREGISTER_CLASS(netw::NetwPredictRecovery);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwPredict);
    GDREGISTER_CLASS(netw::NetwPredictIsland);
    GDREGISTER_CLASS(netw::NetwPredictFieldRecovery);
    GDREGISTER_CLASS(netw::NetwPredictionHandle);
    GDREGISTER_CLASS(netw::NetwPredictCarryContext);
    GDREGISTER_CLASS(netw::NetwPredictStats);
    GDREGISTER_CLASS(netw::NetwPredictJournal);
    GDREGISTER_ABSTRACT_CLASS(netw::Serde);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwRecord);
    GDREGISTER_CLASS(netw::DictionaryRecord);
    GDREGISTER_CLASS(netw::WarmRequest);
    GDREGISTER_CLASS(netw::WarmPolicy);
    GDREGISTER_CLASS(netw::NetwDatabaseBackend);
    GDREGISTER_CLASS(netw::FileSystemDatabase);
    GDREGISTER_CLASS(netw::NetwTransaction);
    GDREGISTER_CLASS(netw::NetwRecordTable);
    GDREGISTER_CLASS(netw::NetwDatabase);
    GDREGISTER_CLASS(netw::NetwMultiplayer);
    GDREGISTER_CLASS(netw::NetwService);
    GDREGISTER_CLASS(netw::MultiplayerTree);
    GDREGISTER_CLASS(netw::LobbyDirectory);
    GDREGISTER_CLASS(netw::ParticipantView);
    GDREGISTER_CLASS(netw::HostSceneView);
    GDREGISTER_CLASS(netw::ParticipantWindow);
    GDREGISTER_CLASS(netw::ParticipantViewport);
    GDREGISTER_CLASS(netw::NetwGroupPromise);
    GDREGISTER_CLASS(netw::NetwDespawnOpts);
    GDREGISTER_CLASS(netw::NetwDespawnConfig);
    GDREGISTER_CLASS(netw::NetwPersistenceConfig);
    GDREGISTER_CLASS(netw::NetwJoinConfig);
    GDREGISTER_CLASS(netw::NetwMemberConfig);
    GDREGISTER_CLASS(netw::NetwPropertyConfig);
    GDREGISTER_CLASS(netw::NetwClockConfig);
    GDREGISTER_CLASS(netw::NetwLagCompensationConfig);
    GDREGISTER_CLASS(netw::NetwSessionConfig);
    GDREGISTER_CLASS(netw::NetwSchemaColumn);
    GDREGISTER_CLASS(netw::NetwSchema);
    GDREGISTER_CLASS(netw::NetwReparentOpts);
    GDREGISTER_CLASS(netw::NetwControlRequest);

    GDREGISTER_CLASS(netw::LocalLinkConditions);
    GDREGISTER_CLASS(netw::LocalMultiplayerPeer);
    GDREGISTER_CLASS(netw::LocalLoopbackSession);
    GDREGISTER_INTERNAL_CLASS(netw::connect::WebRTCLink);
#if defined(NETW_TESTS) && defined(NETW_GDEXTENSION)
    {
        HashSet<String> standing;
        for (int at = 0; at < before_registration.size(); ++at) {
            standing.insert(before_registration[at]);
        }
        const PackedStringArray after
            = ClassDBSingleton::get_singleton()->get_class_list();
        PackedStringArray &published = netw_test::published_classes();
        published.clear();
        for (int at = 0; at < after.size(); ++at) {
            if (!standing.has(after[at])) {
                published.push_back(after[at]);
            }
        }
    }
#endif

#if defined(NETW_TESTS)
#if defined(NETW_GDEXTENSION)
    GDREGISTER_CLASS(netw_test::Carrier);
    GDREGISTER_CLASS(netw_test::SpawnIdentityProbe);
#endif
    GDREGISTER_CLASS(netw_test::NetwTestPersistenceEngine);
    GDREGISTER_CLASS(netw_test::NetwTestAuthFlow);
    GDREGISTER_CLASS(netw_test::RecordingStepper);
    GDREGISTER_CLASS(netw::NetwNativeTests);
#endif
    netw::NetwEntityRecord::set_part_factory(
        netw::NetwEntityRecord::PART_DISPLAY,
        callable_mp_static(&netw::build_display_handle)
    );
    netw::NetwEntityRecord::set_part_factory(
        netw::NetwEntityRecord::PART_INTEREST,
        callable_mp_static(&netw::build_interest_handle)
    );
    netw::NetwEntityRecord::set_part_factory(
        netw::NetwEntityRecord::PART_PREDICTION,
        callable_mp_static(&netw::build_prediction_handle)
    );
    netw::NetwEntityRecord::set_part_factory(
        netw::NetwEntityRecord::PART_SCENE,
        callable_mp_static(&netw::build_scene_handle)
    );

    netw::NetwMultiplayer::install_default_interface();
    netw::connect::install_native_transports();
}

void uninitialize_networked_module(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    netw::NetwMultiplayer::restore_default_interface();
    netw::LocalLoopbackSession::set_shared_session(nullptr);
    netw::NetwEntityRecord::clear_part_factories();
    netw::NetwMultiplayer::clear_wrapper_factory();
    netw::connect::TransportBook::shared().clear();
    netw::entity::shutdown();
    netw::profile::shutdown();
}

#if defined(NETW_GDEXTENSION)

extern "C" {

GDExtensionBool GDE_EXPORT networked_library_init(
    GDExtensionInterfaceGetProcAddress get_proc_address,
    GDExtensionClassLibraryPtr library,
    GDExtensionInitialization *initialization
) {
    GDExtensionBinding::InitObject init(
        get_proc_address,
        library,
        initialization
    );
    init.register_initializer(initialize_networked_module);
    init.register_terminator(uninitialize_networked_module);
    init.set_minimum_library_initialization_level(
        MODULE_INITIALIZATION_LEVEL_SCENE
    );
    return init.init();
}
}

#endif
