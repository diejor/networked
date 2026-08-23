## Law suite for the spawn edge under tick drive alone: a spawn placed between
## two pumps, and a visibility loss committed between two pumps, both reach the
## peer with no idle frame anywhere in the run.
##
## The rig drives with [LockstepStepper], which runs ticks in-process and never
## yields a frame, so anything the pipeline puts on the frame's own deferred
## queue simply never runs. That is the point: the spawn edge belongs to the
## pump the session already drives, and a session that ticks without rendering
## is one a headless server does every day.
class_name TestSpawnSettlesWithoutFrames
extends NetwTestSuite

const TICKRATE := 30

var harness: NetwTestHarness
var client0: MultiplayerTree
var server_clock: NetwClockHandle
var probe_scene: PackedScene

var _stepper: LockstepStepper


func before_test() -> void:
	_stepper = null
	probe_scene = _make_probe_scene()
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	client0 = await harness.add_client()
	server_clock = await harness.add_clock(TICKRATE, 3)
	for mt: MultiplayerTree in [harness.server(), client0]:
		var arena := Node.new()
		arena.name = "Arena"
		mt.add_child(arena)
		var second := Node.new()
		second.name = "Arena2"
		mt.add_child(second)
	await drain_frames(get_tree(), 2)


func test_a_spawn_placed_between_pumps_reaches_the_peer_with_no_frame() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	node.marker = "before-add"
	var entity := _replication().replicate(node)
	_server_arena().add_child(node)
	# No frame has passed since add_child, so this write is inside the window
	# the snapshot has yet to take.
	node.marker = "after-the-frame-would-have-ended"

	_sync_ticks(4)

	var client_node := _client_node(entity.route) as NetwSpawnProbe
	assert_that(client_node).is_not_null()
	assert_str(client_node.enter_tree_report["marker"]) \
			.is_equal("after-the-frame-would-have-ended")


func test_a_visibility_loss_between_pumps_reaches_the_peer_with_no_frame() \
		-> void:
	var peer0 := client0.multiplayer_peer.get_unique_id()
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node)
	_server_arena().add_child(node)

	var interest := harness.server().api
	var layer := interest._native_core.interest_layer(&"settle")
	layer.add_entity(entity)
	interest.interest_flush()
	_sync_ticks(4)
	assert_that(_client_node(entity.route)).is_null()

	layer.add_viewer(peer0)
	interest.interest_flush()
	_sync_ticks(4)
	assert_that(_client_node(entity.route)).is_not_null()

	# Only the sweep issues this edge: the entity itself never moves, so
	# nothing but a re-read of the admission matrix can revoke it.
	layer.remove_viewer(peer0)
	interest.interest_flush()
	_sync_ticks(4)

	assert_that(_client_node(entity.route)).is_null()


func test_a_removed_root_despawns_at_the_pump_with_no_frame() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node)
	_server_arena().add_child(node)
	_sync_ticks(4)
	assert_that(_client_node(entity.route)).is_not_null()

	_server_arena().remove_child(node)
	_sync_ticks(4)

	assert_that(_client_node(entity.route)).is_null()
	assert_that(_server_state(entity.route)) \
			.is_equal(NetwMultiplayer.EntityState.DEAD)
	node.free()


func test_a_root_back_before_the_pump_is_a_reparent_not_a_despawn() -> void:
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node)
	_server_arena().add_child(node)
	_sync_ticks(4)
	assert_that(_client_node(entity.route)).is_not_null()

	# Out and back with no pump between, which is what the grace is for.
	_server_arena().remove_child(node)
	harness.server().get_node("Arena2").add_child(node)
	_sync_ticks(4)

	var client_node := _client_node(entity.route)
	assert_that(client_node).is_not_null()
	assert_that(client_node.get_parent().name).is_equal(&"Arena2")


