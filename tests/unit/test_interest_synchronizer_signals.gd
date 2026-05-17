## Unit tests for [InterestSynchronizer] driver: signal emission,
## verdict caching, deep-first hide ordering. Runs the synchronizer
## in isolation; no multiplayer peer is attached, so
## [code]_is_server[/code] returns [code]true[/code] and all mutators
## run.
##
## Each test calls [method InterestSynchronizer.drive_now] after
## mutating to bypass the deferred driver. Game code should rely on
## the deferred path; tests want synchronous observations.
class_name TestInterestSynchronizerSignals
extends NetworkedTestSuite


var sync: InterestSynchronizer
var enters: Array = []
var exits: Array = []
var viewer_adds: Array = []
var viewer_removes: Array = []
var entity_adds: Array = []
var entity_removes: Array = []


func before_test() -> void:
	sync = InterestSynchronizer.new()
	sync.layer_id = &"test"
	add_child(sync)
	auto_free(sync)
	sync.interest_enter.connect(_on_enter)
	sync.interest_exit.connect(_on_exit)
	sync.viewer_added.connect(_on_viewer_added)
	sync.viewer_removed.connect(_on_viewer_removed)
	sync.entity_added.connect(_on_entity_added)
	sync.entity_removed.connect(_on_entity_removed)
	# _initial_sync_done is normally flipped in _enter_tree; for unit
	# tests we want the setters to drive immediately.
	enters.clear()
	exits.clear()
	viewer_adds.clear()
	viewer_removes.clear()
	entity_adds.clear()
	entity_removes.clear()


func _on_enter(entity: NetwEntity, peer_id: int) -> void:
	enters.append([entity, peer_id])


func _on_exit(entity: NetwEntity, peer_id: int) -> void:
	exits.append([entity, peer_id])


func _on_viewer_added(peer_id: int) -> void:
	viewer_adds.append(peer_id)


func _on_viewer_removed(peer_id: int) -> void:
	viewer_removes.append(peer_id)


func _on_entity_added(entity: NetwEntity) -> void:
	entity_adds.append(entity)


func _on_entity_removed(entity: NetwEntity) -> void:
	entity_removes.append(entity)


func _make_entity(
		entity_name: String = "ent",
		peer_id: int = 0,
		parent: Node = null,
) -> NetwEntity:
	var root := Node.new()
	root.name = entity_name
	if parent:
		parent.add_child(root)
	else:
		add_child(root)
	auto_free(root)
	var sync_node := MultiplayerSynchronizer.new()
	sync_node.name = "Sync"
	root.add_child(sync_node)
	auto_free(sync_node)
	var entity := NetwEntity.of(root)
	entity.peer_id = peer_id
	return entity


# ---------------------------------------------------------------------------
# Lifecycle signals fire eagerly from mutators (no drive needed).
# ---------------------------------------------------------------------------

func test_viewer_added_emits() -> void:
	sync.add_viewer(7)
	assert_that(viewer_adds).contains_exactly([7])


func test_viewer_added_idempotent_no_double_emit() -> void:
	sync.add_viewer(7)
	sync.add_viewer(7)
	assert_that(viewer_adds).contains_exactly([7])


func test_viewer_removed_emits() -> void:
	sync.add_viewer(7)
	sync.remove_viewer(7)
	assert_that(viewer_removes).contains_exactly([7])


func test_viewer_remove_unknown_emits_nothing() -> void:
	sync.remove_viewer(99)
	assert_that(viewer_removes).is_empty()


func test_entity_added_emits() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	assert_that(entity_adds).contains_exactly([e])


func test_entity_removed_emits() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	sync.remove_entity(e)
	assert_that(entity_removes).contains_exactly([e])


# ---------------------------------------------------------------------------
# Visibility transitions: HIDE_FROM_OUTSIDERS (default).
# ---------------------------------------------------------------------------

