#include "register_types.h"

#include "godot/class_db.hpp"
#include "godot/extension.hpp"

#include "netw/api/ring_buffer.hpp"

#include "netw/api/bit_buffer.hpp"
#include "netw/carrier_buffers.hpp"
#include "netw/call_park.hpp"
#include "netw/channel_book.hpp"
#include "netw/carrier_frame.hpp"
#include "netw/api/clock_handle.hpp"
#include "netw/clock_engine.hpp"
#include "netw/api/codec.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/display_book.hpp"
#include "netw/display_channel.hpp"
#include "netw/api/display_decl.hpp"
#include "netw/display_role_facts.hpp"
#include "netw/display_playhead.hpp"
#include "netw/display_runtime.hpp"
#include "netw/display_port.hpp"
#include "netw/api/display_spec_row.hpp"
#include "netw/display_timing.hpp"
#include "netw/display_tracks.hpp"
#include "netw/display_history.hpp"
#include "netw/effect_ledger.hpp"
#include "netw/entity_control.hpp"
#include "netw/entity_ids.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/handle_ledger.hpp"
#include "netw/interest_decl.hpp"
#include "netw/interest_relay.hpp"
#include "netw/interest_engine.hpp"
#include "netw/interest_leave.hpp"
#include "netw/interest_perception.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/liveness_core.hpp"
#include "netw/log.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/frame_records.hpp"
#include "netw/api/predict_journal_snapshot.hpp"
#include "netw/predict/relay_book.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/prediction_core.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/join_payload.hpp"
#include "netw/join_roster.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/resolved_join.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/scene_mark.hpp"
#include "netw/scene_core.hpp"
#include "netw/service_install_book.hpp"
#include "netw/replication_send.hpp"
#include "netw/api/comp_table.hpp"
#include "netw/api/spawn_book.hpp"
#include "netw/spawn_planner.hpp"
#include "netw/spawn_park.hpp"
#include "netw/api/spawn_record.hpp"
#include "netw/api/spawner_roster.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/node_ref.hpp"
#include "netw/api/staged_writes.hpp"
#include "netw/api/sync_kernel.hpp"
#include "netw/api/synchronizers.hpp"
#include "netw/api/sync_model.hpp"
#include "netw/sync_progress.hpp"
#include "netw/api/watch_book_binding.hpp"
#include "netw/session_core.hpp"

#include "netw/lagcomp_core.hpp"
#include "netw/profile.hpp"

#include "netw/api/project.hpp"
#include "netw/pump_stats.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/ring_buffer.hpp"
#include "netw/api/timeline.hpp"
#include "netw/txn_book.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/table/table_core.hpp"
#include "netw/api/tests.hpp"
#include "netw/api/loopback.hpp"

#if defined(NETW_TESTS) && defined(NETW_GDEXTENSION)
#include "tests/support/carrier.h"
#endif

#if defined(NETW_TESTS)
#include "tests/support/persistence_stand.h"
#endif

using namespace godot;

