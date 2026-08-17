#include "register_types.h"

#include "godot/class_db.hpp"
#include "godot/extension.hpp"

#include "netw/bit_buffer.hpp"
#include "netw/carrier_buffers.hpp"
#include "netw/call_park.hpp"
#include "netw/channel_book.hpp"
#include "netw/carrier_frame.hpp"
#include "netw/clock_core.hpp"
#include "netw/codec.hpp"
#include "netw/database_backend.hpp"
#include "netw/display_book.hpp"
#include "netw/display_channel.hpp"
#include "netw/display_decl.hpp"
#include "netw/display_playhead.hpp"
#include "netw/display_runtime.hpp"
#include "netw/display_port.hpp"
#include "netw/display_tracks.hpp"
#include "netw/display_history.hpp"
#include "netw/display_offset.hpp"
#include "netw/effect_ledger.hpp"
#include "netw/entity_identity.hpp"
#include "netw/entity_control.hpp"
#include "netw/entity_ids.hpp"
#include "netw/entity_ids_binding.hpp"
#include "netw/entity.hpp"
#include "netw/entity_record.hpp"
#include "netw/entity_options.hpp"
#include "netw/entity_stage.hpp"
#include "netw/event_plane.hpp"
#include "netw/handle_ledger.hpp"
#include "netw/interest_decl.hpp"
#include "netw/interest_relay.hpp"
#include "netw/interest_engine.hpp"
#include "netw/interest_leave.hpp"
#include "netw/interest_perception.hpp"
#include "netw/interpolate.hpp"
#include "netw/liveness_core.hpp"
#include "netw/log.hpp"
#include "netw/netw_multiplayer.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/frame_records.hpp"
#include "netw/predict/relay_book.hpp"
#include "netw/prediction_core.hpp"
#include "netw/rate_window.hpp"
#include "netw/promise.hpp"
#include "netw/join_payload.hpp"
#include "netw/join_roster.hpp"
#include "netw/netw_identity.hpp"
#include "netw/resolved_join.hpp"
#include "netw/group_promise.hpp"
#include "netw/scene_core.hpp"
#include "netw/service_install_book.hpp"
#include "netw/replication_send.hpp"
#include "netw/comp_table.hpp"
#include "netw/spawn_book.hpp"
#include "netw/spawn_planner.hpp"
#include "netw/spawn_park.hpp"
#include "netw/spawn_record.hpp"
#include "netw/spawner_roster.hpp"
#include "netw/persistence_book.hpp"
#include "netw/snapshot_book.hpp"
#include "netw/node_ref.hpp"
#include "netw/staged_writes.hpp"
#include "netw/sync_kernel.hpp"
#include "netw/synchronizers.hpp"
#include "netw/sync_model.hpp"
#include "netw/sync_progress.hpp"
#include "netw/watch_book_binding.hpp"
#include "netw/session_core.hpp"

#include "netw/lagcomp_core.hpp"
#include "netw/profile.hpp"

#include "netw/project.hpp"
#include "netw/pump_stats.hpp"
#include "netw/quantize.hpp"
#include "netw/ring_buffer.hpp"
#include "netw/timeline.hpp"
#include "netw/txn_book.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/table/table_core.hpp"
#include "netw/tests.hpp"
#include "netw/transport/loopback.hpp"

