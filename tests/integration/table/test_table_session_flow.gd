## Tables across a clocked loopback pair, end to end.
##
## The claim under test is the product one: a server writes plain arrays and
## commits, and a client that owns no node for any of those rows reads them back
## with identity, cohorts, tombstones, and a late-join heal already handled.
class_name TestTableSessionFlow
extends NetwTestSuite

const TICKRATE := 30

var harness: NetwTestHarness
var server: MultiplayerTree
var client: MultiplayerTree
var server_clock: NetwClockHandle
var stepper: LockstepStepper

var mob_pos: int
var mob_hp: int
var rock_integrity: int


func before_test() -> void:
	NetwSchemaModel.clear()
	var mobs := Netw.configure_schema(&"FlowMob")
	mob_pos = mobs.vector3(&"pos")
	mob_hp = mobs.u16(&"hp")
	var rocks := Netw.configure_schema(&"FlowRock").reliable()
	rock_integrity = rocks.f32(&"integrity")


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	harness = null
	stepper = null
	server = null
	client = null
	server_clock = null
	NetwSchemaModel.clear()
	await NetwTestSuite.drain_frames(get_tree(), 2)
	await super.after_test()


func _pair() -> void:
	harness = make_harness()
	await harness.setup()
	client = await harness.add_client()
	server = harness.server()
	server_clock = await harness.add_clock(TICKRATE)
	stepper = LockstepStepper.new(
		[server_clock, client.api._native_core.clock_handle],
		[server.api, client.api],
		harness.session(),
		TICKRATE,
	)


func _publish_mobs(
		routes: PackedInt64Array,
		positions: PackedVector3Array,
		hp: PackedInt32Array,
) -> void:
	var table := server.api.table_find(&"FlowMob")
	server.api.table_write_routes(table, routes)
	server.api.table_write_column(table, mob_pos, positions)
	server.api.table_write_column(table, mob_hp, hp)
	assert_int(server.api.table_commit(table)).is_equal(OK)


## Verify a commit on the server becomes rows on a client that owns no node for
## any of them, with the routes live and resolvable on both peers.
func test_a_commit_reaches_a_client_that_owns_no_nodes() -> void:
	await _pair()
	var routes := server.api.claim_routes(3)
	_publish_mobs(
		routes,
		PackedVector3Array([Vector3.ZERO, Vector3.UP, Vector3(3, 0, 0)]),
		PackedInt32Array([100, 90, 80]),
	)

	stepper.sync_ticks(3)

	var table := client.api.table_find(&"FlowMob")
	assert_bool(table.is_valid()).is_true()
	assert_array(client.api.table_read_routes(table)).is_equal(routes)
	assert_array(client.api.table_read_column(table, mob_hp)).is_equal(
		PackedInt32Array([100, 90, 80]),
	)
	assert_that(client.api.table_read_column(table, mob_pos)[2]).is_equal(
		Vector3(3, 0, 0),
	)
	for value in routes:
		assert_int(client.api.entity_get_state(
				client.api.entity_from_route(int(value)))).is_equal(
			NetwMultiplayer.EntityState.LIVE,
		)
		assert_object(
			client.api.entity_get_node(client.api.entity_from_route(int(value))),
		).is_null()


## Verify the client hears one emission per wave and can read the wave's
## cohorts from inside the handler.
func test_the_client_hears_one_emission_per_wave() -> void:
	await _pair()
	var table := client.api.table_find(&"FlowMob")
	var births: Array = []
	var ticks: Array[int] = []
	client.api.table_received.connect(
		func(received: RID, tick: int) -> void:
			if received != table:
				return
			ticks.append(tick)
			births.append(Array(client.api.table_read_births(table))),
	)

	var routes := server.api.claim_routes(2)
	_publish_mobs(
		routes,
		PackedVector3Array([Vector3.ZERO, Vector3.ONE]),
		PackedInt32Array([1, 2]),
	)
	stepper.sync_ticks(3)

	assert_int(ticks.size()).override_failure_message(
		"one commit must produce exactly one emission",
	).is_equal(1)
	assert_array(births[0]).is_equal(Array(routes))


