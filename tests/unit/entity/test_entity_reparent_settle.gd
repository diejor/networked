## The move an entity reports, observed with no frame.
##
## [signal NetwEntity.reparented] is what a camera or any smoothing view resets
## on, and it cannot fire where the move happens: the entity re-enters the tree
## mid-propagation, with its own subtree still rebuilding. These cases pin the
## report to the session's own settle, by which point the subtree is whole, and
## pin it unkeyed, because two moves in one cascade are two discontinuities and
## a consumer owes each of them a reset.
class_name TestEntityReparentSettle
extends NetwTestSuite

var mt: MultiplayerTree
var home: Node
var away: Node
var body: Node
var entity: NetwEntity
var moves: Array[NetwReparentOpts] = []


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "ReparentTree"
	add_child(mt)
	auto_free(mt)

	home = Node.new()
	home.name = "Home"
	mt.add_child(home)
	away = Node.new()
	away.name = "Away"
	mt.add_child(away)

	body = Node.new()
	body.name = "Traveller"
	entity = NetwEntity.ensure(body)
	entity.entity_id = &"traveller"
	home.add_child(body)
	auto_free(body)

	moves = []
	entity.reparented.connect(
		func(opts: NetwReparentOpts) -> void: moves.append(opts)
	)


## The move does not report where it happens, and the settle is what reports it,
## with nothing awaited and no frame driven.
func test_a_move_reports_at_the_settle_with_no_frame() -> void:
	var opts := NetwReparentOpts.new()
	entity.reparent_to(away, opts)

	assert_object(body.get_parent()).is_same(away)
	assert_array(moves).override_failure_message(
		"a report fired mid-propagation would see a half-built subtree",
	).is_empty()

	mt.api._settle()

	assert_array(moves).contains_exactly([opts])


## Two moves in one cascade report twice. A report keyed by the entity would
## coalesce them into one and swallow the first destination entirely.
func test_two_moves_in_one_cascade_report_twice() -> void:
	var first := NetwReparentOpts.new()
	var second := NetwReparentOpts.new()

	entity.reparent_to(away, first)
	entity.reparent_to(home, second)

	mt.api._settle()

	assert_array(moves).override_failure_message(
		"the first move must not lose its report to the second",
	).contains_exactly([first, second])
