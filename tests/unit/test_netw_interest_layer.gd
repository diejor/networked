## Unit tests for [NetwInterestLayer] membership, subjects, lifecycle,
## and mirror-mode behavior. Visibility composition (which depends on
## [NetwInterest]) is covered separately in
## [code]test_netw_interest.gd[/code].
class_name TestNetwInterestLayer
extends NetworkedTestSuite


var interest: NetwInterest
var layer: NetwInterestLayer


func before_test() -> void:
	interest = NetwInterest.new(null)
	layer = interest.create_layer(&"layer:test", NetwInterestLayer.Policy.GRANT)


# ---------------------------------------------------------------------------
# Members
# ---------------------------------------------------------------------------

func test_add_member_is_recorded() -> void:
	layer.add_member(5)
	assert_that(layer.has_member(5)).is_true()
	assert_that(layer.members()).contains_exactly([5])


func test_add_member_is_idempotent() -> void:
	layer.add_member(5)
	layer.add_member(5)
	assert_that(layer.members()).contains_exactly([5])


func test_add_member_rejects_zero_peer() -> void:
	layer.add_member(0)
	assert_that(layer.has_member(0)).is_false()


func test_remove_member_clears_membership() -> void:
	layer.add_member(5)
	layer.remove_member(5)
	assert_that(layer.has_member(5)).is_false()


func test_remove_unknown_member_is_noop() -> void:
	layer.remove_member(999)
	assert_that(layer.members()).is_empty()


func test_member_added_signal_fires_once() -> void:
	var received: Array[int] = []
	layer.member_added.connect(func(p: int): received.append(p))
	layer.add_member(7)
	layer.add_member(7)
	assert_that(received).contains_exactly([7])


func test_member_removed_signal_fires() -> void:
	layer.add_member(7)
	var received: Array[int] = []
	layer.member_removed.connect(func(p: int): received.append(p))
	layer.remove_member(7)
	assert_that(received).contains_exactly([7])


# ---------------------------------------------------------------------------
# Subjects
# ---------------------------------------------------------------------------

func test_add_subject_is_recorded() -> void:
	var e := NetwEntity.new()
	layer.add_subject(e)
	assert_that(layer.has_subject(e)).is_true()


func test_add_subject_idempotent() -> void:
	var e := NetwEntity.new()
	var received: Array = []
	layer.subject_added.connect(func(x: NetwEntity): received.append(x))
	layer.add_subject(e)
	layer.add_subject(e)
	assert_that(received.size()).is_equal(1)


func test_remove_subject_clears() -> void:
	var e := NetwEntity.new()
	layer.add_subject(e)
	layer.remove_subject(e)
	assert_that(layer.has_subject(e)).is_false()


func test_add_subject_subscribes_entity() -> void:
	var e := NetwEntity.new()
	layer.add_subject(e)
	assert_that(e.subscribed_layers()).contains([layer])


func test_remove_subject_unsubscribes_entity() -> void:
	var e := NetwEntity.new()
	layer.add_subject(e)
	layer.remove_subject(e)
	assert_that(e.subscribed_layers()).is_empty()


# ---------------------------------------------------------------------------
# add_participant convenience
# ---------------------------------------------------------------------------

func test_add_participant_adds_both_member_and_subject() -> void:
	var e := NetwEntity.new()
	e.peer_id = 42
	layer.add_participant(e)
	assert_that(layer.has_member(42)).is_true()
	assert_that(layer.has_subject(e)).is_true()


func test_add_participant_skips_member_for_server_entity() -> void:
	var e := NetwEntity.new()
	e.peer_id = 0
	layer.add_participant(e)
	assert_that(layer.has_subject(e)).is_true()
	assert_that(layer.members()).is_empty()


# ---------------------------------------------------------------------------
# Disposal
# ---------------------------------------------------------------------------

func test_dispose_immediate_marks_disposed() -> void:
	layer.dispose_immediate()
	assert_that(layer.is_disposed()).is_true()


func test_dispose_immediate_emits_closed() -> void:
	var fired: Array = []
	layer.closed.connect(func(): fired.append(true))
	layer.dispose_immediate()
	assert_that(fired).contains_exactly([true])


func test_disposed_layer_rejects_mutations() -> void:
	layer.dispose_immediate()
	layer.add_member(99)
	assert_that(layer.has_member(99)).is_false()


func test_root_layer_cannot_be_disposed() -> void:
	var root_id := interest.root.id
	interest.root.dispose_immediate()
	# Root is still registered after a guarded dispose attempt.
	assert_that(interest.layer(root_id)).is_same(interest.root)


# ---------------------------------------------------------------------------
# Mirror mode
# ---------------------------------------------------------------------------

func test_mirror_layer_rejects_server_mutations() -> void:
	var mirror := NetwInterestLayer.new(
			&"mirror:1", NetwInterestLayer.Policy.GRANT, interest)
	mirror._is_mirror = true
	mirror.add_member(7)
	mirror.add_subject(NetwEntity.new())
	assert_that(mirror.has_member(7)).is_false()
	assert_that(mirror.subjects()).is_empty()


func test_mirror_apply_updates_state() -> void:
	var mirror := NetwInterestLayer.new(
			&"mirror:2", NetwInterestLayer.Policy.GRANT, interest)
	mirror._is_mirror = true
	mirror._client_apply_member_added(11)
	assert_that(mirror.has_member(11)).is_true()
	mirror._client_apply_member_removed(11)
	assert_that(mirror.has_member(11)).is_false()


func test_mirror_signals_fire_on_apply() -> void:
	var mirror := NetwInterestLayer.new(
			&"mirror:3", NetwInterestLayer.Policy.GRANT, interest)
	mirror._is_mirror = true
	var added: Array[int] = []
	mirror.member_added.connect(func(p: int): added.append(p))
	mirror._client_apply_member_added(3)
	mirror._client_apply_member_added(3)
	assert_that(added).contains_exactly([3])
