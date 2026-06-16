## Integration tests for ProxySynchronizer modeling on the production
## [StateSynchronizer] / [InputSynchronizer] (architecture P2 substrate).
##
## The "ride Godot replication" stance: the lag-comp synchronizers model on
## [ProxySynchronizer] without a parallel identity registry. These check virtual
## name identity, root-anchored config paths, and that finalized proxies are
## visible to [SynchronizersCache] now that the root_path pivot is gone (the
## proxy-rootpath-fracture verification). Built through [PlayerBuilder] so the
## assertions run against the exact entity structure the game composes.
class_name TestProxyModeling
extends NetwTestSuite


func _build_player() -> Node2D:
	var root := PlayerBuilder.new("ProxyPlayer") \
			.with_root(Node2D) \
			.with_state([&"position"]) \
			.with_input([&"motion"]) \
			.build() as Node2D
	add_child(root)
	auto_free(root)
	await (Engine.get_main_loop() as SceneTree).process_frame
	return root


func test_virtual_names_and_root_anchored_paths() -> void:
	var root := await _build_player()
	var state := NetwEntity.of(root).state

	# The stamp and ack are virtualized names, not real node properties.
	assert_that(state.has_virtual_property(StampedSynchronizer.TICK)).is_true()
	assert_that(state.has_virtual_property(StateSynchronizer.ACK)).is_true()

	# Root-anchored config path (StateSync:__tick), never the old ":__tick".
	var cfg := state.replication_config
	assert_that(cfg.has_property(NodePath("StateSync:__tick"))).is_true()
	assert_that(cfg.has_property(NodePath(":__tick"))).is_false()


func test_finalized_proxies_visible_to_cache() -> void:
	# Under the old root_path="." pivot these were invisible (get_node(".") was
	# the proxy itself, not the entity root), breaking interest and interpolator
	# discovery. Root-anchored paths fix it.
	var root := await _build_player()
	var found := SynchronizersCache.get_synchronizers(root)
	var names: Array = found.map(func(s: MultiplayerSynchronizer) -> String: return s.name)
	assert_that(names.has("StateSync")).is_true()
	assert_that(names.has("InputSync")).is_true()
