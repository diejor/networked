## Conformance suite for "a scene is an ordinary entity".
##
## The claim under test is that declaring a scene adds an admission boundary and
## nothing else. A declared scene spawns, carries replicated properties, lands a
## value on the spawn packet, and reaches a late joiner through exactly the paths
## any other entity uses. Nothing here goes through the scene manager or the
## scene wrapper, because the point is that a scene needs neither to replicate.
@tool
class_name TestSceneEntityConformance
extends NetwTestSuite

var harness: NetwTestHarness
var client0: MultiplayerTree
var arena_scene: PackedScene


func before_test() -> void:
	arena_scene = _make_arena_scene()
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	client0 = await harness.add_client()
	await harness.add_clock()
	await drain_frames(get_tree(), 2)


func test_a_scene_root_declares_itself_through_its_own_init() -> void:
	var api := harness.server().api
	var arena := _spawn_arena()

	assert_bool(api.scene_is_declared(api.rid_of(arena))).is_true()
	assert_that(NetwEntity.of(arena).scene_label).is_equal(&"DeclaredArena")


func test_a_declared_scene_carries_a_value_on_its_spawn_packet() -> void:
	var arena := _spawn_arena(
		func(node: DeclaredSceneRoot) -> void:
			node.countdown_target = 4242
	)

	var mirror := await _wait_for_mirror(arena)

	assert_that(mirror).is_not_null()
	assert_int(mirror.countdown_target).is_equal(4242)


func test_a_declared_scene_replicates_a_post_spawn_change() -> void:
	var arena := _spawn_arena()
	var mirror := await _wait_for_mirror(arena)
	assert_that(mirror).is_not_null()

	arena.round_number = 7

	var arrived := false
	for i in 120:
		await get_tree().process_frame
		if mirror.round_number == 7:
			arrived = true
			break

	assert_bool(arrived).is_true()


func test_the_facet_and_stem_survive_the_wire() -> void:
	var arena := _spawn_arena()
	var mirror := await _wait_for_mirror(arena)
	assert_that(mirror).is_not_null()

	var client_api := client0.api
	assert_bool(client_api.scene_is_declared(client_api.rid_of(mirror))).is_true()
	assert_that(NetwEntity.of(mirror).scene_label).is_equal(&"DeclaredArena")


func test_the_facet_reaches_a_peer_that_joined_after_the_spawn_flushed() -> void:
	var arena := _spawn_arena(
		func(node: DeclaredSceneRoot) -> void:
			node.countdown_target = 99
	)
	assert_that(await _wait_for_mirror(arena)).is_not_null()

	var late := await harness.add_client()

	var mirror := await _wait_for_mirror(arena, late)

	assert_that(mirror).is_not_null()
	assert_bool(late.api.scene_is_declared(late.api.rid_of(mirror))).is_true()
	assert_int(mirror.countdown_target).is_equal(99)


func test_declaring_a_live_entity_is_a_programmer_error() -> void:
	var api := harness.server().api
	var live := api.rid_of(_spawn_arena())

	assert_that(api.scene_declare(live)).is_equal(ERR_UNCONFIGURED)
	assert_that(api.scene_undeclare(live)).is_equal(ERR_UNCONFIGURED)


func test_undeclaring_before_arm_leaves_an_ordinary_entity() -> void:
	var api := harness.server().api
	var node := arena_scene.instantiate() as DeclaredSceneRoot
	var entity := api.rid_of(node)

	assert_that(api.scene_undeclare(entity)).is_equal(OK)

	api._replication.replicate(node)
	harness.server().add_child(node)
	var mirror := await _wait_for_mirror(node)

	assert_bool(api.scene_is_declared(entity)).is_false()
	assert_that(mirror).is_not_null()
	assert_bool(client0.api.scene_is_declared(client0.api.rid_of(mirror))).is_false()


func _spawn_arena(configure: Callable = Callable()) -> DeclaredSceneRoot:
	var node := arena_scene.instantiate() as DeclaredSceneRoot
	if configure.is_valid():
		configure.call(node)
	harness.server().api._replication.replicate(node)
	harness.server().add_child(node)
	return node


func _wait_for_mirror(
		arena: DeclaredSceneRoot,
		client: MultiplayerTree = null,
		frames: int = 120,
) -> DeclaredSceneRoot:
	var target := client if client else client0
	var route := NetwEntity.of(arena).route
	for i in frames:
		if target.api.route_get_state(route) \
				== NetwMultiplayer.EntityState.LIVE:
			return target.api.entity_get_node(
				target.api.rid_from_route(route),
			) as DeclaredSceneRoot
		await get_tree().process_frame
	return null


func _make_arena_scene() -> PackedScene:
	var root := DeclaredSceneRoot.new()
	root.name = "DeclaredArena"
	var path := NetwPathNamespace.next_path("level", "DeclaredArena")
	var packed := SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed
