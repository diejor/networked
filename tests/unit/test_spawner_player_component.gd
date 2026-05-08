## Tests for [SpawnerPlayerComponent].
##
## Covers [method SpawnerComponent.parse_authority], spawn-property
## collection via [method SpawnerComponent.add_spawn_property] +
## [signal NetwEntity.collecting_spawn_properties], and
## [enum SpawnerComponent.AuthorityMode] behaviour in
## [method SpawnerComponent._on_owner_tree_entered].
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
# add_spawn_property() / sanitize / collector contributions
# ---------------------------------------------------------------------------

func test_add_spawn_property_adds_with_spawn_only_flags() -> void:
	var spawner: SpawnerPlayerComponent = auto_free(SpawnerPlayerComponent.new())
	var path := NodePath(":position")
	spawner.add_spawn_property(path)

	var cfg := spawner.replication_config
	assert_that(cfg.has_property(path)).is_true()
	assert_that(cfg.property_get_spawn(path)).is_true()
	assert_that(cfg.property_get_sync(path)).is_false()
	assert_that(cfg.property_get_watch(path)).is_false()
	assert_that(
		cfg.property_get_replication_mode(path)
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)


func test_sanitize_coerces_inspector_picked_properties() -> void:
	var spawner: SpawnerPlayerComponent = auto_free(SpawnerPlayerComponent.new())
	var cfg := SceneReplicationConfig.new()
	var path := NodePath(":visible")
	cfg.add_property(path)
	cfg.property_set_replication_mode(
		path, SceneReplicationConfig.REPLICATION_MODE_ALWAYS
	)
	cfg.property_set_spawn(path, false)
	cfg.property_set_sync(path, true)
	cfg.property_set_watch(path, true)
	spawner.replication_config = cfg

	spawner._sanitize_replication_config()

	assert_that(cfg.property_get_spawn(path)).is_true()
	assert_that(cfg.property_get_sync(path)).is_false()
	assert_that(cfg.property_get_watch(path)).is_false()
	assert_that(
		cfg.property_get_replication_mode(path)
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)


func test_player_collector_contributes_username() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "TestPlayer"

	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	root.add_child(spawner)
	spawner.owner = root

	spawner._on_collecting_spawn(spawner)

	var expected := NodePath("SpawnerPlayerComponent:username")
	var cfg := spawner.replication_config
	assert_that(cfg.has_property(expected)).is_true()
	assert_that(cfg.property_get_spawn(expected)).is_true()
	assert_that(
		cfg.property_get_replication_mode(expected)
	).is_equal(SceneReplicationConfig.REPLICATION_MODE_NEVER)


# ---------------------------------------------------------------------------
# authority_mode - CLIENT: _on_owner_tree_entered sets authority
# ---------------------------------------------------------------------------

func _make_player_root(peer_id: int) -> Array:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "Alice|%d" % peer_id

	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	root.add_child(spawner)
	spawner.owner = root
	spawner.root_path = spawner.get_path_to(root)

	return [root, spawner]


func test_client_mode_sets_authority_from_name() -> void:
	var parts := _make_player_root(42)
	var root: Node2D = parts[0]
	var spawner: SpawnerPlayerComponent = parts[1]

	spawner.authority_mode = SpawnerComponent.AuthorityMode.CLIENT
	spawner._on_owner_tree_entered()

	assert_that(root.get_multiplayer_authority()).is_equal(42)


func test_server_mode_does_not_change_authority() -> void:
	var parts := _make_player_root(42)
	var root: Node2D = parts[0]
	var spawner: SpawnerPlayerComponent = parts[1]

	assert_that(root.get_multiplayer_authority()).is_equal(1)

	spawner.authority_mode = SpawnerComponent.AuthorityMode.SERVER
	spawner._on_owner_tree_entered()

	assert_that(root.get_multiplayer_authority()).is_equal(1)


func test_client_mode_with_no_peer_in_name_leaves_authority_unchanged() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "NoSeparator"

	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	root.add_child(spawner)
	spawner.owner = root
	spawner.root_path = spawner.get_path_to(root)

	spawner.authority_mode = SpawnerComponent.AuthorityMode.CLIENT
	spawner._on_owner_tree_entered()

	assert_that(root.get_multiplayer_authority()).is_equal(1)


# ---------------------------------------------------------------------------
# unwrap()
# ---------------------------------------------------------------------------

func test_unwrap_returns_spawner_component() -> void:
	var root: Node2D = auto_free(Node2D.new())
	var spawner := SpawnerPlayerComponent.new()
	spawner.name = "SpawnerPlayerComponent"
	root.add_child(spawner)
	spawner.owner = root

	assert_that(SpawnerPlayerComponent.unwrap(root)).is_equal(spawner)


func test_unwrap_returns_null_when_absent() -> void:
	var root: Node2D = auto_free(Node2D.new())
	assert_that(SpawnerPlayerComponent.unwrap(root)).is_null()