## Verify releasing a route retires it on the client too, so a row cannot come
## back and the client learns about the loss as a death.
func test_releasing_a_route_tombstones_it_on_the_client() -> void:
	await _pair()
	var routes := server.api.claim_routes(3)
	_publish_mobs(
		routes,
		PackedVector3Array([Vector3.ZERO, Vector3.UP, Vector3.RIGHT]),
		PackedInt32Array([1, 2, 3]),
	)
	stepper.sync_ticks(3)

	var doomed := PackedInt64Array([routes[1]])
	assert_int(server.api.release_routes(doomed)).is_equal(OK)
	_publish_mobs(
		PackedInt64Array([routes[0], routes[2]]),
		PackedVector3Array([Vector3.ZERO, Vector3.RIGHT]),
		PackedInt32Array([1, 3]),
	)
	stepper.sync_ticks(3)

	var table := client.api.table_find(&"FlowMob")
	assert_int(client.api.table_get_row(table, int(routes[1]))).is_equal(-1)
	assert_array(client.api.table_read_deaths(table)).contains(
		[int(routes[1])],
	)
	assert_int(client.api.entity_get_state(
			client.api.entity_from_route(int(routes[1])))).is_equal(
		NetwMultiplayer.EntityState.DEAD,
	)
	assert_int(client.api.table_read_routes(table).size()).is_equal(2)


## Verify a peer that connects after a table already published is healed by a
## snapshot rather than waiting for a row to change.
func test_a_late_joiner_is_healed_by_a_snapshot() -> void:
	await _pair()
	var routes := server.api.claim_routes(4)
	_publish_mobs(
		routes,
		PackedVector3Array(
			[Vector3.ZERO, Vector3.UP, Vector3.RIGHT, Vector3.DOWN],
		),
		PackedInt32Array([10, 20, 30, 40]),
	)
	stepper.sync_ticks(3)

	var latecomer := await harness.add_client()
	stepper = LockstepStepper.new(
		[server_clock, client.api._native_core.clock_handle, latecomer.api._native_core.clock_handle],
		[server.api, client.api, latecomer.api],
		harness.session(),
		TICKRATE,
	)
	stepper.sync_ticks(4)

	var table := latecomer.api.table_find(&"FlowMob")
	assert_bool(table.is_valid()).is_true()
	assert_array(latecomer.api.table_read_routes(table)).override_failure_message(
		"a joiner is owed the whole table, not the next change to it",
	).is_equal(routes)
	assert_array(latecomer.api.table_read_column(table, mob_hp)).is_equal(
		PackedInt32Array([10, 20, 30, 40]),
	)
	assert_array(latecomer.api.table_read_births(table)).is_equal(routes)


## Verify a rare-change table survives a lossy link, which is the reason
## [constant NetwMultiplayer.TableParam.TABLE_PARAM_RELIABLE] exists: it has no
## next commit to heal with.
func test_a_reliable_table_survives_a_lossy_link() -> void:
	await _pair()
	harness.link(client, 1).loss(0.6)

	var routes := server.api.claim_routes(2)
	var rocks := server.api.table_find(&"FlowRock")
	server.api.table_write_routes(rocks, routes)
	server.api.table_write_column(
		rocks,
		rock_integrity,
		PackedFloat32Array([1.0, 0.0]),
	)
	assert_int(server.api.table_commit(rocks)).is_equal(OK)

	stepper.sync_ticks(20)
	harness.clear_links()
	stepper.sync_ticks(10)

	var table := client.api.table_find(&"FlowRock")
	assert_array(client.api.table_read_routes(table)).override_failure_message(
		"a reliable table must arrive even when every datagram was dropped",
	).is_equal(routes)
	assert_array(client.api.table_read_column(table, rock_integrity)).is_equal(
		PackedFloat32Array([1.0, 0.0]),
	)


