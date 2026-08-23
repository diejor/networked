## Unit tests for the session roster's name policy, the one place two players
## arriving under one name is settled.
##
## The policy itself is lawed natively on [NetwMultiplayerCore]; what these
## cases hold is that the roster hands it the right facts about the peer.
class_name TestSessionRosterNames
extends NetwTestSuite

const SessionRoster := preload("res://addons/networked/session/session_roster.gd")

var _roster
var _held: Array[NetwEntity] = []


func before_test() -> void:
	_roster = SessionRoster.new(NetwMultiplayerCore.new())
	_held = [_player("ana")]


func _player(id: StringName) -> NetwEntity:
	var node := Node2D.new()
	node.name = "Held"
	add_child(node)
	auto_free(node)
	var entity := NetwEntity.ensure(node)
	entity.entity_id = id
	return entity


func _join(peer_id: int, username: StringName, is_debug: bool) -> ResolvedJoin:
	var rj := ResolvedJoin.new()
	rj.peer_id = peer_id
	rj.username = username
	rj.is_debug = is_debug
	return rj


func test_a_free_name_is_admitted_unchanged() -> void:
	var rj := _join(4, &"bo", false)

	assert_bool(_roster.resolve_username_collision(rj, _held, Callable())).is_true()
	assert_str(String(rj.username)).is_equal("bo")


func test_a_debug_join_renames_around_a_name_already_held() -> void:
	var rj := _join(4, &"ana", true)

	assert_bool(_roster.resolve_username_collision(rj, _held, Callable())).is_true()
	assert_str(String(rj.username)).is_not_equal("ana")
	assert_str(String(rj.username)).is_equal("ana1")


## The refusal has to reach the error channel: a peer dropped without a word
## is a join that looks like a network fault to whoever wrote the client.
func test_an_authenticated_collision_is_refused_and_the_peer_dropped() -> void:
	var rj := _join(4, &"ana", false)
	_roster.get_peer_context(4).get_bucket(NetwIdentityBucket).identity = (
		NetwIdentity.new()
	)
	var dropped: Array[int] = []
	var verdicts: Array[bool] = []

	await assert_error(
		func() -> void:
			verdicts.append(_roster.resolve_username_collision(
				rj,
				_held,
				func(peer_id: int) -> void: dropped.append(peer_id),
			))
	).is_push_error(GdUnitArgumentMatchers.any())

	assert_array(verdicts).contains_exactly([false])
	assert_array(dropped).contains_exactly([4])


func test_an_unauthenticated_collision_is_admitted_under_the_same_name() -> void:
	# Nothing proves the name belongs to whoever holds it, so the session says
	# so and lets both in rather than locking one out on a first-come claim.
	var rj := _join(4, &"ana", false)

	assert_bool(_roster.resolve_username_collision(rj, _held, Callable())).is_true()
	assert_str(String(rj.username)).is_equal("ana")
