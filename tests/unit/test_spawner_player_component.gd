## Tests for [SpawnerPlayerComponent].
##
## Covers [method SpawnerComponent.parse_authority],
## [method SpawnSynchronizer.config_spawn_properties],
## and [enum SpawnerPlayerComponent.AuthorityMode] behaviour in
## [method SpawnerPlayerComponent._on_owner_tree_entered].
class_name TestSpawnerPlayerComponent
extends NetworkedTestSuite


# ---------------------------------------------------------------------------
# parse_authority() - pure static, no scene tree needed
# ---------------------------------------------------------------------------

func test_parse_authority_with_valid_name() -> void:
	assert_that(SpawnerComponent.parse_authority("alice|42")).is_equal(42)


func test_parse_authority_with_large_peer_id() -> void:
	assert_that(
		SpawnerComponent.parse_authority("player|2147483647")
	).is_equal(2147483647)


func test_parse_authority_without_separator_returns_zero() -> void:
	assert_that(
		SpawnerComponent.parse_authority("no_separator")
	).is_equal(0)


func test_parse_authority_with_empty_string_returns_zero() -> void:
	assert_that(SpawnerComponent.parse_authority("")).is_equal(0)


func test_parse_authority_with_only_separator_returns_zero() -> void:
	assert_that(SpawnerComponent.parse_authority("|")).is_equal(0)


func test_parse_authority_with_multiple_separators_returns_zero() -> void:
	assert_that(SpawnerComponent.parse_authority("a|b|c")).is_equal(0)


func test_parse_authority_with_non_numeric_peer_returns_zero() -> void:
	assert_that(
		SpawnerComponent.parse_authority("user|abc")
	).is_equal(0)


# ---------------------------------------------------------------------------
# config_spawn_properties() - builds SceneReplicationConfig from sibling syncs
# ---------------------------------------------------------------------------

func test_config_spawn_properties_aggregates_syncs() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "TestPlayer"

	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	root.add_child(spawner)
	spawner.owner = root

	SpawnSynchronizer.new(spawner)

	var player_sync := MultiplayerSynchronizer.new()
	player_sync.name = "PlayerSync"
	player_sync.root_path = NodePath("..")
	var config := SceneReplicationConfig.new()
	config.add_property(NodePath(":position"))
	config.property_set_replication_mode(
		NodePath(":position"),
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	player_sync.replication_config = config
	root.add_child(player_sync)
	player_sync.owner = root

	spawner.spawn_sync.config_spawn_properties(spawner)
	var spawn_config := spawner.spawn_sync.replication_config
	assert_that(spawn_config.has_property(NodePath(":position"))).is_true()
	assert_that(spawn_config.property_get_spawn(NodePath(":position"))).is_true()
	assert_that(spawn_config.property_get_sync(NodePath(":position"))).is_false()


func test_config_spawn_properties_skips_spawn_sync() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "TestPlayer"

	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	root.add_child(spawner)
	spawner.owner = root

	var spawn_sync := SpawnSynchronizer.new(spawner)

	var spawn_config := SceneReplicationConfig.new()
	spawn_config.add_property(NodePath(":visible"))
	spawn_sync.replication_config = spawn_config

	spawner.spawn_sync.config_spawn_properties(spawner)
	var config := spawner.spawn_sync.replication_config
	assert_that(config.has_property(NodePath(":visible"))).is_false()


func test_config_spawn_properties_includes_username() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "TestPlayer"

	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	root.add_child(spawner)
	spawner.owner = root

	SpawnSynchronizer.new(spawner)

	spawner.spawn_sync.config_spawn_properties(spawner)
	var spawn_config := spawner.spawn_sync.replication_config

	var expected_path := NodePath("SpawnerPlayerComponent:username")
	assert_that(spawn_config.has_property(expected_path)).is_true()
	assert_that(spawn_config.property_get_spawn(expected_path)).is_true()
	assert_that(
		spawn_config.property_get_replication_mode(expected_path)
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)


# ---------------------------------------------------------------------------
# authority_mode - CLIENT: _on_owner_tree_entered sets authority
# ---------------------------------------------------------------------------

func _make_player_root(peer_id: int) -> Array:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Alice|%d" % peer_id

	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	spawner.unique_name_in_owner = true
	root.add_child(spawner)
	spawner.owner = root

	SpawnSynchronizer.new(spawner)

	return [root, spawner]


func test_client_mode_sets_authority_from_name() -> void:
	var parts := _make_player_root(42)
	var root: Node2D = parts[0]
	var spawner: SpawnerPlayerComponent = parts[1]

	spawner.authority_mode = SpawnerPlayerComponent.AuthorityMode.CLIENT
	spawner._on_owner_tree_entered()

	assert_that(root.get_multiplayer_authority()).is_equal(42)


func test_server_authoritative_mode_does_not_change_authority() -> void:
	var parts := _make_player_root(42)
	var root: Node2D = parts[0]
	var spawner: SpawnerPlayerComponent = parts[1]

	assert_that(root.get_multiplayer_authority()).is_equal(1)

	spawner.authority_mode = SpawnerPlayerComponent.AuthorityMode.SERVER_AUTHORITATIVE
	spawner._on_owner_tree_entered()

	assert_that(root.get_multiplayer_authority()).is_equal(1)


func test_client_mode_with_no_peer_in_name_leaves_authority_unchanged() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "NoSeparator"

	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	spawner.unique_name_in_owner = true
	root.add_child(spawner)
	spawner.owner = root

	SpawnSynchronizer.new(spawner)

	spawner.authority_mode = SpawnerPlayerComponent.AuthorityMode.CLIENT
	spawner._on_owner_tree_entered()

	assert_that(root.get_multiplayer_authority()).is_equal(1)


# ---------------------------------------------------------------------------
# unwrap()
# ---------------------------------------------------------------------------

func test_unwrap_returns_spawner_component() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	spawner.unique_name_in_owner = true
	root.add_child(spawner)
	spawner.owner = root

	assert_that(SpawnerPlayerComponent.unwrap(root)).is_equal(spawner)


func test_unwrap_returns_null_when_absent() -> void:
	var root: Node2D = auto_free(Node2D.new())
	assert_that(SpawnerPlayerComponent.unwrap(root)).is_null()