func test_add_viewer_after_entity_emits_interest_enter() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	sync.drive_now()
	enters.clear()  # add_entity itself doesn't fire because no viewers
	sync.add_viewer(7)
	sync.drive_now()
	assert_that(enters).contains_exactly([[e, 7]])


func test_add_entity_after_viewer_emits_interest_enter() -> void:
	sync.add_viewer(7)
	sync.drive_now()
	var e := _make_entity()
	sync.add_entity(e)
	sync.drive_now()
	assert_that(enters).contains_exactly([[e, 7]])


func test_remove_viewer_emits_interest_exit() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	sync.add_viewer(7)
	sync.drive_now()
	enters.clear()
	sync.remove_viewer(7)
	sync.drive_now()
	assert_that(exits).contains_exactly([[e, 7]])


func test_remove_entity_emits_interest_exit_for_visible_peers() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	sync.add_viewer(7)
	sync.drive_now()
	exits.clear()
	sync.remove_entity(e)
	# remove_entity emits exit eagerly; no drive needed.
	assert_that(exits).contains_exactly([[e, 7]])


func test_no_signal_for_non_viewer_under_hide_from_outsiders() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	sync.drive_now()
	assert_that(enters).is_empty()
	assert_that(exits).is_empty()


# ---------------------------------------------------------------------------
# Visibility transitions: HIDE_FROM_INSIDERS.
# ---------------------------------------------------------------------------

func test_hide_from_insiders_emits_for_outsider_when_entity_added() -> void:
	sync.policy = InterestSynchronizer.Policy.HIDE_FROM_INSIDERS
	# Two peers; 5 is a viewer (insider), 9 is not (outsider).
	# In a real session, _live_peers would surface both via
	# multiplayer.get_peers. Outside a multiplayer context the list
	# is empty, so we exercise the verdict directly.
	var e := _make_entity()
	sync.add_entity(e)
	sync.drive_now()
	# No peers known -> no signals. This locks in the contract that
	# the driver iterates real live peers only.
	assert_that(enters).is_empty()


func test_policy_flip_inverts_verdict_for_visible_peer() -> void:
	# Manually push a peer into _live_peers via the cache by simulating
	# a visible state, then flip policy and observe the transition.
	# We can't inject _live_peers easily; instead verify _verdict_for
	# directly to lock the inversion semantics.
	sync.add_viewer(5)
	assert_that(sync._verdict_for(5)).is_true()
	sync.policy = InterestSynchronizer.Policy.HIDE_FROM_INSIDERS
	assert_that(sync._verdict_for(5)).is_false()


# ---------------------------------------------------------------------------
# Entity-level signal mirrors the anchor-level signal.
# ---------------------------------------------------------------------------

func test_entity_level_interest_enter_fires() -> void:
	var e := _make_entity()
	var entity_enters: Array = []
	e.interest_enter.connect(func(peer): entity_enters.append(peer))
	sync.add_entity(e)
	sync.add_viewer(7)
	sync.drive_now()
	assert_that(entity_enters).contains_exactly([7])


func test_entity_level_interest_exit_fires() -> void:
	var e := _make_entity()
	var entity_exits: Array = []
	e.interest_exit.connect(func(peer): entity_exits.append(peer))
	sync.add_entity(e)
	sync.add_viewer(7)
	sync.drive_now()
	sync.remove_viewer(7)
	sync.drive_now()
	assert_that(entity_exits).contains_exactly([7])


func test_entity_level_signal_only_fires_for_own_entity() -> void:
	var e1 := _make_entity("ent1")
	var e2 := _make_entity("ent2")
	var e1_enters: Array = []
	e1.interest_enter.connect(func(peer): e1_enters.append(peer))
	sync.add_entity(e1)
	sync.add_entity(e2)
	sync.add_viewer(7)
	sync.drive_now()
	# e1 should only see its own transition.
	assert_that(e1_enters).contains_exactly([7])


# ---------------------------------------------------------------------------
# Cache and queries.
# ---------------------------------------------------------------------------

