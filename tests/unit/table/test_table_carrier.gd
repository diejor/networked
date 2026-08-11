## The TABLE channel's carrier contract, driven directly at the receive edge.
##
## No peers and no transport: frames are handed to the carrier the way a
## datagram would deliver them, which is what lets the gate, the counters, and
## the route binding be judged one frame at a time.
class_name TestTableCarrier
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer
var author: TableCore
var authored: RID
var ledger: NetwHandleLedger


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api
	var schema := api.schema_create(&"CarrierMob")
	api.schema_add_column(schema, &"hp", NetwMultiplayer.ColumnType.COLUMN_U16)
	api.schema_seal(schema)
	api.table_create(schema)

	ledger = NetwHandleLedger.new()
	author = TableCore.new()
	authored = ledger.rid_create()
	var authored_schema := SchemaRecord.new()
	authored_schema.name = &"CarrierMob"
	SchemaCore.append_column(authored_schema, &"hp", SchemaCore.ColumnType.U16)
	SchemaCore.fix(authored_schema)
	author.declare(authored, authored_schema)


func _authored_frames(
		routes: PackedInt64Array,
		hp: PackedInt32Array,
		tick: int,
) -> Array[PackedByteArray]:
	author.write_routes(authored, routes)
	author.write_column(authored, 0, hp)
	author.commit(authored, tick)
	return author.encode_frames(authored, 1200)


func _drive(frames: Array, sender: int = 1) -> void:
	for frame: PackedByteArray in frames:
		api._drive_carrier(
			sender,
			NetwFrameEnvelope.pack(
				0,
				0,
				NetwFrameEnvelope.Channel.TABLE,
				frame,
			),
			true,
		)


## Verify the channel id is claimed where the carrier reads it, and that the
## one remaining reserved id stays unclaimed.
func test_the_table_channel_is_seventeen() -> void:
	assert_int(NetwFrameEnvelope.Channel.TABLE).is_equal(17)
	assert_int(NetwFrameEnvelope.Channel.SYNC).is_equal(19)
	var taken := PackedInt32Array()
	for value in NetwFrameEnvelope.Channel.values():
		taken.append(value)
	assert_array(taken).override_failure_message(
		"id 18 stays reserved",
	).not_contains([18])


## Verify a table frame arriving off the carrier reaches the store and binds
## every route it introduced, so a row is an entity the rest of the addon can
## resolve.
func test_a_carried_frame_lands_and_binds_its_routes() -> void:
	var table := api.table_find(&"CarrierMob")

	_drive(
		_authored_frames(
			PackedInt64Array([4, 5]),
			PackedInt32Array([10, 20]),
			3,
		),
	)

	assert_array(api.table_read_routes(table)).is_equal(PackedInt64Array([4, 5]))
	assert_array(api.table_read_column(table, 0)).is_equal(
		PackedInt32Array([10, 20]),
	)
	assert_int(api.table_get_tick(table)).is_equal(3)
	for route in [4, 5]:
		assert_int(api.route_get_state(route)).is_equal(
			NetwMultiplayer.EntityState.LIVE,
		)
		assert_bool(api.rid_from_route(route).is_valid()).is_true()


## Verify a table frame from anyone but the authority is refused before it is
## parsed, since a client that could author rows could author anything.
func test_a_non_server_sender_is_refused() -> void:
	var table := api.table_find(&"CarrierMob")
	var frames := _authored_frames(
		PackedInt64Array([4]),
		PackedInt32Array([1]),
		1,
	)

	_drive(frames, 3)

	assert_array(api.table_read_routes(table)).is_empty()
	assert_int(
		int(api.stats_snapshot()[&"drops_table_bad_sender"]),
	).is_equal(1)
	assert_int(api.get_stat(NetwMultiplayer.Stat.STAT_VERDICT_UNAUTHORIZED)) \
			.is_equal(1)


## Verify each malformed frame is counted under its own name, so a rising
## counter names the cause rather than only the symptom.
func test_malformed_frames_reach_their_own_counters() -> void:
	var table := api.table_find(&"CarrierMob")
	var good: PackedByteArray = _authored_frames(
		PackedInt64Array([4]),
		PackedInt32Array([1]),
		1,
	)[0]

	var unknown := good.duplicate()
	unknown[0] = 42
	_drive([unknown])

	var skewed := good.duplicate()
	skewed[1] = (skewed[1] + 1) % 256
	skewed[2] = (skewed[2] + 1) % 256
	_drive([skewed])

	var flagged := good.duplicate()
	flagged[4] = TableCore.FLAG_PAIR_KEY
	_drive([flagged])

	_drive([good.slice(0, good.size() - 2)])

	var stats := api.stats_snapshot()
	assert_int(int(stats[&"drops_table_unknown"])).is_equal(1)
	assert_int(int(stats[&"drops_table_schema"])).is_equal(1)
	assert_int(int(stats[&"drops_table_unknown_flag"])).is_equal(1)
	assert_int(int(stats[&"drops_table_truncated"])).is_greater_equal(1)
	assert_array(api.table_read_routes(table)).is_empty()

	_drive([good])
	assert_array(api.table_read_routes(table)).is_equal(PackedInt64Array([4]))


