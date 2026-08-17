## What drives the persistence accumulator, observed with no frame.
##
## Persistence counts in wall-clock seconds toward each column's interval, and
## the accumulator has to advance from somewhere. Driving it from a node's
## [method Node._process] means a session that polls without rendering never
## saves, which is what a headless host does every day. This case pins the
## advance to the session's own poll.
##
## What the case can observe is the call, not a commit: the write rides
## [method NetwDatabase.transaction], which settles across frames, and the
## accumulator's delta is real elapsed time that no test can dictate. The poll
## reaching persistence at all is therefore the whole of the change and the
## whole of the evidence.
class_name TestPersistPump
extends NetwTestSuite


class CountingApi:
	extends NetwMultiplayer

	var persist_ticks := 0


	func persist_tick(delta: float) -> void:
		persist_ticks += 1
		super.persist_tick(delta)


# Installs the counting session through the tree's one construction point.
class CountingTree:
	extends MultiplayerTree

	func _make_api() -> NetwMultiplayer:
		return CountingApi.new(SceneMultiplayer.new())


var mt: MultiplayerTree
var api: CountingApi


func before_test() -> void:
	mt = CountingTree.new()
	mt.name = "PersistPumpTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api as CountingApi


## Each poll advances persistence once, so a session driven by pumps alone keeps
## saving.
func test_each_poll_advances_persistence_once() -> void:
	var before := api.persist_ticks

	api.poll()

	assert_int(api.persist_ticks - before).override_failure_message(
		"a poll that skips persistence leaves a frameless session unable to save",
	).is_equal(1)

	api.poll()
	api.poll()

	assert_int(api.persist_ticks - before).is_equal(3)
