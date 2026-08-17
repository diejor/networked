## The swarm example, driven over a clocked loopback pair.
##
## The example is the product claim, so this suite checks the claim rather than
## the plumbing: a server simulating plain arrays with no node for any mob, a
## client that renders from columns it never owned a node for, and a component
## table that costs only the rows that have it.
##
## [br][br]The last case is the measurement the GDScript era is judged on, and
## it prints rather than asserts a threshold, because a wall-clock number is a
## reading rather than a law.
class_name TestSwarmHarness
extends NetwTestSuite

const TICKRATE := 30
const MEASURED_ROWS := 2000

var harness: NetwTestHarness
var server: MultiplayerTree
var client: MultiplayerTree
var stepper: LockstepStepper
var swarm: SwarmServer
var view: SwarmView


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	harness = null
	stepper = null
	server = null
	client = null
	swarm = null
	view = null
	await NetwTestSuite.drain_frames(get_tree(), 2)
	await super.after_test()


# Steps the simulation and the wire together, one tick at a time, so a test
# reads the same sequence twice.
func _step(ticks: int) -> void:
	for _i in ticks:
		swarm.simulate(1.0 / float(TICKRATE))
		stepper.sync_ticks(1)


func _pair(wave: int) -> void:
	SwarmTables.declare_all()
	harness = make_harness()
	await harness.setup()
	client = await harness.add_client()
	server = harness.server()
	var server_clock := await harness.add_clock(TICKRATE)
	stepper = LockstepStepper.new(
		[server_clock, client.api._clock],
		[server.api, client.api],
		harness.session(),
		TICKRATE,
	)

	swarm = SwarmServer.new()
	swarm.name = "SwarmServer"
	swarm.wave_size = wave
	swarm.self_driven = false
	server.add_child(swarm)
	swarm.start()

	view = SwarmView.new()
	view.name = "SwarmView"
	client.add_child(view)
	view.start()


## Verify the whole loop: the server simulates arrays, the client receives rows
## for mobs it owns no node for, and the view reads them back through the same
## verbs the example ships.
func test_the_swarm_reaches_a_client_that_owns_no_nodes() -> void:
	await _pair(64)

	_step(6)

	var mobs := client.api.table_find(SwarmTables.Mobs.NAME)
	assert_bool(mobs.is_valid()).is_true()
	assert_int(client.api.table_read_routes(mobs).size()).is_equal(64)
	var positions: PackedVector3Array = client.api.table_read_column(
		mobs,
		SwarmTables.Mobs.pos,
	)
	assert_int(positions.size()).is_equal(64)
	assert_float(positions[0].length()).override_failure_message(
		"a quantized position must arrive near the ring it was seeded on",
	).is_between(swarm.ring_radius - 1.0, swarm.ring_radius + 1.0)
	for route in client.api.table_read_routes(mobs):
		assert_object(
			client.api.entity_get_node(client.api.entity_from_route(int(route))),
		).override_failure_message("a mob must have no node anywhere").is_null()


## Verify the mobs keep moving on the client, which is what proves the rows are
## a live stream rather than one delivered snapshot.
func test_the_swarm_keeps_moving_on_the_client() -> void:
	await _pair(32)
	_step(4)

	var mobs := client.api.table_find(SwarmTables.Mobs.NAME)
	var before: PackedVector3Array = client.api.table_read_column(
		mobs,
		SwarmTables.Mobs.pos,
	).duplicate()

	_step(8)

	var after: PackedVector3Array = client.api.table_read_column(
		mobs,
		SwarmTables.Mobs.pos,
	)
	var moved := 0
	for i in mini(before.size(), after.size()):
		if before[i].distance_to(after[i]) > 0.05:
			moved += 1
	assert_int(moved).is_greater(before.size() / 2)


## Verify a component table costs only the rows that have it, joins back in one
## crossing, and reports its own births and deaths.
func test_burning_is_a_sparse_table_that_joins_back() -> void:
	await _pair(40)
	_step(4)

	var alight := PackedInt64Array(
		[swarm.routes[3], swarm.routes[7], swarm.routes[11]],
	)
	for route in alight:
		swarm.ignite(int(route), 20.0, 10.0)
	_step(4)

	var burning := client.api.table_find(SwarmTables.Burning.NAME)
	var mobs := client.api.table_find(SwarmTables.Mobs.NAME)
	var burning_routes := client.api.table_read_routes(burning)
	assert_int(burning_routes.size()).override_failure_message(
		"a sparse effect costs the rows that have it, not a masked section",
	).is_equal(3)

	var rows := client.api.table_get_rows(mobs, burning_routes)
	for row in rows:
		assert_int(row).is_greater_equal(0)
	var hp: PackedInt32Array = client.api.table_read_column(
		mobs,
		SwarmTables.Mobs.hp,
	)
	assert_int(hp[rows[0]]).override_failure_message(
		"burning must be taking health off the joined row",
	).is_less(100)