## Verify an unreliable table heals itself: losing frames costs freshness for
## one tick and the next commit puts everything back.
func test_an_unreliable_table_heals_on_the_next_commit() -> void:
	await _pair()
	harness.link(client, 1).loss(0.5)

	var routes := server.api.claim_routes(40)
	var positions := PackedVector3Array()
	var hp := PackedInt32Array()
	for i in 40:
		positions.append(Vector3(i, 0, 0))
		hp.append(i)
	for _tick in 6:
		_publish_mobs(routes, positions, hp)
		stepper.sync_ticks(1)

	harness.clear_links()
	for _tick in 4:
		_publish_mobs(routes, positions, hp)
		stepper.sync_ticks(1)

	var table := client.api.table_find(&"FlowMob")
	assert_array(client.api.table_read_routes(table)).is_equal(routes)
	assert_array(client.api.table_read_column(table, mob_hp)).is_equal(hp)


## Verify the host reads its own committed snapshot and hears its own emission,
## so client code and listen-server code are the same code.
func test_the_host_reads_and_hears_its_own_commit() -> void:
	await _pair()
	var table := server.api.table_find(&"FlowMob")
	var heard: Array[int] = []
	server.api.table_received.connect(
		func(received: RID, tick: int) -> void:
			if received == table:
				heard.append(tick),
	)

	var routes := server.api.claim_routes(2)
	_publish_mobs(
		routes,
		PackedVector3Array([Vector3.ZERO, Vector3.ONE]),
		PackedInt32Array([7, 8]),
	)
	stepper.sync_ticks(2)

	assert_int(heard.size()).is_equal(1)
	assert_array(server.api.table_read_routes(table)).is_equal(routes)
	assert_array(server.api.table_read_column(table, mob_hp)).is_equal(
		PackedInt32Array([7, 8]),
	)
	assert_array(server.api.table_read_births(table)).is_equal(routes)


## Verify a component table keyed on the base table's routes joins back in one
## crossing, the composition answer that replaces a presence bitmask.
func test_a_component_table_joins_back_in_one_crossing() -> void:
	NetwSchemaModel.clear()
	var mobs := Netw.configure_schema(&"JoinMob")
	var pos := mobs.vector3(&"pos")
	var hp := mobs.u16(&"hp")
	var burning := Netw.configure_schema(&"JoinBurning")
	var dps := burning.f32(&"dps")
	await _pair()

	var routes := server.api.claim_routes(4)
	var mob_table := server.api.table_find(&"JoinMob")
	server.api.table_write_routes(mob_table, routes)
	server.api.table_write_column(
		mob_table,
		pos,
		PackedVector3Array(
			[Vector3.ZERO, Vector3.UP, Vector3.RIGHT, Vector3.DOWN],
		),
	)
	server.api.table_write_column(
		mob_table,
		hp,
		PackedInt32Array([100, 100, 100, 100]),
	)
	server.api.table_commit(mob_table)

	var alight := PackedInt64Array([routes[3], routes[1]])
	var burn_table := server.api.table_find(&"JoinBurning")
	server.api.table_write_routes(burn_table, alight)
	server.api.table_write_column(burn_table, dps, PackedFloat32Array([5.0, 7.0]))
	server.api.table_commit(burn_table)

	stepper.sync_ticks(3)

	var client_mobs := client.api.table_find(&"JoinMob")
	var client_burn := client.api.table_find(&"JoinBurning")
	var burning_routes := client.api.table_read_routes(client_burn)
	assert_array(burning_routes).is_equal(alight)

	var rows := client.api.table_get_rows(client_mobs, burning_routes)
	assert_array(rows).override_failure_message(
		"a sparse effect costs the rows that have it, and joins in one call",
	).is_equal(PackedInt32Array([3, 1]))
	var client_hp: PackedInt32Array = client.api.table_read_column(
		client_mobs,
		hp,
	)
	assert_int(client_hp[rows[0]]).is_equal(100)