void initialize_networked_module(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    netw::profile::startup();
    netw::profile::configure_plots();
    netw::log::configure();
    GDREGISTER_CLASS(netw::NetwRingBuffer);
    GDREGISTER_CLASS(netw::NetwTimeline);
    GDREGISTER_CLASS(netw::NetwBitBufferWriter);
    GDREGISTER_CLASS(netw::NetwBitBufferReader);
    GDREGISTER_CLASS(netw::NetwCodec);
    GDREGISTER_CLASS(netw::NetwHandleLedger);
    GDREGISTER_CLASS(netw::NetwClockHandle);
    GDREGISTER_CLASS(netw::NetwInterpolate);
    GDREGISTER_CLASS(netw::NetwDisplayHistory);
    GDREGISTER_CLASS(netw::NetwDisplayDecl);
    GDREGISTER_CLASS(netw::NetwDisplayRoleFacts);
    GDREGISTER_CLASS(netw::NetwDisplayPlayhead);
    GDREGISTER_CLASS(netw::NetwDisplayTracks);
    GDREGISTER_CLASS(netw::NetwDisplayBook);
    GDREGISTER_CLASS(netw::NetwDisplayChannel);
    GDREGISTER_CLASS(netw::NetwDisplayPort);
    GDREGISTER_CLASS(netw::NetwDisplayRuntime);
    GDREGISTER_CLASS(netw::NetwDisplaySpecRow);
    GDREGISTER_CLASS(netw::NetwDisplayTiming);
    GDREGISTER_CLASS(netw::NetwPumpStats);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwProject);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwSynchronizers);
    GDREGISTER_CLASS(netw::NetwEntityRecord);
    GDREGISTER_CLASS(netw::NetwEntity);
    GDREGISTER_CLASS(netw::NetwLivenessCore);
    GDREGISTER_CLASS(netw::NetwInterestLayer);
    GDREGISTER_CLASS(netw::NetwInterestDecl);
    GDREGISTER_CLASS(netw::NetwPromise);
    GDREGISTER_CLASS(netw::ResolvedJoin);
    GDREGISTER_CLASS(netw::NetwParticipant);
    GDREGISTER_CLASS(netw::NetwIdentity);
    GDREGISTER_CLASS(netw::JoinPayload);
    GDREGISTER_CLASS(netw::NetwSceneMark);
    GDREGISTER_INTERNAL_CLASS(netw::NetwSceneCore);
    GDREGISTER_CLASS(netw::NetwReplicationSend);
    GDREGISTER_CLASS(netw::NetwCompTable);
    GDREGISTER_CLASS(netw::NetwSpawnPlanner);
    GDREGISTER_CLASS(netw::NetwSpawnRecord);
    GDREGISTER_CLASS(netw::NetwSpawnBook);
    GDREGISTER_CLASS(netw::NetwSpawnPark);
    GDREGISTER_CLASS(netw::NetwSpawnerRoster);
    GDREGISTER_CLASS(netw::NetwPersistenceEngine);
    GDREGISTER_CLASS(netw::NetwTxnBook);
    GDREGISTER_CLASS(netw::NetwNodeRef);
    GDREGISTER_CLASS(netw::NetwStagedWrites);
    GDREGISTER_CLASS(netw::NetwSyncKernel);
    GDREGISTER_CLASS(netw::NetwSyncSetRow);
    GDREGISTER_CLASS(netw::NetwSyncModel);
    GDREGISTER_CLASS(netw::NetwSyncProgress);
    GDREGISTER_CLASS(netw::NetwWatchBook);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwQuantize);
    GDREGISTER_CLASS(netw::NetwQuantizeBits);
    GDREGISTER_CLASS(netw::NetwQuantizeFixed);
    GDREGISTER_CLASS(netw::NetwQuantizeAngle);
    GDREGISTER_CLASS(netw::NetwQuantizeQuaternion);
    GDREGISTER_CLASS(netw::NetwQuantizeTransform2D);
    GDREGISTER_CLASS(netw::NetwQuantizeTransform3D);
    GDREGISTER_CLASS(netw::NetwPredictionCore);
    GDREGISTER_CLASS(netw::NetwPredictFold);
    GDREGISTER_CLASS(netw::NetwPredictJudgement);
    GDREGISTER_CLASS(netw::NetwPredictRecovery);
    GDREGISTER_CLASS(netw::NetwPredictDeclaration);
    GDREGISTER_CLASS(netw::NetwPredictConsumePlan);
    GDREGISTER_CLASS(netw::NetwPredictDrive);
    GDREGISTER_CLASS(netw::NetwPredictEvidence);
    GDREGISTER_CLASS(netw::NetwPredictReplayEntry);
    GDREGISTER_CLASS(netw::NetwPredictCarryContext);
    GDREGISTER_CLASS(netw::NetwPredictCarryAttempt);
    GDREGISTER_CLASS(netw::NetwPredictJournalRow);
    GDREGISTER_CLASS(netw::NetwPredictRecoveryRequest);
    GDREGISTER_CLASS(netw::NetwPredictWritePlan);
    GDREGISTER_CLASS(netw::NetwPredictEpisodeReport);
    GDREGISTER_CLASS(netw::NetwPredictJointPlan);
    GDREGISTER_CLASS(netw::NetwPredictVerdict);
    GDREGISTER_CLASS(netw::NetwPredictionEngine);
    GDREGISTER_CLASS(netw::NetwPredictCommandFrame);
    GDREGISTER_CLASS(netw::NetwPredictAckFrame);
    GDREGISTER_CLASS(netw::NetwPredictRelayBook);
    GDREGISTER_CLASS(netw::NetwPredictStats);
    GDREGISTER_CLASS(netw::NetwPredictJournal);
    GDREGISTER_INTERNAL_CLASS(netw::NetwEffectLedger);
    GDREGISTER_CLASS(netw::NetwEvent);
    GDREGISTER_CLASS(netw::NetwLagCompCore);
    GDREGISTER_CLASS(netw::NetwDatabaseBackend);
    GDREGISTER_CLASS(netw::NetwMultiplayerCore);
    GDREGISTER_CLASS(netw::NetwCallPark);
    GDREGISTER_CLASS(netw::NetwChannelBook);
    GDREGISTER_CLASS(netw::NetwGroupPromise);
    GDREGISTER_CLASS(netw::NetwServiceInstallBook);
    GDREGISTER_INTERNAL_CLASS(netw::NetwCarrierBuffers);
    GDREGISTER_CLASS(netw::NetwCarrierFrame);
    GDREGISTER_INTERNAL_CLASS(netw::NetwCarrierDatagram);
    GDREGISTER_CLASS(netw::NetwEntityControl);
    GDREGISTER_CLASS(netw::NetwDespawnOpts);
    GDREGISTER_CLASS(netw::NetwReparentOpts);
    GDREGISTER_CLASS(netw::NetwControlRequest);

    GDREGISTER_CLASS(netw::SchemaColumn);
    GDREGISTER_CLASS(netw::SchemaRecord);
    GDREGISTER_CLASS(netw::SchemaCore);
    GDREGISTER_CLASS(netw::TableCore);
    GDREGISTER_CLASS(netw::LocalLinkConditions);
    GDREGISTER_CLASS(netw::LocalMultiplayerPeer);
    GDREGISTER_CLASS(netw::LocalLoopbackSession);
#if defined(NETW_TESTS)
#if defined(NETW_GDEXTENSION)
    GDREGISTER_CLASS(netw_test::Carrier);
#endif
    GDREGISTER_CLASS(netw_test::NetwTestPersistenceEngine);
    GDREGISTER_CLASS(netw_test::NetwTestPersistenceDatabase);
    GDREGISTER_CLASS(netw::NetwNativeTests);
#endif
}

void uninitialize_networked_module(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    netw::LocalLoopbackSession::set_shared_session(nullptr);
    netw::NetwEntityRecord::clear_part_factories();
    netw::NetwMultiplayerCore::clear_wrapper_factory();
    netw::NetwEntity::set_session_lookup(godot::Callable());
    netw::entity_ids::shutdown();
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
