## Regression gate for the consumed-synchronizer interpolation feed across a
## route's lifecycle churn.
##
## After the consumption flip a plain [MultiplayerSynchronizer] no longer feeds
## [NetwMultiplayerCore]'s display runtime through the native
## [signal MultiplayerSynchronizer.synchronized] signal but through the
## pipeline's apply hook. This suite proves that feed keeps delivering samples to
## a spawned entity's interpolation buffer after a late join and after the entity
## goes dark on interest loss and is revived, the two lifecycle transitions that
## rebuild the interpolation runtime.
class_name TestConsumedInterpolationRevival
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var probe_scene: PackedScene
var _server_api: NetwMultiplayer
var _server_clock: NetwClockHandle


func before_test() -> void:
	probe_scene = _make_probe_scene()
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	client0 = await harness.add_client()
	_server_clock = await harness.add_clock(30)
	_server_api = harness.server().api
	_mount_arena(harness.server())
	_mount_arena(client0)
	await drain_frames(get_tree(), 2)


func test_consumed_sync_feeds_interpolation_after_flap_revival() -> void:
	var peer0 := client0.multiplayer_peer.get_unique_id()
	var node := probe_scene.instantiate() as NetwSpawnProbe
	node.name = "InterpProbe"
	var entity := harness.server().api._replication.replicate(node)
	var route := entity.route
	harness.server().get_node("Arena").add_child(node)

	# The server authors a fresh position every tick so the consumed sync always
	# has new state to deliver into the receiver's interpolation buffer.
	_server_api.on_tick.connect(func(_d: float, t: int) -> void: node.position = Vector2(t, -t))

	var interest := harness.server().api
	var layer := interest._native_core.interest_layer(&"interp_flap")
	layer.add_entity(entity)
	layer.add_viewer(peer0)
	interest.interest_flush()
	assert_that(await _wait_state(route, NetwMultiplayer.EntityState.LIVE)).is_true()

	var buffer_key := &"position"
	var first_tick := await _wait_buffer_advance(route, buffer_key, 0)
	assert_int(first_tick).is_greater(0)

	# Interest loss darkens the route, then re-admission revives it.
	layer.remove_viewer(peer0)
	interest.interest_flush()
	assert_that(await _wait_state(route, NetwMultiplayer.EntityState.DEAD)).is_true()

	layer.add_viewer(peer0)
	interest.interest_flush()
	assert_that(await _wait_state(route, NetwMultiplayer.EntityState.LIVE)).is_true()

	# The rebuilt runtime must accept fresh consumed-sync samples again.
	var revived_tick := await _wait_buffer_advance(route, buffer_key, first_tick)
	assert_int(revived_tick).is_greater(first_tick)


func _mount_arena(mt: MultiplayerTree) -> void:
	var arena := Node.new()
	arena.name = "Arena"
	mt.add_child(arena)


func _client_buffer(route: int, key: StringName) -> NetwRingBuffer:
	var node := client0.api.entity_get_node(client0.api.entity_from_route(route))
	if not is_instance_valid(node):
		return null
	var entity := NetwEntity.of(node)
	if not entity:
		return null
	return entity.interpolation.get_buffer(key)


# Waits until the client's interpolation buffer for [param key] holds a sample
# newer than [param after_tick], returning that newest tick.
func _wait_buffer_advance(
		route: int,
		key: StringName,
		after_tick: int,
		frames: int = 240,
) -> int:
	for i in frames:
		var buf := _client_buffer(route, key)
		if buf and buf.newest_tick() > after_tick:
			return buf.newest_tick()
		await get_tree().process_frame
	var last := _client_buffer(route, key)
	return last.newest_tick() if last else -1


func _wait_state(
		route: int,
		state: NetwMultiplayer.EntityState,
		frames: int = 180,
) -> bool:
	for i in frames:
		if client0.api.entity_get_state(
				client0.api.entity_from_route(route)) == state:
			return true
		await get_tree().process_frame
	return false


func _make_probe_scene() -> PackedScene:
	var root := NetwSpawnProbe.new()
	root.name = "InterpStockProbe"

	var sync := MultiplayerSynchronizer.new()
	sync.name = "Sync"
	sync.root_path = NodePath("..")
	var cfg := SceneReplicationConfig.new()
	var pos := NodePath(".:position")
	cfg.add_property(pos)
	cfg.property_set_replication_mode(
		pos,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	sync.replication_config = cfg
	root.add_child(sync)
	sync.owner = root

	var interp := MultiplayerInterpolator.new()
	interp.name = "Interp"
	interp.property_interpolators = {
		&"position": NetwInterpolate.new().lerp().smooth(0.0),
	}
	root.add_child(interp)
	interp.owner = root

	var path := NetwPathNamespace.next_path("player", "InterpStockProbe")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed
