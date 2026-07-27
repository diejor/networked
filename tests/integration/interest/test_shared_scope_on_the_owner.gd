## Live coverage for [method NetwInterestInterface.shared_entities] off the
## server.
class_name TestSharedScopeOnTheOwner
extends NetwTestSuite

const MAIN := preload("res://examples/quick_start/Main.tscn")
const DATABASE := preload(
	"res://examples/quick_start/quick_start_database.tres"
)
const LEVEL_1_SPAWN := "uid://bqi7mvxdnvgch::Player"

var game: NetwGameHarness


func before() -> void:
	var fs := DATABASE.backend as FileSystemDatabase
	fs.base_dir = create_temp_dir("shared_scope_saves")


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()
	game.show_views()


# A peer that computes no admission still has to answer "who shares my scope",
# because every consumer of that answer runs on the owner: an island roster, the
# contact-equivalence test, and the out-of-domain window are all decided where
# the entity is predicted, never where it is consumed.
#
# The answer is derived rather than replicated. A client is told its own row and
# nothing else on purpose, so this walks the routes it already holds and keeps
# the ones whose own declared labels intersect its own. That reaches the same
# roster the server committed without carrying any part of another peer's row.
func test_the_owner_resolves_the_same_shared_scope_the_server_committed() -> void:
	var host := await game.add_host("valeria", true, _level_1_spawn())
	var client := await game.add_client("jose", true, _level_1_spawn())
	await game.wait_for_transitions()
	await game.sync_ticks(2)

	# A label is declared, not replicated. A game states it in _init so every
	# peer's copy carries it, and only the server turns that declaration into
	# committed membership. This mirrors that: the same join runs on each peer's
	# own copy, and the server's is the one that commits.
	#
	# A label the server adds at runtime is therefore NOT visible to the owner,
	# because nothing carries it there. That is a real limit of this derivation
	# and the reason the join below is not server-only.
	var host_car := NetwEntity.of(host.find_player(&"valeria"))
	var client_car := NetwEntity.of(host.find_player(&"jose"))
	assert_that(host_car).is_not_null()
	assert_that(client_car).is_not_null()
	host_car.interest.join(&"arena")
	client_car.interest.join(&"arena")
	for runner: NetwSceneRunner in [host, client]:
		for username: StringName in [&"valeria", &"jose"] as Array[StringName]:
			var local := NetwEntity.of(runner.find_player(username))
			if local:
				local.interest.join(&"arena")
	host.tree.api.interest.flush()
	await game.sync_ticks(8)

	# The server's own view, which is the roster being matched against.
	var server_scope := host.tree.api.interest.shared_entities(
		client_car,
		&"arena",
	)

	# The owner's view of its own car. The entity instance differs per peer, so
	# the rosters compare by entity id.
	var owner_car := _local_entity(client)
	assert_that(owner_car).is_not_null()
	var owner_scope := client.tree.api.interest.shared_entities(
		owner_car,
		&"arena",
	)

	assert_int(owner_scope.size()).override_failure_message(
		"the owner resolved an empty shared scope while the server resolved %d. "
		% server_scope.size()
		+ "Every consumer of a declared island runs on the owner, so an empty "
		+ "roster there silently disables the feature rather than failing.",
	).is_equal(server_scope.size())
	assert_array(_ids(owner_scope)).override_failure_message(
		"owner scope %s does not match the server's %s"
		% [_ids(owner_scope), _ids(server_scope)],
	).contains_exactly(_ids(server_scope))

	# The budget the derivation must not exceed: it names only entities this
	# peer already holds a live route for.
	var live := client.tree.api.liveness.live_entities()
	for member: NetwEntity in owner_scope:
		assert_bool(member in live).override_failure_message(
			"the owner named %s, which it holds no live route for, so the "
			% member.entity_id
			+ "derivation reached past its own row",
		).is_true()


func _local_entity(runner: NetwSceneRunner) -> NetwEntity:
	var node := runner.local_player
	return NetwEntity.of(node) if node else null


func _ids(entities: Array) -> Array:
	var out: Array = []
	for entity: NetwEntity in entities:
		out.append(String(entity.entity_id))
	out.sort()
	return out


func _level_1_spawn() -> SceneNodePath:
	return SceneNodePath.new(LEVEL_1_SPAWN)
