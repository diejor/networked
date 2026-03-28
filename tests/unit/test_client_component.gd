class_name TestClientComponent
extends GdUnitTestSuite


# ---------------------------------------------------------------------------
# parse_authority() — pure static, no scene tree needed
# ---------------------------------------------------------------------------

func test_parse_authority_with_valid_name() -> void:
	assert_that(ClientComponent.parse_authority("alice|42")).is_equal(42)


func test_parse_authority_with_large_peer_id() -> void:
	assert_that(ClientComponent.parse_authority("player|2147483647")).is_equal(2147483647)


func test_parse_authority_without_separator_returns_zero() -> void:
	assert_that(ClientComponent.parse_authority("no_separator")).is_equal(0)


func test_parse_authority_with_empty_string_returns_zero() -> void:
	assert_that(ClientComponent.parse_authority("")).is_equal(0)


func test_parse_authority_with_only_separator_returns_zero() -> void:
	# "|" splits into ["", ""] — "".to_int() == 0
	assert_that(ClientComponent.parse_authority("|")).is_equal(0)


func test_parse_authority_with_multiple_separators_returns_zero() -> void:
	# "a|b|c" splits into 3 parts, size != 2
	assert_that(ClientComponent.parse_authority("a|b|c")).is_equal(0)


func test_parse_authority_with_non_numeric_peer_returns_zero() -> void:
	# "abc".to_int() returns 0 in GDScript
	assert_that(ClientComponent.parse_authority("user|abc")).is_equal(0)


# ---------------------------------------------------------------------------
# config_spawn_properties() — builds SceneReplicationConfig from sibling syncs
# ---------------------------------------------------------------------------
# These tests build a manual node tree (not added to the scene tree) to test
# the config aggregation logic. SynchronizersCache.get_client_synchronizers()
# uses find_children() which works on disconnected trees.

func test_config_spawn_properties_aggregates_syncs() -> void:
	# Build: root -> ClientComponent -> SpawnSync
	#         root -> PlayerSync (with one tracked property)
	var root: Node2D = auto_free(Node2D.new())
	root.name = "TestPlayer"

	var client := ClientComponent.new()
	client.name = "ClientComponent"
	root.add_child(client)
	client.owner = root

	var spawn_sync := MultiplayerSynchronizer.new()
	spawn_sync.name = "SpawnSynchronizer"
	spawn_sync.unique_name_in_owner = true
	spawn_sync.root_path = NodePath("../..")
	client.add_child(spawn_sync)
	spawn_sync.owner = root

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

	var result := client.config_spawn_properties(client)
	assert_that(result.has_property(NodePath(":position"))).is_true()
	assert_that(result.property_get_spawn(NodePath(":position"))).is_true()
	assert_that(result.property_get_sync(NodePath(":position"))).is_false()


func test_config_spawn_properties_skips_spawn_sync() -> void:
	var root: Node2D = auto_free(Node2D.new())
	root.name = "TestPlayer"

	var client := ClientComponent.new()
	client.name = "ClientComponent"
	root.add_child(client)
	client.owner = root

	var spawn_sync := MultiplayerSynchronizer.new()
	spawn_sync.name = "SpawnSynchronizer"
	spawn_sync.unique_name_in_owner = true
	spawn_sync.root_path = NodePath("../..")
	var spawn_config := SceneReplicationConfig.new()
	spawn_config.add_property(NodePath(":visible"))
	spawn_sync.replication_config = spawn_config
	client.add_child(spawn_sync)
	spawn_sync.owner = root

	# No other syncs — result should be empty since spawn_sync is excluded
	var result := client.config_spawn_properties(client)
	assert_that(result.has_property(NodePath(":visible"))).is_false()
