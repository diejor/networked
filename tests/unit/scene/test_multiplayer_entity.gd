## Tests for [MultiplayerEntity].
##
## Covers [NetwEntity] identity helpers, spawn-property collection,
## and [enum MultiplayerEntity.AuthorityMode] behavior.
class_name TestMultiplayerEntity
extends NetwTestSuite

func test_parse_peer_with_valid_name() -> void:
	assert_that(NetwEntity.parse_peer("alice|42")).is_equal(42)
	assert_that(NetwEntity.parse_entity("alice|42")).is_equal(&"alice")


func test_parse_peer_with_large_peer_id() -> void:
	assert_that(
		NetwEntity.parse_peer("player|2147483647"),
	).is_equal(2147483647)


func test_parse_peer_invalid_names_return_empty_identity() -> void:
	assert_that(
		NetwEntity.parse_peer("no_separator"),
	).is_equal(0)

	assert_that(NetwEntity.parse_peer("")).is_equal(0)

	assert_that(NetwEntity.parse_peer("|")).is_equal(0)
	assert_that(NetwEntity.parse_entity("|")).is_equal(&"")

	assert_that(NetwEntity.parse_peer("a|b|c")).is_equal(0)
	assert_that(NetwEntity.parse_entity("a|b|c")).is_equal(&"")

	assert_that(
		NetwEntity.parse_peer("user|abc"),
	).is_equal(0)


func test_netw_entity_bundle_encodes_name_and_identity() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Player"

	var entity := MultiplayerEntity.new()
	entity.name = "MultiplayerEntity"
	root.add_child(entity)
	entity.owner = root

	NetwEntity.bundle(root, 42, &"alice")

	var entity := NetwEntity.of(root)
	assert_that(root.name).is_equal("alice|42")
	assert_that(entity.entity_id).is_equal(&"alice")
	assert_that(entity.peer_id).is_equal(42)
	assert_that(entity.entity_id).is_equal(&"alice")
	assert_that(entity.peer_id).is_equal(42)


func test_netw_entity_template_flag_reflects_spawner() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var entity := NetwEntity.of(root)

	assert_that(entity.is_template).is_false()

	var entity := MultiplayerEntity.new()
	root.add_child(entity)
	entity.owner = root

	entity.set_multiplayer_entity(entity)

	assert_that(entity.is_template).is_true()


func test_add_spawn_property_adds_with_spawn_only_flags() -> void:
	var entity: MultiplayerEntity = auto_free(MultiplayerEntity.new())
	var path := NodePath(":position")
	entity.add_spawn_property(path)

	var cfg := entity.replication_config
	assert_that(cfg.has_property(path)).is_true()
	assert_that(cfg.property_get_spawn(path)).is_true()
	assert_that(cfg.property_get_sync(path)).is_false()
	assert_that(cfg.property_get_watch(path)).is_false()
	assert_that(
		cfg.property_get_replication_mode(path),
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)


func test_sanitize_coerces_inspector_picked_properties() -> void:
	var entity: MultiplayerEntity = auto_free(MultiplayerEntity.new())
	var cfg := SceneReplicationConfig.new()
	var path := NodePath(":visible")
	cfg.add_property(path)
	cfg.property_set_replication_mode(
		path,
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
	)
	cfg.property_set_spawn(path, false)
	cfg.property_set_sync(path, true)
	cfg.property_set_watch(path, true)
	entity.replication_config = cfg

	entity._sanitize_replication_config()

	assert_that(cfg.property_get_spawn(path)).is_true()
	assert_that(cfg.property_get_sync(path)).is_false()
	assert_that(cfg.property_get_watch(path)).is_false()
	assert_that(
		cfg.property_get_replication_mode(path),
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)


func test_spawner_collector_does_not_contribute_identity() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "TestPlayer"

	var entity := MultiplayerEntity.new()
	entity.name = "MultiplayerEntity"
	root.add_child(entity)
	entity.owner = root

	var expected := NodePath("MultiplayerEntity:entity_id")
	var cfg := entity.replication_config
	assert_that(cfg == null or not cfg.has_property(expected)).is_true()


func _make_player_root(peer_id: int) -> Array:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Alice|%d" % peer_id

	var entity := MultiplayerEntity.new()
	entity.name = "MultiplayerEntity"
	root.add_child(entity)
	entity.owner = root
	entity.root_path = entity.get_path_to(root)

	return [root, entity]


func test_authority_mode_updates_from_name_when_client_owned() -> void:
	var parts := _make_player_root(42)
	var root: Node2D = parts[0]
	var entity: MultiplayerEntity = parts[1]

	entity.authority_mode = MultiplayerEntity.AuthorityMode.CLIENT
	entity._on_owner_tree_entered()

	assert_that(root.get_multiplayer_authority()).is_equal(42)


func test_authority_mode_leaves_server_and_invalid_names_unchanged() -> void:
	var parts := _make_player_root(42)
	var root: Node2D = parts[0]
	var entity: MultiplayerEntity = parts[1]

	assert_that(root.get_multiplayer_authority()).is_equal(1)

	entity.authority_mode = MultiplayerEntity.AuthorityMode.SERVER
	entity._on_owner_tree_entered()

	assert_that(root.get_multiplayer_authority()).is_equal(1)

	var no_peer_root: Node2D = auto_free(Node2D.new())
	no_peer_root.name = "NoSeparator"

	var no_peer_spawner := MultiplayerEntity.new()
	no_peer_spawner.name = "MultiplayerEntity"
	no_peer_root.add_child(no_peer_spawner)
	no_peer_spawner.owner = no_peer_root
	no_peer_spawner.root_path = no_peer_spawner.get_path_to(no_peer_root)

	no_peer_spawner.authority_mode = MultiplayerEntity.AuthorityMode.CLIENT
	no_peer_spawner._on_owner_tree_entered()

	assert_that(no_peer_root.get_multiplayer_authority()).is_equal(1)


func test_unwrap_returns_spawner_or_null() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var entity := MultiplayerEntity.new()
	entity.name = "MultiplayerEntity"
	root.add_child(entity)
	entity.owner = root

	assert_that(MultiplayerEntity.unwrap(root)).is_equal(entity)

	var empty_root: Node2D = auto_free(Node2D.new())
	assert_that(MultiplayerEntity.unwrap(empty_root)).is_null()
