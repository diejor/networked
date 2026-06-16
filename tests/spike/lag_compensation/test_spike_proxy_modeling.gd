## Tier A spike: ProxySynchronizer modeling (architecture P2 substrate).
##
## The "ride Godot replication" stance: the lag-comp synchronizers must model on
## [ProxySynchronizer] without a parallel identity registry. These check virtual
## name identity, root-anchored config paths, authority-derived direction, and
## that finalized proxies are visible to [SynchronizersCache] now that the
## root_path pivot is gone (the proxy-rootpath-fracture verification).
class_name TestSpikeProxyModeling
extends NetwTestSuite


func _const_right(_tick: int) -> Dictionary:
	return {&"mx": 1.0, &"my": 0.0}


func _build_player() -> Node2D:
	var root := Node2D.new()
	root.name = "SpikePlayer"

	var state := SpikeStateSync.new()
	state.name = "StateSync"
	state.bundled = true
	root.add_child(state)
	state.owner = root
	state.root_path = state.get_path_to(root)

	var input := SpikeInputSync.new()
	input.name = "InputSync"
	input.controller_id = 1
	root.add_child(input)
	input.owner = root
	input.root_path = input.get_path_to(root)

	add_child(root)
	auto_free(root)
	return root


func test_a2_virtual_names_and_root_anchored_paths() -> void:
	var root := _build_player()
	var state: SpikeStateSync = root.get_node("StateSync")

	assert_that(state.has_virtual_property(&"__state")).is_true()

	# Root-anchored config path (Player/Proxy:vname), never the old ":vname".
	var cfg := state.replication_config
	assert_that(cfg.has_property(NodePath("StateSync:__state"))).is_true()
	assert_that(cfg.has_property(NodePath(":__state"))).is_false()


func test_a4_finalized_proxies_visible_to_cache() -> void:
	# Under the old root_path="." pivot these were invisible (get_node(".") was
	# the proxy itself, not the entity root), breaking interest and interpolator
	# discovery. Root-anchored paths fix it.
	var root := _build_player()
	var found := SynchronizersCache.get_synchronizers(root)
	var names: Array = found.map(func(s: MultiplayerSynchronizer) -> String: return s.name)
	assert_that(names.has("StateSync")).is_true()
	assert_that(names.has("InputSync")).is_true()


# This suite is the last consumer of SpikePredictRig (and through it
# SpikePrediction/SpikeTimeline). The real-node prediction suites under
# tests/integration/sim/ replaced the four migrated spike suites, so retiring the
# prediction doubles is blocked only on moving these two rig-using cases off it.
func test_a3_authority_derives_direction() -> void:
	var rig := SpikePredictRig.new()
	await rig.setup(self, _const_right)
	rig.delay_both(4)
	rig.sync_ticks(40)

	var server_state: SpikeStateSync = rig.server_body.get_node("StateSync")
	var client_input: SpikeInputSync = rig.client_body.get_node("InputSync")
	var controller := rig.client.multiplayer_peer.get_unique_id()

	# State is server-authored; input is controller-authored.
	assert_that(server_state.get_multiplayer_authority()).is_equal(1)
	assert_that(client_input.get_multiplayer_authority()).is_equal(controller)

	# Functional direction: client input reached the server, server state reached
	# the client (both timelines advanced).
	assert_that(rig.server.consumed_count).is_greater(0)
	assert_that(rig.predictor.divergence_log.size()).is_greater(0)


func test_a2_peer_symmetric_virtual_names() -> void:
	var rig := SpikePredictRig.new()
	await rig.setup(self, _const_right)
	rig.sync_ticks(5)

	var server_state: SpikeStateSync = rig.server_body.get_node("StateSync")
	var client_state: SpikeStateSync = rig.client_body.get_node("StateSync")
	# The same scene yields the same virtual map on both peers, so the timeline
	# keys match without an identity server.
	assert_that(client_state.get_virtual_properties()) \
			.is_equal(server_state.get_virtual_properties())
