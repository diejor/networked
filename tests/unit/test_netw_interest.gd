## Unit tests for [NetwInterest] facade behavior: layer registry, ROOT
## policy, visibility composition across GRANT / DENY / ISOLATE, and
## flush ordering for nested entities (Godot issue #68508).
##
## Tests construct NetwInterest with a null tree so flushes never
## auto-schedule; mutations are applied via [method NetwInterest.flush_now]
## and verified through [member NetwInterest._visible].
class_name TestNetwInterest
extends NetworkedTestSuite


var interest: NetwInterest


func before_test() -> void:
	interest = NetwInterest.new(null)


# ---------------------------------------------------------------------------
# Layer registry
# ---------------------------------------------------------------------------

func test_root_layer_exists_at_construction() -> void:
	assert_that(interest.root).is_not_null()
	assert_that(interest.root.id).is_equal(NetwInterest.ROOT_ID)
	assert_that(interest.root.policy).is_equal(NetwInterestLayer.Policy.GRANT)


func test_create_layer_registers_id() -> void:
	var l := interest.create_layer(
			&"scene:test", NetwInterestLayer.Policy.ISOLATE)
	assert_that(l).is_not_null()
	assert_that(interest.layer(&"scene:test")).is_same(l)


func test_create_layer_duplicate_id_returns_null() -> void:
	interest.create_layer(&"dup", NetwInterestLayer.Policy.GRANT)
	var second := interest.create_layer(
			&"dup", NetwInterestLayer.Policy.ISOLATE)
	assert_that(second).is_null()


func test_all_layers_includes_root_and_created() -> void:
	var l := interest.create_layer(&"L1", NetwInterestLayer.Policy.GRANT)
	var ids: Array = []
	for layer in interest.all_layers():
		ids.append(layer.id)
	assert_that(ids).contains([NetwInterest.ROOT_ID, &"L1"])


func test_dispose_layer_removes_from_registry() -> void:
	var l := interest.create_layer(&"tmp", NetwInterestLayer.Policy.GRANT)
	l.dispose_immediate()
	assert_that(interest.layer(&"tmp")).is_null()


# ---------------------------------------------------------------------------
# Peer / entity auto-registration on ROOT
# ---------------------------------------------------------------------------

func test_on_peer_connected_adds_to_root() -> void:
	interest._on_peer_connected(42)
	assert_that(interest.root.has_member(42)).is_true()


func test_on_peer_disconnected_removes_from_all_layers() -> void:
	var l := interest.create_layer(&"L", NetwInterestLayer.Policy.GRANT)
	interest._on_peer_connected(7)
	l.add_member(7)
	interest._on_peer_disconnected(7)
	assert_that(interest.root.has_member(7)).is_false()
	assert_that(l.has_member(7)).is_false()


func test_on_entity_ready_adds_to_root() -> void:
	var e := NetwEntity.new()
	interest._on_entity_ready(e)
	assert_that(interest.root.has_subject(e)).is_true()


# ---------------------------------------------------------------------------
# Visibility composition
# ---------------------------------------------------------------------------

func test_grant_root_makes_entity_visible_to_member() -> void:
	var e := NetwEntity.new()
	interest._on_peer_connected(1)
	interest._on_entity_ready(e)
	interest.flush_now()
	assert_that(interest.visible_subjects_for(1)).contains([e])


func test_grant_does_not_leak_to_non_member() -> void:
	var l := interest.create_layer(&"grant", NetwInterestLayer.Policy.GRANT)
	var e := NetwEntity.new()
	l.add_subject(e)
	l.add_member(1)
	# Peer 2 was never added.
	interest.flush_now()
	assert_that(interest.viewers_of(e)).contains([1])
	assert_that(interest.viewers_of(e)).not_contains([2])


func test_isolate_layer_blocks_outside_subjects() -> void:
	var bubble := interest.create_layer(
			&"iso", NetwInterestLayer.Policy.ISOLATE)
	var inside := NetwEntity.new()
	var outside := NetwEntity.new()
	interest._on_peer_connected(1)
	bubble.add_member(1)
	bubble.add_subject(inside)
	interest._on_entity_ready(outside)  # added to ROOT, not bubble
	interest.flush_now()
	var visible := interest.visible_subjects_for(1)
	assert_that(visible).contains([inside])
	assert_that(visible).not_contains([outside])


func test_deny_hides_subject_from_co_members() -> void:
	var deny := interest.create_layer(
			&"deny", NetwInterestLayer.Policy.DENY)
	var e := NetwEntity.new()
	interest._on_peer_connected(1)
	deny.add_member(1)
	deny.add_subject(e)
	interest.flush_now()
	assert_that(interest.visible_subjects_for(1)).not_contains([e])


# ---------------------------------------------------------------------------
# interest_enter / interest_exit emission
# ---------------------------------------------------------------------------

func test_interest_enter_emits_for_layer_member() -> void:
	var l := interest.create_layer(&"L", NetwInterestLayer.Policy.GRANT)
	var e := NetwEntity.new()
	var enters: Array = []
	l.interest_enter.connect(
			func(ent: NetwEntity, pid: int): enters.append([ent, pid]))
	l.add_member(1)
	l.add_subject(e)
	interest.flush_now()
	assert_that(enters.size()).is_equal(1)
	assert_that(enters[0]).is_equal([e, 1])


func test_interest_exit_emits_on_subject_removal() -> void:
	var l := interest.create_layer(&"L", NetwInterestLayer.Policy.GRANT)
	var e := NetwEntity.new()
	l.add_member(1)
	l.add_subject(e)
	interest.flush_now()
	var exits: Array = []
	l.interest_exit.connect(
			func(ent: NetwEntity, pid: int): exits.append([ent, pid]))
	l.remove_subject(e)
	interest.flush_now()
	assert_that(exits.size()).is_equal(1)


# ---------------------------------------------------------------------------
# Flush dirty tracking
# ---------------------------------------------------------------------------

func test_flush_clears_dirty_state() -> void:
	var l := interest.create_layer(&"L", NetwInterestLayer.Policy.GRANT)
	l.add_member(1)
	l.add_subject(NetwEntity.new())
	interest.flush_now()
	assert_that(interest._dirty_peers).is_empty()
	assert_that(interest._dirty_entities).is_empty()


func test_flushed_signal_reports_delta_count() -> void:
	var l := interest.create_layer(&"L", NetwInterestLayer.Policy.GRANT)
	var counts: Array[int] = []
	interest.flushed.connect(func(n: int): counts.append(n))
	l.add_member(1)
	l.add_subject(NetwEntity.new())
	interest.flush_now()
	assert_that(counts.size()).is_equal(1)
	assert_that(counts[0]).is_greater_equal(1)
