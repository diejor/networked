/* Where a `[Hosted]` case file enters the module tier.
 *
 * A `[Hosted]` case is written once and run in both tiers, and each tier
 * discovers it a different way. The library build globs every `.cpp` under
 * `tests/`, recursively, from `sources.py`, so a case file is a `.cpp` and
 * needs no registration at all. The engine globs only the `.h` files directly
 * under `modules/networked/tests/` and compiles what it finds into its own
 * `test_main.cpp`, so the module build reaches the same case files only
 * through this header.
 *
 * That is why the includes below name `.cpp` files. The extension is the marker
 * for "the library build compiles this on its own", and no file is ever
 * compiled twice: the module build never adds `tests/` to its sources, and the
 * library build never compiles a header the glob does not name.
 *
 * Adding a portable suite is one line here. Leaving the line out is the failure
 * this header exists to make visible, and it is silent — a filter matching no
 * cases still exits zero — so a run is only evidence when paired with
 * `--list-test-cases` and a grep for the family.
 *
 * `tools/check_hosted_registry.py` is what turns that from a convention into a
 * gate, in both directions: a file declaring a `[Hosted]` case that this header
 * does not include, and a file this header includes that declares none. It runs
 * in `static-checks.yml` and proves itself red with `--self-test`.
 */
#pragma once

#include "support/netw_cells.h"
#include "support/netw_reset.h"

#include "bit_buffer_tests.cpp"
#include "call_park_tests.cpp"
#include "channel_book_tests.cpp"
#include "carrier_buffers_tests.cpp"
#include "carrier_frame_tests.cpp"
#include "clock_core_tests.cpp"
#include "codec_tests.cpp"
#include "comp_table_tests.cpp"
#include "declared_world_tests.cpp"
#include "display_book_tests.cpp"
#include "display_channel_tests.cpp"
#include "display_decl_tests.cpp"
#include "display_history_tests.cpp"
#include "display_offset_tests.cpp"
#include "display_playhead_tests.cpp"
#include "display_port_tests.cpp"
#include "display_role_tests.cpp"
#include "display_runtime_tests.cpp"
#include "display_tracks_tests.cpp"
#include "effect_ledger_tests.cpp"
#include "entity_control_tests.cpp"
#include "entity_facade_tests.cpp"
#include "entity_identity_tests.cpp"
#include "entity_stage_tests.cpp"
#include "event_plane_laws.cpp"
#include "event_session_laws.cpp"
#include "entity_options_tests.cpp"
#include "entity_record_tests.cpp"
#include "group_promise_tests.cpp"
#include "handle_ledger_tests.cpp"
#include "interest_decl_tests.cpp"
#include "interest_engine_tests.cpp"
#include "interest_leave_tests.cpp"
#include "interest_perception_tests.cpp"
#include "interest_relay_tests.cpp"
#include "interpolate_tests.cpp"
#include "liveness_core_tests.cpp"
#include "persistence_book_tests.cpp"
#include "synchronizers_tests.cpp"
#include "loopback_transport_tests.cpp"
#include "netw_multiplayer_tests.cpp"
#include "join_payload_tests.cpp"
#include "join_roster_tests.cpp"
#include "netw_identity_tests.cpp"
#include "resolved_join_tests.cpp"
#include "predict_axis_laws.cpp"
#include "predict_compare_laws.cpp"
#include "predict_declaration_laws.cpp"
#include "predict_domain_laws.cpp"
#include "predict_drive_laws.cpp"
#include "predict_episode_laws.cpp"
#include "predict_frame_laws.cpp"
#include "predict_joint_laws.cpp"
#include "predict_lane_laws.cpp"
#include "predict_quarantine_laws.cpp"
#include "predict_recovery_laws.cpp"
#include "predict_replay_laws.cpp"
#include "predict_sensor_laws.cpp"
#include "prelude_tests.cpp"
#include "project_tests.cpp"
#include "quantize_tests.cpp"
#include "promise_tests.cpp"
#include "pump_stats_tests.cpp"
#include "rate_window_tests.cpp"
#include "recorder_tests.cpp"
#include "repl_freshness_book_tests.cpp"
#include "repl_lane_set_tests.cpp"
#include "repl_receive_pass_tests.cpp"
#include "repl_row_frame_tests.cpp"
#include "repl_retained_lane_tests.cpp"
#include "repl_round_trip_laws.cpp"
#include "repl_row_lane_tests.cpp"
#include "repl_window_ring_tests.cpp"
#include "repl_session_send_tests.cpp"
#include "repl_send_pass_tests.cpp"
#include "repl_set_model_tests.cpp"
#include "repl_spawn_plan_tests.cpp"
#include "repl_watch_book_tests.cpp"
#include "reset_tests.cpp"
#include "ring_buffer_tests.cpp"
#include "route_adoption_laws.cpp"
#include "scene_core_tests.cpp"
#include "schema_core_tests.cpp"
#include "session_core_tests.cpp"
#include "settle_queue_tests.cpp"
#include "snapshot_book_tests.cpp"
#include "spawn_book_tests.cpp"
#include "spawn_park_tests.cpp"
#include "spawn_record_tests.cpp"
#include "spawner_roster_tests.cpp"
#include "subsystem_tests.cpp"
#include "table_codec_tests.cpp"
#include "table_grammar_tests.cpp"
#include "timeline_laws.cpp"
#include "txn_book_tests.cpp"
#include "wire_ack_book_tests.cpp"
#include "wire_apply_refusal_tests.cpp"
#include "wire_baseline_book_tests.cpp"
#include "wire_code_row_tests.cpp"
#include "wire_conformance_tests.cpp"
#include "wire_describe_tests.cpp"
#include "wire_fitter_laws.cpp"
#include "wire_fitter_tests.cpp"
#include "wire_gather_refusal_tests.cpp"
#include "wire_identity_coverage_tests.cpp"
#include "wire_plan_tests.cpp"
#include "wire_registry_tests.cpp"
#include "wire_send_replay_tests.cpp"
#include "wire_stream_tests.cpp"
#include "wire_value_row_tests.cpp"

// The module tier's single inclusion point for the shared support layer, so
// this is where its half of the per-case reset is installed. The library build
// installs the same listener from its runner.
NETW_INSTALL_RESET_LISTENER();
NETW_INSTALL_CELLS_LISTENER();
