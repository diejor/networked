## Unit tests for the [NetwEntity] lag-compensation slots.
##
## Covers the state/input/timeline/prediction accessors over the generic
## provide/slot plumbing, the synchronizer self-registration at
## NOTIFICATION_PARENTED, and the input authority binding that follows the
## entity controller.
class_name TestNetwEntitySlots
extends NetwTestSuite

func test_slots_provide_and_retrieve() -> void:
	var entity := NetwEntity.new()
	var state := auto_free(StateSynchronizer.new()) as StateSynchronizer
	var input := auto_free(InputSynchronizer.new()) as InputSynchronizer
	var timeline := NetwTimeline.new()

	entity.state = state
	entity.input = input
	entity.timeline = timeline

	assert_bool(entity.state == state).is_true()
	assert_bool(entity.input == input).is_true()
	assert_bool(entity.timeline == timeline).is_true()
	assert_bool(entity.prediction == null).is_true()


func test_state_synchronizer_self_registers_slot() -> void:
	var root := make_test_entity(self, "Player", 0, false)
	var entity := NetwEntity.of(root)

	var sync := StateSynchronizer.new()
	sync.name = "State"
	root.add_child(sync)

	assert_bool(entity.state == sync).is_true()
	# Server Authority Protection keeps it on the server.
	assert_int(sync.get_multiplayer_authority()).is_equal(1)


func test_input_synchronizer_binds_authority_to_controller() -> void:
	var root := make_test_entity(self, "Player", 7, false)
	var entity := NetwEntity.of(root)
	entity.controller = 7

	var sync := InputSynchronizer.new()
	sync.name = "Input"
	root.add_child(sync)

	assert_bool(entity.input == sync).is_true()
	assert_int(sync.get_multiplayer_authority()).is_equal(7)

	# A mid-game control transfer re-pins authority.
	entity.controller = 9
	assert_int(sync.get_multiplayer_authority()).is_equal(9)
