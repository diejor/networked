## Unit tests for [InterestSynchronizer] in isolation. The primitive
## is dormant - it is not yet wired into [NetwInterest] or the
## [SceneSynchronizer] path - so these tests pin only its single-node
## viewer / entity / policy semantics.
##
## Integration coverage across peers will live in
## [code]tests/integration/test_interest_synchronizer.gd[/code] once
## the primitive is wired into a multi-peer harness.
class_name TestInterestSynchronizer
extends NetworkedTestSuite


var sync: InterestSynchronizer


func before_test() -> void:
	sync = InterestSynchronizer.new()
	add_child(sync)
	auto_free(sync)


func _make_entity(entity_name: String = "ent", peer_id: int = 0) -> NetwEntity:
	var root := Node.new()
	root.name = entity_name
	add_child(root)
	auto_free(root)
	var entity := NetwEntity.of(root)
	entity.peer_id = peer_id
	return entity


# ---------------------------------------------------------------------------
# Exports and defaults.
# ---------------------------------------------------------------------------

func test_default_policy_is_hide_from_outsiders() -> void:
	assert_that(sync.policy).is_equal(
			InterestSynchronizer.Policy.HIDE_FROM_OUTSIDERS)


func test_default_viewers_is_empty() -> void:
	assert_that(sync.viewers).is_empty()


func test_default_entities_is_empty() -> void:
	assert_that(sync.entities).is_empty()


func test_default_layer_id_is_empty() -> void:
	assert_that(String(sync.layer_id)).is_equal("")


func test_public_visibility_enabled() -> void:
	assert_that(sync.public_visibility).is_true()


# ---------------------------------------------------------------------------
# Viewer API.
# ---------------------------------------------------------------------------

func test_add_viewer_is_recorded() -> void:
	sync.add_viewer(5)
	assert_that(sync.has_viewer(5)).is_true()
	assert_that(sync.viewer_ids()).contains_exactly([5])


func test_add_viewer_is_idempotent() -> void:
	sync.add_viewer(5)
	sync.add_viewer(5)
	assert_that(sync.viewer_ids()).contains_exactly([5])


func test_add_viewer_rejects_zero_peer() -> void:
	sync.add_viewer(0)
	assert_that(sync.has_viewer(0)).is_false()


func test_remove_viewer_clears_membership() -> void:
	sync.add_viewer(5)
	sync.remove_viewer(5)
	assert_that(sync.has_viewer(5)).is_false()


func test_remove_unknown_viewer_is_noop() -> void:
	sync.remove_viewer(999)
	assert_that(sync.viewer_ids()).is_empty()


func test_multiple_viewers_tracked_independently() -> void:
	sync.add_viewer(10)
	sync.add_viewer(20)
	assert_that(sync.has_viewer(10)).is_true()
	assert_that(sync.has_viewer(20)).is_true()
	sync.remove_viewer(10)
	assert_that(sync.has_viewer(10)).is_false()
	assert_that(sync.has_viewer(20)).is_true()


# ---------------------------------------------------------------------------
# Verdict / filter under HIDE_FROM_OUTSIDERS.
# ---------------------------------------------------------------------------

func test_outsiders_verdict_hides_unknown_peer() -> void:
	assert_that(sync._verdict_for(99)).is_false()


func test_outsiders_verdict_admits_server_peer() -> void:
	assert_that(sync._verdict_for(
			MultiplayerPeer.TARGET_PEER_SERVER)).is_true()


func test_outsiders_verdict_rejects_zero_peer() -> void:
	assert_that(sync._verdict_for(0)).is_false()


func test_outsiders_verdict_admits_viewer() -> void:
	sync.add_viewer(7)
	assert_that(sync._verdict_for(7)).is_true()


func test_outsiders_verdict_rejects_after_remove() -> void:
	sync.add_viewer(7)
	sync.remove_viewer(7)
	assert_that(sync._verdict_for(7)).is_false()


# ---------------------------------------------------------------------------
# Verdict / filter under HIDE_FROM_INSIDERS.
# ---------------------------------------------------------------------------

func test_insiders_verdict_admits_outsider() -> void:
	sync.policy = InterestSynchronizer.Policy.HIDE_FROM_INSIDERS
	assert_that(sync._verdict_for(99)).is_true()


func test_insiders_verdict_hides_viewer() -> void:
	sync.policy = InterestSynchronizer.Policy.HIDE_FROM_INSIDERS
	sync.add_viewer(7)
	assert_that(sync._verdict_for(7)).is_false()


func test_insiders_verdict_admits_server_peer() -> void:
	sync.policy = InterestSynchronizer.Policy.HIDE_FROM_INSIDERS
	sync.add_viewer(MultiplayerPeer.TARGET_PEER_SERVER)
	assert_that(sync._verdict_for(
			MultiplayerPeer.TARGET_PEER_SERVER)).is_true()


func test_insiders_verdict_rejects_zero_peer() -> void:
	sync.policy = InterestSynchronizer.Policy.HIDE_FROM_INSIDERS
	assert_that(sync._verdict_for(0)).is_false()


# ---------------------------------------------------------------------------
# Entity API.
# ---------------------------------------------------------------------------

func test_add_entity_is_recorded() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	assert_that(sync.has_entity(e)).is_true()


func test_add_entity_is_idempotent() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	sync.add_entity(e)
	assert_that(sync.entities.size()).is_equal(1)


func test_add_entity_rejects_null() -> void:
	sync.add_entity(null)
	assert_that(sync.entities).is_empty()


func test_remove_entity_clears() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	sync.remove_entity(e)
	assert_that(sync.has_entity(e)).is_false()


func test_remove_unknown_entity_is_noop() -> void:
	var e := _make_entity()
	sync.remove_entity(e)
	assert_that(sync.entities).is_empty()


func test_entity_exit_handler_removes_entry() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	assert_that(sync.has_entity(e)).is_true()
	sync._on_entity_tree_exiting(e)
	assert_that(sync.has_entity(e)).is_false()


# ---------------------------------------------------------------------------
# admit / dismiss.
# ---------------------------------------------------------------------------

func test_admit_adds_entity_and_viewer_for_peer_owned() -> void:
	var e := _make_entity("player", 7)
	sync.admit(e)
	assert_that(sync.has_entity(e)).is_true()
	assert_that(sync.has_viewer(7)).is_true()


func test_admit_skips_viewer_for_server_owned() -> void:
	var e := _make_entity("npc", 0)
	sync.admit(e)
	assert_that(sync.has_entity(e)).is_true()
	assert_that(sync.viewer_ids()).is_empty()


func test_dismiss_removes_entity_and_viewer() -> void:
	var e := _make_entity("player", 7)
	sync.admit(e)
	sync.dismiss(e)
	assert_that(sync.has_entity(e)).is_false()
	assert_that(sync.has_viewer(7)).is_false()
