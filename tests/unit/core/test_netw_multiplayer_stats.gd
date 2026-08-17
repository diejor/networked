## Unit tests for the unified [enum NetwMultiplayer.Stat] diagnostics surface.
class_name TestNetwMultiplayerStats
extends NetwTestSuite

const LEGACY_KEYS: Array[StringName] = [
	&"drops_unknown_route",
	&"drops_not_live",
	&"drops_no_node",
	&"drops_backlog_limit",
	&"drops_traversal",
	&"drops_comp_unresolved",
	&"sends_dropped_unroutable",
	&"sends_dropped_not_live",
	&"sync_drops_stale",
	&"derived_sets_active",
	&"derived_frames_in",
	&"drops_derived_no_set",
	&"drops_derived_bad_sender",
	&"drops_derived_schema",
	&"row_frames_out",
	&"row_frames_full",
	&"sync_sets_active",
	&"sync_frames_out",
	&"sync_frames_in",
	&"delta_frames_out",
	&"delta_frames_in",
	&"drops_sync_no_set",
	&"drops_sync_bad_sender",
	&"drops_sync_poisoned",
	&"drops_sync_unknown_flag",
	&"spawn_book_armed",
	&"spawn_book_spawned",
	&"spawn_book_recv",
	&"sent_packets",
	&"sent_bytes",
	&"received_packets",
	&"received_bytes",
	&"state_acks_out",
	&"state_acks_in",
	&"standalone_acks_out",
]


func test_snapshot_preserves_legacy_keys_and_covers_every_stat() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var snapshot := api.stats_snapshot()

	for key in LEGACY_KEYS:
		assert_bool(snapshot.has(key)).is_true()
	for enum_name: String in NetwMultiplayer.Stat.keys():
		var stat: int = NetwMultiplayer.Stat[enum_name]
		var key := StringName(enum_name.trim_prefix("STAT_").to_lower())
		assert_bool(snapshot.has(key)).is_true()
		assert_int(api.get_stat(stat)).is_equal(int(snapshot[key]))
	assert_int(snapshot.size()).is_equal(NetwMultiplayer.Stat.size())
	assert_that(api.stats_snapshot()).is_equal(snapshot)

	api.embedding.dispose()