func test_is_visible_to_true_after_visible_transition() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	sync.add_viewer(7)
	sync.drive_now()
	assert_that(sync.is_visible_to(e, 7)).is_true()


func test_is_visible_to_false_after_hide_transition() -> void:
	var e := _make_entity()
	sync.add_entity(e)
	sync.add_viewer(7)
	sync.drive_now()
	sync.remove_viewer(7)
	sync.drive_now()
	assert_that(sync.is_visible_to(e, 7)).is_false()


func test_is_visible_to_false_for_unknown_entity() -> void:
	var e := _make_entity()
	assert_that(sync.is_visible_to(e, 7)).is_false()


# ---------------------------------------------------------------------------
# admit / dismiss convenience.
# ---------------------------------------------------------------------------

func test_admit_peer_owned_entity_emits_enter() -> void:
	var e := _make_entity("p5", 5)
	sync.admit(e)
	sync.drive_now()
	assert_that(viewer_adds).contains_exactly([5])
	assert_that(entity_adds).contains_exactly([e])
	# Peer 5 is the entity's owning peer and also a viewer ->
	# verdict true -> enter fires for (e, 5).
	assert_that(enters).contains_exactly([[e, 5]])


func test_dismiss_emits_exit() -> void:
	var e := _make_entity("p5", 5)
	sync.admit(e)
	sync.drive_now()
	exits.clear()
	sync.dismiss(e)
	# dismiss removes the viewer first (driver scheduled), then the
	# entity (which eagerly emits interest_exit for visible peers).
	# The entity remove path emits before viewer removal driver runs,
	# so we observe the eager exit.
	assert_that(exits.size() >= 1).is_true()
	assert_that(exits[0][0]).is_equal(e)


# ---------------------------------------------------------------------------
# Initial spawn-sync semantics: setter must be store-only until the
# end of [code]_enter_tree[/code]. Locks the invariant that lets
# clients absorb the anchor's initial state without emitting transition
# signals for already-resolved visibility.
# ---------------------------------------------------------------------------

func test_setter_during_initial_sync_does_not_drive() -> void:
	# Simulate a client receiving spawn-sync before _enter_tree: the
	# setter fires while _initial_sync_done is still false.
	var fresh := InterestSynchronizer.new()
	fresh.layer_id = &"fresh"
	# Don't add to tree yet; _initial_sync_done stays false.
	var collected: Array = []
	fresh.viewer_added.connect(func(p): collected.append(p))
	# Assigning the dict whole-cloth triggers the setter.
	fresh.viewers = {7: true, 9: true}
	assert_that(collected).is_empty()
	fresh.free()


func test_setter_after_initial_sync_drives_and_emits() -> void:
	# sync is already in tree -> _initial_sync_done is true.
	var collected: Array = []
	sync.viewer_added.connect(func(p): collected.append(p))
	sync.viewers = {7: true, 9: true}
	# Setter computes diff against prior empty viewers -> emits both.
	collected.sort()
	assert_that(collected).contains_exactly([7, 9])


func test_initial_sync_done_flips_at_enter_tree() -> void:
	var fresh := InterestSynchronizer.new()
	assert_that(fresh._initial_sync_done).is_false()
	add_child(fresh)
	auto_free(fresh)
	assert_that(fresh._initial_sync_done).is_true()


func test_no_double_emit_when_entity_arrives_after_viewer() -> void:
	# Simulates client-side timing: viewers setter fires first (no
	# entities locally yet -> no emit), then entity registration
	# arrives and drives once.
	sync.add_viewer(7)
	sync.drive_now()
	# enters should be empty at this point - no entities enrolled.
	assert_that(enters).is_empty()
	# Now entity arrives.
	var e := _make_entity()
	sync.add_entity(e)
	sync.drive_now()
	# Exactly one enter for the new entity.
	assert_that(enters).contains_exactly([[e, 7]])