#if defined(NETW_TESTS) && defined(NETW_GDEXTENSION)
#include "tests/support/carrier.h"
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
    GDREGISTER_CLASS(netw::NetwClockCore);
    GDREGISTER_CLASS(netw::NetwInterpolate);
    GDREGISTER_CLASS(netw::NetwDisplayHistory);
    GDREGISTER_CLASS(netw::NetwDisplayDecl);
    GDREGISTER_CLASS(netw::NetwDisplayRoleFacts);
    GDREGISTER_CLASS(netw::NetwDisplayPlayhead);
    GDREGISTER_CLASS(netw::NetwDisplayTracks);
    GDREGISTER_CLASS(netw::NetwDisplayBook);
    GDREGISTER_CLASS(netw::NetwDisplayChannel);
    GDREGISTER_CLASS(netw::NetwDisplayOffset);
    GDREGISTER_CLASS(netw::NetwDisplayPort);
    GDREGISTER_CLASS(netw::NetwDisplayRuntime);
    GDREGISTER_CLASS(netw::NetwPumpStats);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwProject);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwEntityIds);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwSynchronizers);
    GDREGISTER_CLASS(netw::NetwEntityRecord);
    GDREGISTER_CLASS(netw::NetwEntity);
    GDREGISTER_CLASS(netw::NetwLivenessCore);
    GDREGISTER_CLASS(netw::NetwRateWindow);
    GDREGISTER_CLASS(netw::NetwInterestBitSet);
    GDREGISTER_CLASS(netw::NetwInterestStats);
    GDREGISTER_CLASS(netw::NetwInterestDelta);
    GDREGISTER_CLASS(netw::NetwInterestEngine);
    GDREGISTER_CLASS(netw::NetwInterestDecl);
    GDREGISTER_CLASS(netw::NetwInterestAwareness);
    GDREGISTER_CLASS(netw::NetwInterestRelay);
    GDREGISTER_CLASS(netw::NetwInterestLeave);
    GDREGISTER_CLASS(netw::NetwInterestPerception);
    GDREGISTER_CLASS(netw::NetwPromise);
    GDREGISTER_CLASS(netw::ResolvedJoin);
    GDREGISTER_CLASS(netw::NetwJoinRoster);
    GDREGISTER_CLASS(netw::NetwIdentity);
    GDREGISTER_CLASS(netw::JoinPayload);
    GDREGISTER_CLASS(netw::NetwSceneCore);
    GDREGISTER_CLASS(netw::NetwSessionCore);
    GDREGISTER_CLASS(netw::NetwReplicationSend);
    GDREGISTER_CLASS(netw::NetwCompTable);
    GDREGISTER_CLASS(netw::NetwSpawnPlanner);
    GDREGISTER_CLASS(netw::NetwSpawnRecord);
    GDREGISTER_CLASS(netw::NetwSpawnBook);
    GDREGISTER_CLASS(netw::NetwSpawnPark);
    GDREGISTER_CLASS(netw::NetwSpawnerRoster);
    GDREGISTER_CLASS(netw::NetwPersistenceBook);
    GDREGISTER_CLASS(netw::NetwSnapshotBook);
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
    GDREGISTER_CLASS(netw::NetwEffectLedger);
    GDREGISTER_CLASS(netw::NetwEvent);
    GDREGISTER_CLASS(netw::NetwLagCompCore);
    GDREGISTER_CLASS(netw::NetwDatabaseBackend);
    GDREGISTER_CLASS(netw::NetwDatabaseBackendDict);
    GDREGISTER_CLASS(netw::NetwMultiplayerCore);
    GDREGISTER_CLASS(netw::NetwEntityIdentity);
    GDREGISTER_CLASS(netw::NetwCallPark);
    GDREGISTER_CLASS(netw::NetwChannelBook);
    GDREGISTER_CLASS(netw::NetwGroupPromise);
    GDREGISTER_CLASS(netw::NetwServiceInstallBook);
    GDREGISTER_CLASS(netw::NetwCarrierBuffers);
    GDREGISTER_CLASS(netw::NetwCarrierFrame);
    GDREGISTER_CLASS(netw::NetwCarrierDatagram);
    GDREGISTER_CLASS(netw::NetwEntityControl);
    GDREGISTER_CLASS(netw::NetwEntityStage);
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
    GDREGISTER_CLASS(netw::NetwNativeTests);
#endif
}

void uninitialize_networked_module(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    // The process-wide loopback session is a static reference, and a static
    // reference outliving the engine is a crash at exit rather than a leak.
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