## Verify a mob that burns to death releases its identity, so both tables drop
## the row and the client tombstones the route rather than keeping a ghost.
func test_a_dead_mob_is_retired_on_the_client() -> void:
	await _pair(16)
	_step(4)

	var doomed := int(swarm.routes[2])
	swarm.ignite(doomed, 4000.0, 10.0)
	_step(12)

	var mobs := client.api.table_find(SwarmTables.Mobs.NAME)
	var burning := client.api.table_find(SwarmTables.Burning.NAME)
	assert_int(client.api.table_get_row(mobs, doomed)).is_equal(-1)
	assert_int(client.api.table_get_row(burning, doomed)).is_equal(-1)
	assert_int(client.api.entity_get_state(
			client.api.entity_from_route(doomed))).is_equal(
		NetwMultiplayer.EntityState.DEAD,
	)
	assert_int(client.api.table_read_routes(mobs).size()).is_equal(15)


## Measures the GDScript era's ceiling: encode and decode microseconds per tick
## for a 2,000-row swarm, printed so the number stays honest rather than
## asserted against a machine-specific threshold.
func test_measures_the_two_thousand_row_ceiling() -> void:
	var ledger := NetwHandleLedger.new()
	var tx := TableCore.new()
	var rx := TableCore.new()
	var handles: Array[RID] = []
	for core in [tx, rx]:
		var schema := SchemaRecord.new()
		schema.name = &"MeasureMob"
		var pos_column := SchemaCore.append_column(
			schema,
			&"pos",
			NetwMultiplayer.ColumnType.COLUMN_VECTOR3,
		)
		SchemaCore.assign_quantizer(
			schema,
			pos_column,
			NetwQuantizeFixed.new().step(0.03).limits(-512.0, 512.0),
		)
		SchemaCore.append_column(schema, &"vel", NetwMultiplayer.ColumnType.COLUMN_VECTOR3)
		SchemaCore.append_column(schema, &"hp", NetwMultiplayer.ColumnType.COLUMN_U16)
		SchemaCore.fix(schema)
		var rid := ledger.rid_create()
		core.declare(rid, schema)
		handles.append(rid)

	var routes := PackedInt64Array()
	var pos := PackedVector3Array()
	var vel := PackedVector3Array()
	var hp := PackedInt32Array()
	routes.resize(MEASURED_ROWS)
	pos.resize(MEASURED_ROWS)
	vel.resize(MEASURED_ROWS)
	hp.resize(MEASURED_ROWS)
	for i in MEASURED_ROWS:
		var angle := TAU * float(i) / float(MEASURED_ROWS)
		routes[i] = i + 1
		pos[i] = Vector3(cos(angle), 0.0, sin(angle)) * 64.0
		vel[i] = Vector3(-sin(angle), 0.0, cos(angle))
		hp[i] = 100
	tx.write_routes(handles[0], routes)
	tx.write_column(handles[0], 0, pos)
	tx.write_column(handles[0], 1, vel)
	tx.write_column(handles[0], 2, hp)
	tx.commit(handles[0], 1)

	var encode_start := Time.get_ticks_usec()
	var frames := tx.encode_frames(handles[0], 1200)
	var encode_us := Time.get_ticks_usec() - encode_start

	var bytes := 0
	for frame: PackedByteArray in frames:
		bytes += frame.size()

	var decode_start := Time.get_ticks_usec()
	rx.begin_intake()
	for frame: PackedByteArray in frames:
		rx.apply_frame(frame)
	var decode_us := Time.get_ticks_usec() - decode_start

	print(
		"SWARM N=%d  encode %d us  decode %d us  %d frames  %d bytes/tick"
		% [MEASURED_ROWS, encode_us, decode_us, frames.size(), bytes],
	)

	assert_int(rx.read_routes(handles[1]).size()).is_equal(MEASURED_ROWS)
	assert_int(rx.tick_of(handles[1])).is_equal(1)
	for frame: PackedByteArray in frames:
		assert_int(frame.size()).is_less_equal(1200)