## Verify every table stat is reachable by name and by id, so a dashboard
## reading either sees the same number.
func test_the_table_stats_are_reachable_both_ways() -> void:
	var pairs := {
		NetwMultiplayer.Stat.STAT_DROPS_TABLE_UNKNOWN: &"drops_table_unknown",
		NetwMultiplayer.Stat.STAT_DROPS_TABLE_SCHEMA: &"drops_table_schema",
		NetwMultiplayer.Stat.STAT_DROPS_TABLE_UNKNOWN_FLAG: &"drops_table_unknown_flag",
		NetwMultiplayer.Stat.STAT_DROPS_TABLE_TRUNCATED: &"drops_table_truncated",
		NetwMultiplayer.Stat.STAT_DROPS_TABLE_BAD_SENDER: &"drops_table_bad_sender",
		NetwMultiplayer.Stat.STAT_TABLE_DROPS_STALE: &"table_drops_stale",
		NetwMultiplayer.Stat.STAT_TABLE_DROPS_TOMBSTONE: &"table_drops_tombstone",
		NetwMultiplayer.Stat.STAT_TABLE_DROPS_STALE_ROW: &"table_drops_stale_row",
	}
	var snapshot := api.stats_snapshot()
	for stat: int in pairs:
		var name: StringName = pairs[stat]
		assert_bool(snapshot.has(name)).override_failure_message(
			"stats_snapshot must expose %s" % name,
		).is_true()
		assert_int(api.get_stat(stat as NetwMultiplayer.Stat)).is_equal(
			int(snapshot[name]),
		)


## Verify a wave becomes exactly one emission per table at the tick boundary,
## however many frames it took to carry.
func test_table_received_is_one_emission_per_wave() -> void:
	var table := api.table_find(&"CarrierMob")
	var seen: Array[int] = []
	api.table_received.connect(
		func(received: RID, tick: int) -> void:
			assert_that(received).is_equal(table)
			seen.append(tick),
	)

	var routes := PackedInt64Array()
	var hp := PackedInt32Array()
	for i in 200:
		routes.append(100 + i)
		hp.append(i)
	author.write_routes(authored, routes)
	author.write_column(authored, 0, hp)
	author.commit(authored, 8)
	var frames := author.encode_frames(authored, 200)
	assert_int(frames.size()).is_greater(1)

	_drive(frames)
	assert_array(seen).override_failure_message(
		"nothing is emitted until the tick boundary",
	).is_empty()

	api._replication.pump_tables(8)
	assert_array(seen).is_equal([8])

	# A tick that received nothing emits nothing.
	api._replication.pump_tables(9)
	assert_array(seen).is_equal([8])


## Verify the lifecycle stream reaches the store through the same channel and
## retires the route locally.
func test_the_lifecycle_stream_arrives_on_the_same_channel() -> void:
	var table := api.table_find(&"CarrierMob")
	_drive(_authored_frames(PackedInt64Array([4, 5]), PackedInt32Array([1, 2]), 1))

	_drive(TableCore.encode_lifecycle(PackedInt64Array([5]), 2, 1200))

	assert_int(api.table_get_row(table, 5)).is_equal(-1)
	assert_array(api.table_read_deaths(table)).is_equal(PackedInt64Array([5]))
	assert_bool(api._table_core.is_tombstoned(5)).is_true()


## Verify a session that never declared a table pays nothing: no dispatch work,
## no pump work, and no counters moved.
func test_a_session_with_no_tables_pays_nothing() -> void:
	var bare := MultiplayerTree.new()
	bare.name = "BareTree"
	add_child(bare)
	auto_free(bare)

	bare.api._replication.pump_tables(1)

	assert_array(bare.api._table_core.dirty_tables()).is_empty()
	assert_array(bare.api._table_core.published_tables()).is_empty()
	assert_array(bare.api._table_core.touched_tables()).is_empty()
	var stats := bare.api.stats_snapshot()
	assert_int(int(stats[&"drops_table_unknown"])).is_equal(0)
	assert_int(int(stats[&"table_drops_stale"])).is_equal(0)