## Verify a spawn leaves one row for each stage that ran it, on the peer that
## ran that stage. The server declares and reconciles, the client constructs,
## and the plane is where an observer sees which half of a materialization it is
## looking at. Nothing else in the corpus reads a stage row, so a stage that
## stopped reporting would be invisible.
func test_a_spawn_reports_the_stages_that_carried_it() -> void:
	harness.server().api._native_core.event_arm(true)
	client0.api._native_core.event_arm(true)

	var peer0 := client0.multiplayer_peer.get_unique_id()
	var node := probe_scene.instantiate() as NetwSpawnProbe
	var entity := _replication().replicate(node)
	_server_arena().add_child(node)

	# The sweep is what reconciles, so the spawn rides an interest edge rather
	# than the open default: a materialization nobody had to decide about never
	# reaches the stage that decides.
	var interest := harness.server().api
	var layer := interest._native_core.interest_layer(&"stages")
	layer.add_entity(entity)
	interest.interest_flush()
	_sync_ticks(4)
	layer.add_viewer(peer0)
	interest.interest_flush()
	_sync_ticks(4)
	assert_that(_client_node(entity.route)).is_not_null()

	# One drain per ring: reading a ring empties it, so a second read of the
	# same route answers nothing whatever the session did.
	var server_session := _stage_counts(harness.server().api, 0)
	var server_entity := _stage_counts(harness.server().api, entity.route)
	var client_session := _stage_counts(client0.api, 0)
	var client_entity := _stage_counts(client0.api, entity.route)

	assert_int(
		int(server_entity.get(NetwMultiplayerCore.SPAWN_DECLARE, 0))
	).is_greater(0)
	assert_int(
		int(server_session.get(NetwMultiplayerCore.SPAWN_RECONCILE, 0))
	).is_greater(0)
	assert_int(
		int(client_session.get(NetwMultiplayerCore.SPAWN_CONSTRUCT, 0))
	).is_greater(0)
	assert_int(
		int(client_entity.get(NetwMultiplayerCore.SPAWN_DECLARE, 0))
	).is_equal(0)


func test_a_scene_park_drops_its_retry_when_the_last_park_is_gone() -> void:
	var api := harness.server().api
	var pipeline := api._replication._spawn_pipeline
	var retry := Callable(pipeline, "_retry_scene_parked_spawns")

	assert_bool(api.entity_live.is_connected(retry)).is_false()
	pipeline._park_spawn_for_scene(PackedByteArray([1, 2, 3]), 9001)
	assert_bool(api.entity_live.is_connected(retry)).is_true()

	pipeline._park.cancel(9001)
	pipeline._retry_scene_parked_spawns(0, null)
	assert_bool(api.entity_live.is_connected(retry)).is_false()


func test_a_session_end_drops_the_scene_park_retry() -> void:
	var api := harness.server().api
	var pipeline := api._replication._spawn_pipeline
	var retry := Callable(pipeline, "_retry_scene_parked_spawns")

	pipeline._park_spawn_for_scene(PackedByteArray([1, 2, 3]), 9002)
	assert_bool(api.entity_live.is_connected(retry)).is_true()

	pipeline.clear_session()

	assert_bool(api.entity_live.is_connected(retry)).is_false()


# How many rows of each kind a session recorded on a route, draining the ring.
func _stage_counts(api: NetwMultiplayer, route: int) -> Dictionary:
	var seen: Dictionary[int, int] = { }
	for row: NetwEvent in api._native_core.event_ring(route):
		seen[row.event] = int(seen.get(row.event, 0)) + 1
	return seen


func _sync_ticks(n: int) -> void:
	if _stepper == null:
		var clocks: Array[NetwClockHandle] = [server_clock, client0.api._native_core.clock_handle]
		var apis: Array[MultiplayerAPI] = [
			harness.server().multiplayer,
			client0.multiplayer,
		]
		_stepper = LockstepStepper.new(
			clocks,
			apis,
			harness.session(),
			TICKRATE,
		)
	_stepper.sync_ticks(n)


func _replication() -> ReplicationCore:
	return harness.server().api._replication


func _server_arena() -> Node:
	return harness.server().get_node("Arena")


func _server_state(route: int) -> NetwMultiplayer.EntityState:
	var api := harness.server().api
	return api.entity_get_state(api.entity_from_route(route))


func _client_node(route: int) -> Node:
	var entity := client0.api.entity_from_route(route)
	if client0.api.entity_get_state(entity) \
			!= NetwMultiplayer.EntityState.LIVE:
		return null
	return client0.api.entity_get_node(entity)


func _make_probe_scene() -> PackedScene:
	var root := NetwSpawnProbe.new()
	root.name = "SettleProbe"
	var path := NetwPathNamespace.next_path("player", "SettleProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed
