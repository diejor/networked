## Tests for [NetwEntity] identity and controller lifecycle behavior.
class_name TestNetwEntityIdentity
extends NetwTestSuite

func test_controller_lifecycle_flow() -> void:
	var root := _make_player_root(42)
	var entity := NetwEntity.of(root)
	entity.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	add_child(root)

	assert_that(root.get_multiplayer_authority()).is_equal(42)
	assert_that(entity.controller).is_equal(42)
	assert_that(NetwEntity.of(root).controller).is_equal(42)
	assert_that(NetwEntity.of(root).control_kind) \
			.is_equal(NetwEntity.CONTROL_PEER_CONTROLLED)

	var server_root := _make_player_root(42)
	var server_entity := NetwEntity.of(server_root)
	server_entity.initial_controller = NetwEntity.INITIAL_SERVER
	add_child(server_root)
	assert_that(server_root.get_multiplayer_authority()).is_equal(1)

	var no_peer_root: Node2D = auto_free(Node2D.new())
	no_peer_root.name = "NoSeparator"
	var no_peer_entity := NetwEntity.ensure(no_peer_root)
	no_peer_entity.initial_controller = NetwEntity.INITIAL_REPRESENTED_PEER
	add_child(no_peer_root)
	assert_that(no_peer_root.get_multiplayer_authority()).is_equal(1)

	var grant_root := _make_player_root(0)
	var grant_entity := NetwEntity.of(grant_root)
	add_child(grant_root)
	grant_entity.grant_control(42)

	assert_that(grant_root.get_multiplayer_authority()).is_equal(42)
	assert_that(grant_entity.controller).is_equal(42)
	assert_that(grant_entity.control_kind) \
			.is_equal(NetwEntity.CONTROL_PEER_CONTROLLED)

	grant_entity.revoke_control()

	assert_that(grant_root.get_multiplayer_authority()).is_equal(1)
	assert_that(grant_entity.controller).is_equal(0)
	assert_that(grant_entity.control_kind) \
			.is_equal(NetwEntity.CONTROL_SERVER_CONTROLLED)

	var validator := TopologyValidator.new()
	grant_entity.grant_control(42)
	assert_that(validator._get_expected_authority(grant_root)).is_equal(42)
	grant_entity.revoke_control()
	assert_that(validator._get_expected_authority(grant_root)).is_equal(1)


func test_route_binds_before_tree_entry() -> void:
	var mt := MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	var native_core := mt.api._native_core

	var route := native_core.liveness_reserve_route()
	assert_that(route).is_greater(0)

	var root := Node2D.new()
	root.name = "player1"
	auto_free(root)

	# Bind identity and the reserved route BEFORE entering the tree. The
	# direct route write is enough: the record re-binds from it at tree entry.
	NetwEntity.bind(root, &"player1", 42)
	var entity := NetwEntity.of(root)
	entity.route = route

	# Add to the tree to trigger tree-entry route binding.
	mt.add_child(root)

	assert_that(native_core.liveness_route_of(entity)).is_equal(route)
	assert_that(native_core.wrapper_for_route(route)).is_equal(entity)


func _make_player_root(peer_id: int) -> Node2D:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "valeria|%d" % peer_id
	NetwEntity.ensure(root)
	return root
