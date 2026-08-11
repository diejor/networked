#include "register_types.h"

#include "godot/class_db.hpp"
#include "godot/extension.hpp"

#include "netw/bit_buffer.hpp"
#include "netw/clock_core.hpp"
#include "netw/codec.hpp"
#include "netw/database_backend.hpp"
#include "netw/display_history.hpp"
#include "netw/effect_ledger.hpp"
#include "netw/handle_ledger.hpp"
#include "netw/interest_engine.hpp"
#include "netw/interpolate.hpp"
#include "netw/liveness_core.hpp"
#include "netw/log.hpp"
#include "netw/netw_multiplayer.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/frame_records.hpp"
#include "netw/predict/relay_book.hpp"
#include "netw/prediction_core.hpp"
#include "netw/rate_window.hpp"
#include "netw/scene_core.hpp"
#include "netw/session_core.hpp"

#include "netw/lagcomp_core.hpp"
#include "netw/profile.hpp"

#include "netw/project.hpp"
#include "netw/quantize.hpp"
#include "netw/ring_buffer.hpp"
#include "netw/timeline.hpp"
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
    GDREGISTER_ABSTRACT_CLASS(netw::NetwProject);
    GDREGISTER_CLASS(netw::NetwLivenessCore);
    GDREGISTER_CLASS(netw::NetwRateWindow);
    GDREGISTER_CLASS(netw::NetwInterestBitSet);
    GDREGISTER_CLASS(netw::NetwInterestStats);
    GDREGISTER_CLASS(netw::NetwInterestDelta);
    GDREGISTER_CLASS(netw::NetwInterestEngine);
    GDREGISTER_CLASS(netw::NetwSceneCore);
    GDREGISTER_CLASS(netw::NetwSessionCore);
    GDREGISTER_ABSTRACT_CLASS(netw::NetwQuantize);
    GDREGISTER_CLASS(netw::NetwQuantizeBits);
    GDREGISTER_CLASS(netw::NetwQuantizeFixed);
    GDREGISTER_CLASS(netw::NetwQuantizeAngle);
    GDREGISTER_CLASS(netw::NetwQuantizeQuaternion);
    GDREGISTER_CLASS(netw::NetwQuantizeTransform2D);
    GDREGISTER_CLASS(netw::NetwQuantizeTransform3D);
    GDREGISTER_CLASS(netw::NetwPredictionCore);
    GDREGISTER_CLASS(netw::NetwPredictDeclaration);
    GDREGISTER_CLASS(netw::NetwPredictConsumePlan);
    GDREGISTER_CLASS(netw::NetwPredictDrive);
    GDREGISTER_CLASS(netw::NetwPredictEvidence);
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
    GDREGISTER_CLASS(netw::NetwLagCompCore);
    GDREGISTER_CLASS(netw::NetwDatabaseBackend);
    GDREGISTER_CLASS(netw::NetwDatabaseBackendDict);
    GDREGISTER_CLASS(netw::NetwMultiplayerCore);

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
