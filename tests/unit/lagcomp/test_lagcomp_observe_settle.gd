## The rewind engine's second look at a node that entered the tree, observed
## with no frame.
##
## A node enters the tree before whatever stamps a [NetwEntity] onto it has run,
## so [NetwMultiplayer] reads it twice: once on the spot and once after the cascade
## that added it. These cases pin the second read to the session's own settle,
## with nothing awaited and no frame driven, and pin its key to the node,
## because a second look keyed by anything less drops every node in a batch but
## the last.
class_name TestLagcompObserveSettle
extends NetwTestSuite

var mt: MultiplayerTree
var engine: NetwMultiplayer


func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "TestTree"
	add_child(mt)
	auto_free(mt)
	mt.add_child(MultiplayerClock.new())
	mt.add_child(LagCompensation.new())
	engine = mt.api


## An entity stamped after its node entered the tree is picked up by the settle,
## which adopts whatever optimistic effect the entity's own key armed.
func test_an_entity_stamped_after_entry_is_observed_at_the_settle() -> void:
	var body := _add_body("Late", &"late-subject")

	assert_bool(mt.api.effect_pending(&"late-subject")).override_failure_message(
		"the arm must outlive the add, or the adopt proves nothing",
	).is_true()
	assert_bool(_observe_queued(body)).is_true()

	mt.api._settle()

	assert_bool(_observe_queued(body)).is_false()
	assert_bool(mt.api.effect_pending(&"late-subject")).is_false()


## Two nodes added in one cascade are both observed, which is what keying by the
## node buys. A shared key would coalesce them into one row and leave the first
## node unread.
func test_two_nodes_added_together_are_both_observed() -> void:
	_add_body("FirstLate", &"first-subject")
	_add_body("SecondLate", &"second-subject")

	mt.api._settle()

	assert_bool(mt.api.effect_pending(&"first-subject")) \
			.override_failure_message(
				"the first node added must not lose its second look to the next",
			).is_false()
	assert_bool(mt.api.effect_pending(&"second-subject")).is_false()


# Whether the session holds a second look at [param node] for its next settle.
func _observe_queued(node: Node) -> bool:
	return mt.api._native_core.settle_has_key(
		StringName("lagcomp-observe-node?%d" % node.get_instance_id()),
	)


# A node that enters the tree bare and is stamped afterwards, which is the
# ordering the second look exists for, carrying an armed effect under
# [param entity_id] so the adopt has something to resolve.
func _add_body(node_name: String, entity_id: StringName) -> Node:
	var body := Node.new()
	body.name = node_name
	mt.add_child(body)
	auto_free(body)
	mt.api.effect_arm(entity_id, func() -> void: pass)
	var entity := NetwEntity.ensure(body)
	entity.entity_id = entity_id
	return body
