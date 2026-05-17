## Integration tests for [InterestSynchronizer] driven by live peer
## ids from a two-peer harness. The canary lives outside the
## [MultiplayerTree] sub-tree (Godot's replication system never sees it)
## so we are exercising the visibility verdict and entity API against
## real peer ids without needing a packed canary scene or scene-manager
## wiring.
##
## End-to-end replication parity (canary placed inside a spawnable
## scene, asserted against [SceneSynchronizer] behavior) belongs in the
## Phase 4 side-by-side suite.
class_name TestInterestSynchronizerIntegration
extends NetworkedTestSuite


var harness: NetworkTestHarness
var container: Node
var sync: InterestSynchronizer
var client0: MultiplayerTree
var client1: MultiplayerTree


func before_test() -> void:
	harness = auto_free(NetworkTestHarness.new())
	add_child(harness)
	await harness.setup(null)

	client0 = await harness.add_client()
	client1 = await harness.add_client()

	# Parent the canary to the test node (outside the MultiplayerTree
	# sub-tree) so Godot's replication system does not try to spawn it
	# on the clients. Mutators still resolve `multiplayer.is_server()`
	# from the test scene tree default (null peer -> treated as server),
	# which matches the production server-only guard.
	container = Node.new()
	container.name = "InterestContainer"
	add_child(container)
	auto_free(container)

	sync = InterestSynchronizer.new()
	sync.name = "Canary"
	container.add_child(sync)


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


func _id0() -> int:
	return client0.multiplayer_peer.get_unique_id()


func _id1() -> int:
	return client1.multiplayer_peer.get_unique_id()


# ---------------------------------------------------------------------------
# Verdict under HIDE_FROM_OUTSIDERS (default policy), real peer ids.
# ---------------------------------------------------------------------------

func test_unregistered_peer_not_visible() -> void:
	assert_that(sync._verdict_for(_id0())).is_false()


func test_server_always_visible() -> void:
	assert_that(sync._verdict_for(
			MultiplayerPeer.TARGET_PEER_SERVER)).is_true()


func test_added_viewer_becomes_visible() -> void:
	sync.add_viewer(_id0())
	assert_that(sync._verdict_for(_id0())).is_true()


func test_other_viewer_remains_hidden() -> void:
	sync.add_viewer(_id0())
	assert_that(sync._verdict_for(_id1())).is_false()


func test_removed_viewer_loses_visibility() -> void:
	sync.add_viewer(_id0())
	sync.remove_viewer(_id0())
	assert_that(sync._verdict_for(_id0())).is_false()


# ---------------------------------------------------------------------------
# Verdict under HIDE_FROM_INSIDERS.
# ---------------------------------------------------------------------------

func test_insiders_unregistered_peer_visible() -> void:
	sync.policy = InterestSynchronizer.Policy.HIDE_FROM_INSIDERS
	assert_that(sync._verdict_for(_id0())).is_true()


func test_insiders_added_viewer_becomes_hidden() -> void:
	sync.policy = InterestSynchronizer.Policy.HIDE_FROM_INSIDERS
	sync.add_viewer(_id0())
	assert_that(sync._verdict_for(_id0())).is_false()


func test_insiders_other_peer_still_visible() -> void:
	sync.policy = InterestSynchronizer.Policy.HIDE_FROM_INSIDERS
	sync.add_viewer(_id0())
	assert_that(sync._verdict_for(_id1())).is_true()


# ---------------------------------------------------------------------------
# Server-only guards: mutators on the server actually mutate.
# ---------------------------------------------------------------------------

func test_add_viewer_on_server_mutates() -> void:
	sync.add_viewer(_id0())
	assert_that(sync.has_viewer(_id0())).is_true()


func test_add_viewer_with_two_live_peers_independent() -> void:
	sync.add_viewer(_id0())
	sync.add_viewer(_id1())
	assert_that(sync.has_viewer(_id0())).is_true()
	assert_that(sync.has_viewer(_id1())).is_true()
	sync.remove_viewer(_id0())
	assert_that(sync.has_viewer(_id0())).is_false()
	assert_that(sync.has_viewer(_id1())).is_true()


# ---------------------------------------------------------------------------
# Entity enrollment under live peers.
# ---------------------------------------------------------------------------

func test_entity_enrollment_records_and_clears() -> void:
	var entity_root := Node.new()
	entity_root.name = "Entity"
	container.add_child(entity_root)
	auto_free(entity_root)

	var entity := NetwEntity.of(entity_root)
	sync.add_entity(entity)
	assert_that(sync.has_entity(entity)).is_true()

	sync.remove_entity(entity)
	assert_that(sync.has_entity(entity)).is_false()


func test_admit_dismiss_with_live_peer() -> void:
	var entity_root := Node.new()
	entity_root.name = "Player"
	container.add_child(entity_root)
	auto_free(entity_root)

	var entity := NetwEntity.of(entity_root)
	entity.peer_id = _id0()

	sync.admit(entity)
	assert_that(sync.has_entity(entity)).is_true()
	assert_that(sync.has_viewer(_id0())).is_true()

	sync.dismiss(entity)
	assert_that(sync.has_entity(entity)).is_false()
	assert_that(sync.has_viewer(_id0())).is_false()
